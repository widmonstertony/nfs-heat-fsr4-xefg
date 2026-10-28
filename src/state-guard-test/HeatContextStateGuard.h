#pragma once

// Original diagnostic helper. Context states isolate pipeline bindings, not GPU
// resource contents, synchronization, queries, or changes made to other contexts.
// Use only on the same render thread as the real immediate context, with no
// simultaneous calls from other threads. The caller owns resource lifetime.
#include <d3d11_1.h>
#include <wrl/client.h>
#include <stdexcept>
#ifdef HEAT_FG_EXPLICIT_COMPUTE_STATE
#include "ComputeBindings.h"
#include <memory>
#endif

namespace HeatBridge {

inline HRESULT CreateIsolatedContextState(
    ID3D11Device* device, ID3DDeviceContextState** result) noexcept {
    if (!result) return E_INVALIDARG;
    *result = nullptr;
    if (!device) return E_INVALIDARG;
    Microsoft::WRL::ComPtr<ID3D11Device1> device1;
    const HRESULT qi = device->QueryInterface(IID_PPV_ARGS(&device1));
    if (FAILED(qi)) return qi;
    const D3D_FEATURE_LEVEL featureLevel = device->GetFeatureLevel();
    const UINT flags = (device->GetCreationFlags() & D3D11_CREATE_DEVICE_SINGLETHREADED)
        ? D3D11_1_CREATE_DEVICE_CONTEXT_STATE_SINGLETHREADED : 0;
    return device1->CreateDeviceContextState(
        flags, &featureLevel, 1, D3D11_SDK_VERSION,
        __uuidof(ID3D11Device1), nullptr, result);
}

class ContextStateGuard final {
public:
    ContextStateGuard(ID3D11DeviceContext1* context,
                      ID3DDeviceContextState* isolatedState)
        : context_(context) {
        if (!context || !isolatedState)
            throw std::invalid_argument("ContextStateGuard requires non-null context and state.");
        if (context->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE)
            throw std::invalid_argument("ContextStateGuard does not support deferred contexts.");
        Microsoft::WRL::ComPtr<ID3D11Device> contextDevice, stateDevice;
        Microsoft::WRL::ComPtr<IUnknown> contextIdentity, stateIdentity;
        context->GetDevice(&contextDevice);
        isolatedState->GetDevice(&stateDevice);
        if (!contextDevice || !stateDevice ||
            FAILED(contextDevice.As(&contextIdentity)) ||
            FAILED(stateDevice.As(&stateIdentity)) ||
            contextIdentity.Get() != stateIdentity.Get())
            throw std::invalid_argument("Context and isolated state must belong to the same device.");
#ifdef HEAT_FG_EXPLICIT_COMPUTE_STATE
        compute_=std::make_unique<ComputeBindings>(context);
#endif
#ifdef HEAT_FG_NO_STATE_SWAP
        active_=true;
        return;
#endif
        context_->SwapDeviceContextState(isolatedState, &previousState_);
        if (!previousState_)
            throw std::runtime_error("SwapDeviceContextState did not return previous state.");
        active_ = true;
    }

    ~ContextStateGuard() noexcept { Restore(); }
    ContextStateGuard(const ContextStateGuard&) = delete;
    ContextStateGuard& operator=(const ContextStateGuard&) = delete;
    ContextStateGuard(ContextStateGuard&&) = delete;
    ContextStateGuard& operator=(ContextStateGuard&&) = delete;

    void Restore() noexcept {
        if (!active_) return;
#ifndef HEAT_FG_NO_STATE_SWAP
        context_->SwapDeviceContextState(previousState_.Get(), nullptr);
#endif
#ifdef HEAT_FG_EXPLICIT_COMPUTE_STATE
        compute_->Restore();
#endif
        active_ = false;
    }

private:
    Microsoft::WRL::ComPtr<ID3D11DeviceContext1> context_;
    Microsoft::WRL::ComPtr<ID3DDeviceContextState> previousState_;
    bool active_ = false;
#ifdef HEAT_FG_EXPLICIT_COMPUTE_STATE
    std::unique_ptr<ComputeBindings> compute_;
#endif
};

} // namespace HeatBridge
