#pragma once
// Original bounded real-input diagnostic. Never replaces game output/history.
// Each of the three evaluations resets history: no temporal-quality claim.
#include <d3dcompiler.h>
#include <filesystem>
#include <stdexcept>
#include <vector>
#include <fstream>
#include <d3d11_4.h>
#include "nvsdk_ngx.h"
#include "../state-guard-test/HeatContextStateGuard.h"
#include "HeatProjection.h"

namespace HeatShadow {
using Microsoft::WRL::ComPtr;
// Serialize the complete capture/evaluation/restore transaction with all
// protected immediate-context callers, including the backend's present path.
struct ContextTransaction {
    ComPtr<ID3D11Multithread> lock;
    explicit ContextTransaction(ID3D11DeviceContext* context) {
#if defined(HEAT_FG_SERIALIZE_CONTEXT) && !defined(HEAT_FG_NO_STATE_SWAP)
        if(FAILED(context->QueryInterface(IID_PPV_ARGS(&lock))))
            throw std::runtime_error("Immediate context has no multithread protection");
        lock->SetMultithreadProtected(TRUE);
        lock->Enter();
#else
        (void)context;
#endif
    }
    ~ContextTransaction(){if(lock)lock->Leave();}
};
inline void clearBackendBindings(ID3D11DeviceContext* c) {
#ifdef HEAT_FG_NO_STATE_SWAP
    ID3D11ShaderResourceView* srvs[128]{};ID3D11UnorderedAccessView* uavs[64]{};
    ID3D11Buffer* cb[14]{};ID3D11SamplerState* samplers[16]{};
    c->CSSetShader(nullptr,nullptr,0);c->CSSetShaderResources(0,128,srvs);
    c->CSSetUnorderedAccessViews(0,64,uavs,nullptr);c->CSSetConstantBuffers(0,14,cb);c->CSSetSamplers(0,16,samplers);
#else
    c->ClearState();
#endif
}
inline void check(HRESULT hr,const char* what) {
    if(FAILED(hr)) throw std::runtime_error(std::string(what)+" HRESULT="+std::to_string(hr));
}
inline void ngxCheck(NVSDK_NGX_Result r,const char* what) {
    if(r!=NVSDK_NGX_Result_Success) throw std::runtime_error(std::string(what)+" NGX="+std::to_string(r));
}
template<class T> T get(HMODULE m,const char* n) {
    auto p=reinterpret_cast<T>(GetProcAddress(m,n));
    if(!p) throw std::runtime_error(std::string("Missing export ")+n);
    return p;
}
inline float half(unsigned short h) {
    int s=(h&0x8000)?-1:1, e=(h>>10)&31, f=h&1023;
    if(e==31) return f?NAN:s*INFINITY;
    return s*std::ldexp(e?1.0f+float(f)/1024:float(f),e?e-15:-24);
}
struct Surface {
    ComPtr<ID3D11Texture2D> texture;
    ComPtr<ID3D11UnorderedAccessView> uav;
};
struct Backend {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3DDeviceContextState> isolated;
    ComPtr<ID3D11ComputeShader> convert;
    ComPtr<ID3D11ComputeShader> copyBack;
    ComPtr<ID3D11ShaderResourceView> outputSrv;
    ComPtr<ID3D11Buffer> cbReadback;
    Surface depth,motion,color,output;
    Surface exposure;
    NVSDK_NGX_Parameter* parameters=nullptr;
    NVSDK_NGX_Handle* feature=nullptr;
    decltype(&NVSDK_NGX_D3D11_EvaluateFeature) evaluate=nullptr;
    decltype(&NVSDK_NGX_D3D11_ReleaseFeature) release=nullptr;
    UINT width=0,height=0;
    UINT outputWidth=0,outputHeight=0;
    bool ldrInput=false;
    unsigned frames=0;
    bool failed=false;
    ULONGLONG firstTarget=0;
    float accumulatedExposure=1.0f;
    unsigned replacements=0;

    std::string dump(ID3D11DeviceContext1* c,ID3D11Texture2D* tex,const std::wstring& path) {
        D3D11_TEXTURE2D_DESC d{};tex->GetDesc(&d);d.Usage=D3D11_USAGE_STAGING;d.BindFlags=0;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Texture2D> staging;check(device->CreateTexture2D(&d,nullptr,&staging),"dump staging");
        c->CopyResource(staging.Get(),tex);D3D11_MAPPED_SUBRESOURCE map{};check(c->Map(staging.Get(),0,D3D11_MAP_READ,0,&map),"dump map");
        const UINT dumpW=d.Width,dumpH=d.Height;
        std::vector<unsigned> pixels(size_t(dumpW)*dumpH);double energy=0;size_t nonzero=0;
        for(UINT y=0;y<dumpH;y++){auto row=reinterpret_cast<const unsigned short*>(static_cast<const char*>(map.pData)+y*map.RowPitch);
            for(UINT x=0;x<dumpW;x++){unsigned p=0xff000000;double total=0;for(UINT k=0;k<3;k++){
                float v=half(row[x*4+k]);if(!std::isfinite(v))v=0;v=std::max(0.0f,v);energy+=v;total+=v;
                unsigned mapped=unsigned(255*std::pow(v/(1+v),1.0f/2.2f)+.5f);p|=mapped<<((2-k)*8);}
                nonzero+=total>.0001;pixels[size_t(y)*dumpW+x]=p;}}
        c->Unmap(staging.Get(),0);
        BITMAPFILEHEADER f{};BITMAPINFOHEADER h{};f.bfType=0x4d42;f.bfOffBits=sizeof(f)+sizeof(h);f.bfSize=f.bfOffBits+DWORD(pixels.size()*4);
        h.biSize=sizeof(h);h.biWidth=LONG(dumpW);h.biHeight=-LONG(dumpH);h.biPlanes=1;h.biBitCount=32;h.biCompression=BI_RGB;
        std::ofstream out(path,std::ios::binary);out.write(reinterpret_cast<const char*>(&f),sizeof(f));out.write(reinterpret_cast<const char*>(&h),sizeof(h));out.write(reinterpret_cast<const char*>(pixels.data()),pixels.size()*4);
        if(!out)throw std::runtime_error("dump write failed");
        return "meanRGB="+std::to_string(energy/(dumpW*dumpH*3))+" nonBlackPixels="+std::to_string(nonzero);
    }

    Surface make(DXGI_FORMAT format,UINT w=0,UINT h=0) {
        Surface s; D3D11_TEXTURE2D_DESC d{};
        d.Width=w?w:width; d.Height=h?h:height; d.Format=format; d.MipLevels=d.ArraySize=d.SampleDesc.Count=1;
        d.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS;
        check(device->CreateTexture2D(&d,nullptr,&s.texture),"create independent texture");
        check(device->CreateUnorderedAccessView(s.texture.Get(),nullptr,&s.uav),"create independent UAV");
        return s;
    }
    void init(ID3D11DeviceContext1* c,UINT w,UINT h,const std::wstring& runtime,UINT ow=0,UINT oh=0,bool ldr=false) {
        width=w;height=h;c->GetDevice(&device);
        outputWidth=ow?ow:w;outputHeight=oh?oh:h;ldrInput=ldr;
        check(HeatBridge::CreateIsolatedContextState(device.Get(),&isolated),"create isolated state");
        HeatBridge::ContextStateGuard guard(c,isolated.Get());
        depth=make(DXGI_FORMAT_R32_FLOAT);motion=make(DXGI_FORMAT_R16G16_FLOAT);
        color=make(DXGI_FORMAT_R16G16B16A16_FLOAT);output=make(DXGI_FORMAT_R16G16B16A16_FLOAT,outputWidth,outputHeight);
        if(ldrInput){exposure=make(DXGI_FORMAT_R32_FLOAT,1,1);const float one[]={1,1,1,1};c->ClearUnorderedAccessViewFloat(exposure.uav.Get(),one);}
        const char* source=R"(
Texture2D<float> srcDepth:register(t0);
Texture2D<float2> srcMotion:register(t1);
Texture2D<float3> srcColor:register(t2);
RWTexture2D<float> dstDepth:register(u0);
RWTexture2D<float2> dstMotion:register(u1);
RWTexture2D<float4> dstColor:register(u2);
[numthreads(8,8,1)] void main(uint3 id:SV_DispatchThreadID) {
 uint w,h;dstColor.GetDimensions(w,h);if(id.x>=w||id.y>=h)return;
 dstDepth[id.xy]=srcDepth.Load(int3(id.xy,0));
 dstMotion[id.xy]=srcMotion.Load(int3(id.xy,0));
 dstColor[id.xy]=float4(srcColor.Load(int3(id.xy,0)),1);
})";
        ComPtr<ID3DBlob> blob,error;
        check(D3DCompile(source,strlen(source),"HeatShadowConvert",nullptr,nullptr,"main","cs_5_0",
                         D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&blob,&error),"compile conversion");
        check(device->CreateComputeShader(blob->GetBufferPointer(),blob->GetBufferSize(),nullptr,&convert),"create conversion");
#ifdef HEAT_DISPLAY_TEST
        check(device->CreateShaderResourceView(output.texture.Get(),nullptr,&outputSrv),"output SRV");
        const char* copySource=R"(
Texture2D<float4> inputColor:register(t0);RWTexture2D<float3> outputColor:register(u0);
[numthreads(8,8,1)] void main(uint3 id:SV_DispatchThreadID){uint w,h;outputColor.GetDimensions(w,h);
if(id.x<w&&id.y<h)outputColor[id.xy]=inputColor.Load(int3(id.xy,0)).rgb;})";
        blob.Reset();error.Reset();check(D3DCompile(copySource,strlen(copySource),"HeatCopyBack",nullptr,nullptr,"main","cs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&blob,&error),"compile copyback");
        check(device->CreateComputeShader(blob->GetBufferPointer(),blob->GetBufferSize(),nullptr,&copyBack),"create copyback");
#endif
        std::filesystem::path backendPath=std::filesystem::path(runtime)/L"dxgi.dll";
#ifdef HEAT_FG_GAME
        wchar_t exe[32768]{};GetModuleFileNameW(nullptr,exe,32768);backendPath=std::filesystem::path(exe).parent_path()/L"dxgi.dll";
#endif
        HMODULE m=LoadLibraryExW(backendPath.c_str(),nullptr,LOAD_WITH_ALTERED_SEARCH_PATH);
        if(!m)throw std::runtime_error("load verified backend");
        auto init=get<decltype(&NVSDK_NGX_D3D11_Init)>(m,"NVSDK_NGX_D3D11_Init");
        auto caps=get<decltype(&NVSDK_NGX_D3D11_GetCapabilityParameters)>(m,"NVSDK_NGX_D3D11_GetCapabilityParameters");
        auto create=get<decltype(&NVSDK_NGX_D3D11_CreateFeature)>(m,"NVSDK_NGX_D3D11_CreateFeature");
        evaluate=get<decltype(evaluate)>(m,"NVSDK_NGX_D3D11_EvaluateFeature");
        release=get<decltype(release)>(m,"NVSDK_NGX_D3D11_ReleaseFeature");
        NVSDK_NGX_FeatureCommonInfo info{};
        ngxCheck(init(0x1337,runtime.c_str(),device.Get(),&info,NVSDK_NGX_Version_API),"init");
        ngxCheck(caps(&parameters),"capabilities");if(!parameters)throw std::runtime_error("null parameters");
        auto p=parameters;
        p->Set(NVSDK_NGX_Parameter_Width,w);p->Set(NVSDK_NGX_Parameter_Height,h);
        p->Set(NVSDK_NGX_Parameter_OutWidth,outputWidth);p->Set(NVSDK_NGX_Parameter_OutHeight,outputHeight);
        p->Set(NVSDK_NGX_Parameter_PerfQualityValue,int(outputWidth==w&&outputHeight==h?NVSDK_NGX_PerfQuality_Value_DLAA:NVSDK_NGX_PerfQuality_Value_MaxQuality));
        int flags=NVSDK_NGX_DLSS_Feature_Flags_MVLowRes|NVSDK_NGX_DLSS_Feature_Flags_DepthInverted;
        if(!ldr)flags|=NVSDK_NGX_DLSS_Feature_Flags_IsHDR|NVSDK_NGX_DLSS_Feature_Flags_AutoExposure;
        p->Set(NVSDK_NGX_Parameter_DLSS_Feature_Create_Flags,flags);
        ngxCheck(create(c,NVSDK_NGX_Feature_SuperSampling,p,&feature),"create feature");
        if(!feature)throw std::runtime_error("null feature");
    }
    std::string run(ID3D11DeviceContext* context,const std::wstring& runtime,UINT ow=0,UINT oh=0,bool ldr=false) {
        ContextTransaction transaction(context);
        ComPtr<ID3D11DeviceContext1> c;check(context->QueryInterface(IID_PPV_ARGS(&c)),"context1");
        if(c->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE)throw std::runtime_error("not immediate context");
        ComPtr<ID3D11ShaderResourceView> views[3];
        c->CSGetShaderResources(0,1,&views[0]);c->CSGetShaderResources(1,1,&views[1]);c->CSGetShaderResources(3,1,&views[2]);
        D3D11_TEXTURE2D_DESC desc[3]{};
        for(int i=0;i<3;++i){
            if(!views[i])throw std::runtime_error("missing target SRV");
            ComPtr<ID3D11Resource> r;views[i]->GetResource(&r);ComPtr<ID3D11Texture2D> t;
            check(r.As(&t),"input texture");t->GetDesc(&desc[i]);
            if(desc[i].SampleDesc.Count!=1||desc[i].ArraySize!=1)throw std::runtime_error("unsupported array/MSAA");
        }
        if(desc[0].Width!=desc[2].Width||desc[1].Width!=desc[2].Width||
           desc[0].Height!=desc[2].Height||desc[1].Height!=desc[2].Height)throw std::runtime_error("input dimensions mismatch");
        if(desc[2].Width<64||desc[2].Width>4096||desc[2].Height<64||desc[2].Height>4096)throw std::runtime_error("input size bounds");
        if(!feature)init(c.Get(),desc[2].Width,desc[2].Height,runtime,ow,oh,ldr);
        if(width!=desc[2].Width||height!=desc[2].Height)throw std::runtime_error("size changed during bounded test");
        if((ow&&outputWidth!=ow)||(oh&&outputHeight!=oh)||ldr!=ldrInput)throw std::runtime_error("output configuration changed during bounded test");
        ComPtr<ID3D11ComputeShader> before; c->CSGetShader(&before,nullptr,nullptr);
#ifdef HEAT_FG_NO_STATE_SWAP
        ComPtr<ID3D11PixelShader> beforePS;ComPtr<ID3D11VertexShader> beforeVS;
        ComPtr<ID3D11RenderTargetView> beforeRT;ComPtr<ID3D11DepthStencilView> beforeDS;
        c->PSGetShader(&beforePS,nullptr,nullptr);c->VSGetShader(&beforeVS,nullptr,nullptr);
        c->OMGetRenderTargets(1,&beforeRT,&beforeDS);
#endif
        ComPtr<ID3D11Buffer> beforeCB;UINT first=0,count=0;
        c->CSGetConstantBuffers1(0,1,&beforeCB,&first,&count);
        float jitterX=0,jitterY=0,frameMs=16.6667f,preExposure=1,verticalFov=.78539816f,nearPlane=.06f,farPlane=20000;
        int reset=1;
#ifdef HEAT_DISPLAY_TEST
        // Provisional projection-derived interpretation, bounded visual test.
        // CB0[11..13]/[15..17] contain unjittered/jittered inverse rays.
        if(!beforeCB||count<32)throw std::runtime_error("no complete TAA constant window");
        D3D11_BUFFER_DESC cbd{};beforeCB->GetDesc(&cbd);
        if(uint64_t(first)*16+512>cbd.ByteWidth)throw std::runtime_error("TAA constants outside buffer");
        if(!cbReadback){D3D11_BUFFER_DESC rd{};rd.ByteWidth=512;rd.Usage=D3D11_USAGE_STAGING;rd.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
            check(device->CreateBuffer(&rd,nullptr,&cbReadback),"CB readback");}
        D3D11_BOX box{};box.left=first*16;box.right=box.left+512;box.bottom=box.back=1;
        c->CopySubresourceRegion(cbReadback.Get(),0,0,0,0,beforeCB.Get(),0,&box);
        D3D11_MAPPED_SUBRESOURCE mapped{};check(c->Map(cbReadback.Get(),0,D3D11_MAP_READ,0,&mapped),"map TAA constants");
        float v[128]{};memcpy(v,mapped.pData,sizeof(v));c->Unmap(cbReadback.Get(),0);
        unsigned actualW=0,actualH=0;memcpy(&actualW,v,4);memcpy(&actualH,v+1,4);
        if(actualW!=width||actualH!=height)
            throw std::runtime_error("unrecognized TAA projection layout");
        const auto projection=HeatProjection::decode(v,width,height);
        jitterX=projection.jitterX;jitterY=projection.jitterY;verticalFov=projection.verticalFov;
        nearPlane=projection.nearPlane;farPlane=projection.farPlane;
        frameMs=std::clamp(v[90]*1000,1.0f,200.0f);
        reset=(frames==0||v[95]>.5f)?1:0;
        if(reset)accumulatedExposure=1;
        else if(std::isfinite(v[96])&&v[96]>.1f&&v[96]<10)accumulatedExposure*=v[96];
        if(!std::isfinite(accumulatedExposure)||accumulatedExposure<.0001f||accumulatedExposure>10000){reset=1;accumulatedExposure=1;}
        preExposure=accumulatedExposure;
#endif
        {
            HeatBridge::ContextStateGuard guard(c.Get(),isolated.Get());
            clearBackendBindings(c.Get());
            ID3D11ShaderResourceView* srvs[]={views[0].Get(),views[1].Get(),views[2].Get()};
            ID3D11UnorderedAccessView* uavs[]={depth.uav.Get(),motion.uav.Get(),color.uav.Get()};
            c->CSSetShader(convert.Get(),nullptr,0);c->CSSetShaderResources(0,3,srvs);
            c->CSSetUnorderedAccessViews(0,3,uavs,nullptr);c->Dispatch((width+7)/8,(height+7)/8,1);
            clearBackendBindings(c.Get());const float sentinel[]={.875f,.25f,.625f,1};
            c->ClearUnorderedAccessViewFloat(output.uav.Get(),sentinel);
            auto p=parameters;
            p->Set(NVSDK_NGX_Parameter_Color,static_cast<ID3D11Resource*>(color.texture.Get()));
            p->Set(NVSDK_NGX_Parameter_Depth,static_cast<ID3D11Resource*>(depth.texture.Get()));
            p->Set(NVSDK_NGX_Parameter_MotionVectors,static_cast<ID3D11Resource*>(motion.texture.Get()));
            p->Set(NVSDK_NGX_Parameter_Output,static_cast<ID3D11Resource*>(output.texture.Get()));
            if(ldrInput)p->Set(NVSDK_NGX_Parameter_ExposureTexture,static_cast<ID3D11Resource*>(exposure.texture.Get()));
            // Deliberate diagnostic reset every evaluation: do not assume jitter
            // or pre-exposure correctness before separate temporal validation.
            p->Set(NVSDK_NGX_Parameter_Reset,reset);
            p->Set(NVSDK_NGX_Parameter_Jitter_Offset_X,jitterX);p->Set(NVSDK_NGX_Parameter_Jitter_Offset_Y,jitterY);
            p->Set(NVSDK_NGX_Parameter_MV_Scale_X,-.5f*width);p->Set(NVSDK_NGX_Parameter_MV_Scale_Y,.5f*height);
            p->Set(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Width,width);
            p->Set(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Height,height);
            p->Set(NVSDK_NGX_Parameter_DLSS_Pre_Exposure,ldrInput?1.0f:preExposure);p->Set(NVSDK_NGX_Parameter_DLSS_Exposure_Scale,1.0f);
            p->Set(NVSDK_NGX_Parameter_FrameTimeDeltaInMsec,frameMs);p->Set(NVSDK_NGX_Parameter_Sharpness,0.0f);
            p->Set("FSR.cameraNear",farPlane);p->Set("FSR.cameraFar",nearPlane);p->Set("FSR.cameraFovAngleVertical",verticalFov);
            ngxCheck(evaluate(c.Get(),feature,p,nullptr),"evaluate real inputs");
        }
        ComPtr<ID3D11ComputeShader> after;c->CSGetShader(&after,nullptr,nullptr);
#ifdef HEAT_FG_NO_STATE_SWAP
        ComPtr<ID3D11PixelShader> afterPS;ComPtr<ID3D11VertexShader> afterVS;
        ComPtr<ID3D11RenderTargetView> afterRT;ComPtr<ID3D11DepthStencilView> afterDS;
        c->PSGetShader(&afterPS,nullptr,nullptr);c->VSGetShader(&afterVS,nullptr,nullptr);c->OMGetRenderTargets(1,&afterRT,&afterDS);
        if(beforePS.Get()!=afterPS.Get()||beforeVS.Get()!=afterVS.Get()||beforeRT.Get()!=afterRT.Get()||beforeDS.Get()!=afterDS.Get())
            throw std::runtime_error("Backend modified non-compute pipeline; abort manual-CS mode");
#endif
        ComPtr<ID3D11Buffer> afterCB;UINT afirst=0,acount=0;c->CSGetConstantBuffers1(0,1,&afterCB,&afirst,&acount);
        if(before.Get()!=after.Get()||beforeCB.Get()!=afterCB.Get()||first!=afirst||count!=acount){
            std::ostringstream detail;detail<<"GAME STATE RESTORE MISMATCH shader="<<before.Get()<<"/"<<after.Get()<<" CB="<<beforeCB.Get()<<"/"<<afterCB.Get()<<" first="<<first<<"/"<<afirst<<" count="<<count<<"/"<<acount;
            // Restore the verified static TAA shader and original CB1 window
            // before aborting this experimental backend. Never run the original
            // dispatch with known-corrupted bindings.
            c->CSSetShader(before.Get(),nullptr,0);c->CSSetConstantBuffers1(0,1,beforeCB.GetAddressOf(),&first,&count);
            throw std::runtime_error(detail.str());
        }
#ifdef HEAT_DISPLAY_TEST
        if(frames>=3&&frames!=119){++frames;return frames%30==0?"DISPLAY_EVALUATE frame="+std::to_string(frames)+" jitter="+std::to_string(jitterX)+","+std::to_string(jitterY):std::string();}
#endif
        D3D11_TEXTURE2D_DESC d{};output.texture->GetDesc(&d);d.Usage=D3D11_USAGE_STAGING;d.BindFlags=0;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Texture2D> staging;check(device->CreateTexture2D(&d,nullptr,&staging),"staging output");
        c->CopyResource(staging.Get(),output.texture.Get());D3D11_MAPPED_SUBRESOURCE map{};
        check(c->Map(staging.Get(),0,D3D11_MAP_READ,0,&map),"map output");
        size_t changed=0,finite=0;double energy=0;
        const unsigned short sentinel[]={0x3b00,0x3400,0x3900,0x3c00};
        for(UINT y=0;y<outputHeight;++y){auto row=reinterpret_cast<const unsigned short*>(static_cast<const char*>(map.pData)+y*map.RowPitch);
            for(UINT x=0;x<outputWidth;++x){bool different=false;for(UINT k=0;k<4;++k){float f=half(row[x*4+k]);
                finite+=std::isfinite(f);different|=row[x*4+k]!=sentinel[k];if(k<3&&std::isfinite(f))energy+=std::abs(f);}
                changed+=different;}}
        c->Unmap(staging.Get(),0);++frames;
        if(finite!=size_t(outputWidth)*outputHeight*4||changed<size_t(outputWidth)*outputHeight/2)throw std::runtime_error("invalid actual GPU output");
        std::string captured;
#ifndef HEAT_PUBLIC_BUILD
        if(frames==3){
            auto base=std::filesystem::path(runtime).parent_path()/std::to_wstring(GetCurrentProcessId());
            captured=" input["+dump(c.Get(),color.texture.Get(),base.wstring()+L"-input.bmp")+"] output["+
                dump(c.Get(),output.texture.Get(),base.wstring()+L"-output.bmp")+"]";
        }
#endif
        std::ostringstream s;s<<"SHADOW_GPU_PASS frame="<<frames<<" size="<<width<<'x'<<height<<" changed="<<changed
            <<" output="<<outputWidth<<'x'<<outputHeight<<" finite="<<finite<<" meanRGB="<<energy/(outputWidth*outputHeight*3)<<" stateRestored=1 reset="<<reset<<" jitter="<<jitterX<<','<<jitterY<<" gameOutputReplaced=0"<<captured;
        return s.str();
    }
    bool display(ID3D11DeviceContext* context) {
        ContextTransaction transaction(context);
#ifdef HEAT_DISPLAY_TEST
        if(failed||!feature||frames<3)return false;
        ComPtr<ID3D11DeviceContext1> c;check(context->QueryInterface(IID_PPV_ARGS(&c)),"display context1");
        ComPtr<ID3D11UnorderedAccessView> target,history;c->CSGetUnorderedAccessViews(2,1,&target);c->CSGetUnorderedAccessViews(3,1,&history);
        if(!target)throw std::runtime_error("display output missing");
        ComPtr<ID3D11Resource> r,hr;target->GetResource(&r);if(history)history->GetResource(&hr);
        if(r.Get()==hr.Get())throw std::runtime_error("refuse to overwrite TAA history");
        ComPtr<ID3D11Texture2D> t;check(r.As(&t),"display texture");D3D11_TEXTURE2D_DESC d{};t->GetDesc(&d);
        if(d.Width!=width||d.Height!=height||d.Format!=DXGI_FORMAT_R11G11B10_FLOAT)throw std::runtime_error("display format/size mismatch");
        HeatBridge::ContextStateGuard guard(c.Get(),isolated.Get());clearBackendBindings(c.Get());c->CSSetShader(copyBack.Get(),nullptr,0);
        c->CSSetShaderResources(0,1,outputSrv.GetAddressOf());c->CSSetUnorderedAccessViews(0,1,target.GetAddressOf(),nullptr);
        c->Dispatch((width+7)/8,(height+7)/8,1);++replacements;return true;
#else
        (void)context;return false;
#endif
    }
    void end(ID3D11DeviceContext* context){
        ContextTransaction transaction(context);
        if(!feature||!release)return;
        ComPtr<ID3D11DeviceContext1> c;check(context->QueryInterface(IID_PPV_ARGS(&c)),"end context1");
        HeatBridge::ContextStateGuard guard(c.Get(),isolated.Get());clearBackendBindings(c.Get());
        auto old=feature;feature=nullptr;ngxCheck(release(old),"release feature and FG context");
    }
};
// Resident for process lifetime: never unload a backend with live hook threads.
inline Backend& instance(){static Backend* b=new Backend;return *b;}
}
