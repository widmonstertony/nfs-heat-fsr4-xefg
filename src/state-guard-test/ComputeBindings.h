#pragma once
#include <d3d11_1.h>
#include <array>

namespace HeatBridge {
// Explicit CS backup supplements SwapDeviceContextState for this bounded test.
// The context-state object still restores the non-compute pipeline.
class ComputeBindings final {
    ID3D11DeviceContext1* c;
    ID3D11ComputeShader* shader=nullptr;
    std::array<ID3D11ClassInstance*,256> instances{};
    UINT instanceCount=256;
    std::array<ID3D11Buffer*,14> buffers{};
    std::array<UINT,14> first{},count{};
    std::array<ID3D11ShaderResourceView*,128> srvs{};
    std::array<ID3D11UnorderedAccessView*,64> uavs{};
    std::array<ID3D11SamplerState*,16> samplers{};
    bool active=true;
    template<class T,size_t N> static void release(std::array<T*,N>& a){for(auto p:a)if(p)p->Release();}
public:
    explicit ComputeBindings(ID3D11DeviceContext1* context):c(context){
        c->CSGetShader(&shader,instances.data(),&instanceCount);
        c->CSGetConstantBuffers1(0,14,buffers.data(),first.data(),count.data());
        c->CSGetShaderResources(0,128,srvs.data());
        c->CSGetUnorderedAccessViews(0,64,uavs.data());
        c->CSGetSamplers(0,16,samplers.data());
    }
    void Restore() noexcept {
        if(!active)return;
        std::array<ID3D11ShaderResourceView*,128> emptySrv{};
        std::array<ID3D11UnorderedAccessView*,64> emptyUav{};
        c->CSSetShaderResources(0,128,emptySrv.data());
        c->CSSetUnorderedAccessViews(0,64,emptyUav.data(),nullptr);
        c->CSSetShader(shader,instances.data(),instanceCount);
        c->CSSetConstantBuffers1(0,14,buffers.data(),first.data(),count.data());
        c->CSSetShaderResources(0,128,srvs.data());
        c->CSSetUnorderedAccessViews(0,64,uavs.data(),nullptr);
        c->CSSetSamplers(0,16,samplers.data());
        active=false;
    }
    ~ComputeBindings(){Restore();if(shader)shader->Release();release(instances);release(buffers);release(srvs);release(uavs);release(samplers);}
    ComputeBindings(const ComputeBindings&)=delete;
};
}
