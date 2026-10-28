#pragma once
// Experimental late linear-LDR SR: preserve original UI composition, and
// replace original temporal AA color only while a bounded test is armed.
#include "HeatShadowBackend.h"
namespace HeatFinal {
using Microsoft::WRL::ComPtr;
using HeatShadow::check;
struct Bridge {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11Texture2D> depth,motion;
    ComPtr<ID3D11ShaderResourceView> depthSrv,motionSrv,rawColor;
    ComPtr<ID3D11Buffer> taaConstants,finalReadback,finalConstants;
    ComPtr<ID3D11ComputeShader> rawCopy;
    UINT width=0,height=0;
    bool pending=false;
    unsigned captures=0,composites=0;
    static D3D11_TEXTURE2D_DESC describe(ID3D11View* view) {
        if(!view)throw std::runtime_error("SR missing view");
        ComPtr<ID3D11Resource> r;ComPtr<ID3D11Texture2D> t;view->GetResource(&r);
        check(r.As(&t),"SR texture view");D3D11_TEXTURE2D_DESC d{};t->GetDesc(&d);return d;
    }
    void copyInput(ID3D11DeviceContext* c,ID3D11ShaderResourceView* src,
                   ComPtr<ID3D11Texture2D>& dst,ComPtr<ID3D11ShaderResourceView>& srv) {
        auto d=describe(src);
        if(d.Width!=width||d.Height!=height||d.ArraySize!=1||d.SampleDesc.Count!=1||d.MipLevels!=1)
            throw std::runtime_error("SR auxiliary resource layout changed");
        if(!dst){d.Usage=D3D11_USAGE_DEFAULT;d.CPUAccessFlags=0;d.MiscFlags=0;d.BindFlags=D3D11_BIND_SHADER_RESOURCE;
            check(device->CreateTexture2D(&d,nullptr,&dst),"SR auxiliary copy texture");
            D3D11_SHADER_RESOURCE_VIEW_DESC sd{};src->GetDesc(&sd);
            check(device->CreateShaderResourceView(dst.Get(),&sd,&srv),"SR auxiliary copy SRV");}
        ComPtr<ID3D11Resource> resource;src->GetResource(&resource);c->CopyResource(dst.Get(),resource.Get());
    }
    void capture(ID3D11DeviceContext1* c) {
        pending=false;ComPtr<ID3D11ShaderResourceView> ds,mv,color;
        c->CSGetShaderResources(0,1,&ds);c->CSGetShaderResources(1,1,&mv);c->CSGetShaderResources(3,1,&color);
        auto cd=describe(color.Get());
        if(cd.Format!=DXGI_FORMAT_R11G11B10_FLOAT||cd.Width<64||cd.Height<64||cd.Width>4096||cd.Height>4096)
            throw std::runtime_error("SR unrecognized main color");
        if(!device){c->GetDevice(&device);width=cd.Width;height=cd.Height;
            D3D11_BUFFER_DESC bd{};bd.ByteWidth=512;bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
            check(device->CreateBuffer(&bd,nullptr,&taaConstants),"SR TAA constants");
            bd.ByteWidth=256;check(device->CreateBuffer(&bd,nullptr,&finalConstants),"SR final constants");
            bd.BindFlags=0;bd.Usage=D3D11_USAGE_STAGING;bd.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
            check(device->CreateBuffer(&bd,nullptr,&finalReadback),"SR final readback");
            const char* code=R"(Texture2D<float3> inputColor:register(t0);RWTexture2D<float3> outputColor:register(u0);
                [numthreads(8,8,1)] void main(uint3 p:SV_DispatchThreadID){uint w,h;outputColor.GetDimensions(w,h);if(p.x<w&&p.y<h)outputColor[p.xy]=inputColor.Load(int3(p.xy,0));})";
            ComPtr<ID3DBlob> blob,error;check(D3DCompile(code,strlen(code),"HeatRawTAA",nullptr,nullptr,"main","cs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&blob,&error),"SR raw shader compile");
            check(device->CreateComputeShader(blob->GetBufferPointer(),blob->GetBufferSize(),nullptr,&rawCopy),"SR raw shader");
        }
        if(cd.Width!=width||cd.Height!=height)throw std::runtime_error("SR render size changed");
        ComPtr<ID3D11Buffer> cb;UINT first=0,count=0;c->CSGetConstantBuffers1(0,1,&cb,&first,&count);
        if(!cb||count<32)throw std::runtime_error("SR missing temporal constants");
        D3D11_BUFFER_DESC bd{};cb->GetDesc(&bd);if(uint64_t(first)*16+512>bd.ByteWidth)throw std::runtime_error("SR temporal constants bounds");
        D3D11_BOX box{first*16,0,0,first*16+512,1,1};c->CopySubresourceRegion(taaConstants.Get(),0,0,0,0,cb.Get(),0,&box);
        copyInput(c,ds.Get(),depth,depthSrv);copyInput(c,mv.Get(),motion,motionSrv);
        rawColor=color;pending=true;++captures;
    }
    void rawAfterTaa(ID3D11DeviceContext1* c) {
        if(!pending)return;
        ComPtr<ID3D11UnorderedAccessView> target,history;c->CSGetUnorderedAccessViews(2,1,&target);c->CSGetUnorderedAccessViews(3,1,&history);
        auto td=describe(target.Get());if(td.Width!=width||td.Height!=height||td.Format!=DXGI_FORMAT_R11G11B10_FLOAT)throw std::runtime_error("SR raw output mismatch");
        ComPtr<ID3D11Resource> tr,hr;target->GetResource(&tr);if(history)history->GetResource(&hr);
        if(tr.Get()==hr.Get())throw std::runtime_error("SR refusing to alter TAA history");
        HeatBridge::ComputeBindings guard(c);HeatShadow::clearBackendBindings(c);
        c->CSSetShader(rawCopy.Get(),nullptr,0);c->CSSetShaderResources(0,1,rawColor.GetAddressOf());
        c->CSSetUnorderedAccessViews(0,1,target.GetAddressOf(),nullptr);c->Dispatch((width+7)/8,(height+7)/8,1);
    }
    // Caller invokes the original final draw while this restores PS state via RAII.
    template<class Draw> std::string compose(ID3D11DeviceContext1* c,const std::wstring& runtime,Draw draw) {
        if(!pending)return {};
        pending=false;
        ComPtr<ID3D11ShaderResourceView> input;c->PSGetShaderResources(0,1,&input);
        ComPtr<ID3D11RenderTargetView> target;c->OMGetRenderTargets(1,&target,nullptr);
        auto id=describe(input.Get()),od=describe(target.Get());
        if(id.Width!=width||id.Height!=height||od.Width<width||od.Height<height||od.Width>4096||od.Height>4096)
            throw std::runtime_error("SR composition dimensions mismatch");
        D3D11_SHADER_RESOURCE_VIEW_DESC inputView{};input->GetDesc(&inputView);
        if(inputView.Format!=DXGI_FORMAT_R10G10B10A2_UNORM)throw std::runtime_error("SR requires captured linear LDR tonemap format");
        auto& b=HeatShadow::instance();std::string result;
        {
            HeatBridge::ComputeBindings saved(c);HeatShadow::clearBackendBindings(c);
            c->CSSetShaderResources(0,1,depthSrv.GetAddressOf());c->CSSetShaderResources(1,1,motionSrv.GetAddressOf());
            c->CSSetShaderResources(3,1,input.GetAddressOf());UINT zero=0,count=32;
            c->CSSetConstantBuffers1(0,1,taaConstants.GetAddressOf(),&zero,&count);
            result=b.run(c,runtime,od.Width,od.Height,true);
        }
        struct RestorePS {
            ID3D11DeviceContext1* c;ComPtr<ID3D11ShaderResourceView> srv;ComPtr<ID3D11Buffer> cb;UINT first=0,count=0;
            explicit RestorePS(ID3D11DeviceContext1* context):c(context){c->PSGetShaderResources(0,1,&srv);c->PSGetConstantBuffers1(0,1,&cb,&first,&count);}
            ~RestorePS(){c->PSSetShaderResources(0,1,srv.GetAddressOf());c->PSSetConstantBuffers1(0,1,cb.GetAddressOf(),&first,&count);}
        } restore(c);
        if(!restore.cb||restore.count<16)throw std::runtime_error("SR final CB missing");
        D3D11_BUFFER_DESC bd{};restore.cb->GetDesc(&bd);if(uint64_t(restore.first)*16+256>bd.ByteWidth)throw std::runtime_error("SR final CB bounds");
        D3D11_BOX box{restore.first*16,0,0,restore.first*16+256,1,1};c->CopySubresourceRegion(finalReadback.Get(),0,0,0,0,restore.cb.Get(),0,&box);
        D3D11_MAPPED_SUBRESOURCE map{};check(c->Map(finalReadback.Get(),0,D3D11_MAP_READ,0,&map),"SR final CB map");
        float values[64]{};memcpy(values,map.pData,sizeof(values));c->Unmap(finalReadback.Get(),0);
        if(std::abs(values[0]-float(width))>.5f||std::abs(values[1]-float(height))>.5f)throw std::runtime_error("SR final shader CB layout unrecognized");
        values[0]=float(od.Width);values[1]=float(od.Height);values[2]=1.0f/od.Width;values[3]=1.0f/od.Height;
        c->UpdateSubresource(finalConstants.Get(),0,nullptr,values,0,0);UINT zero=0,count=16;
        c->PSSetConstantBuffers1(0,1,finalConstants.GetAddressOf(),&zero,&count);c->PSSetShaderResources(0,1,b.outputSrv.GetAddressOf());
        draw();++composites;
        if(composites==1||composites%30==0)result+=" FINAL_SR_COMPOSE count="+std::to_string(composites)+" input="+std::to_string(width)+"x"+std::to_string(height)+" output="+std::to_string(od.Width)+"x"+std::to_string(od.Height)+" originalUiPreserved=1";
        return result;
    }
};
inline Bridge& instance(){static Bridge* b=new Bridge;return *b;}
}
