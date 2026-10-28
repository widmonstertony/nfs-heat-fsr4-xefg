#pragma once
#include <dxgi1_2.h>
#include <d3d12.h>
namespace ObserveFactory {
using Fn=HRESULT(STDMETHODCALLTYPE*)(IDXGIFactory2*,IUnknown*,HWND,const DXGI_SWAP_CHAIN_DESC1*,const DXGI_SWAP_CHAIN_FULLSCREEN_DESC*,IDXGIOutput*,IDXGISwapChain1**);
inline Fn original=nullptr;
inline void(*logger)(const std::string&)=nullptr;
inline std::atomic<unsigned> calls{0};
inline HRESULT STDMETHODCALLTYPE call(IDXGIFactory2* f,IUnknown* d,HWND w,const DXGI_SWAP_CHAIN_DESC1* desc,const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* full,IDXGIOutput* restrictOutput,IDXGISwapChain1** out){
 const unsigned id=++calls;
 Microsoft::WRL::ComPtr<ID3D12CommandQueue> q;bool dx12=d&&SUCCEEDED(d->QueryInterface(IID_PPV_ARGS(&q)));
 if(id<=16&&logger&&desc){
  std::ostringstream s;s<<"SC_DESC call="<<id<<" dx12="<<dx12<<" size="<<desc->Width<<'x'<<desc->Height<<" fmt="<<desc->Format<<" stereo="<<desc->Stereo<<" samples="<<desc->SampleDesc.Count<<','<<desc->SampleDesc.Quality
   <<" usage="<<desc->BufferUsage<<" count="<<desc->BufferCount<<" scaling="<<desc->Scaling<<" effect="<<desc->SwapEffect<<" alpha="<<desc->AlphaMode<<" flags="<<desc->Flags<<" restrict="<<restrictOutput<<" hwnd="<<w;
  if(full)s<<" windowed="<<full->Windowed<<" hz="<<full->RefreshRate.Numerator<<'/'<<full->RefreshRate.Denominator<<" scanline="<<full->ScanlineOrdering<<" fsScaling="<<full->Scaling;
  else s<<" fullscreenDesc=null";
  logger(s.str());
 }
 const DXGI_SWAP_CHAIN_DESC1* effective=desc;
#ifdef HEAT_FG_FIX_SWAPCHAIN_USAGE
 DXGI_SWAP_CHAIN_DESC1 normalized{};
 // Keep the game's original DX11 UAV-capable buffer unchanged. Only the
 // secondary DX12 presentation buffer must drop unsupported UAV usage.
 if(dx12&&desc&&(desc->BufferUsage&DXGI_USAGE_UNORDERED_ACCESS)){
  normalized=*desc;normalized.BufferUsage&=~DXGI_USAGE_UNORDERED_ACCESS;effective=&normalized;
  if(logger)logger("SC_DX12_USAGE_FIX before="+std::to_string(desc->BufferUsage)+" after="+std::to_string(normalized.BufferUsage)+" originalDx11Unchanged=1");
 }
#endif
 auto r=original(f,d,w,effective,full,restrictOutput,out);
 if(id<=16&&logger){std::ostringstream s;s<<"SC_RESULT call="<<id<<" hr=0x"<<std::hex<<unsigned(r);logger(s.str());}return r;
}
inline void install(HMODULE proxy,void(*log)(const std::string&)){
 auto create=reinterpret_cast<decltype(&CreateDXGIFactory1)>(GetProcAddress(proxy,"CreateDXGIFactory1"));
 if(!create)throw std::runtime_error("factory observer missing export");
 Microsoft::WRL::ComPtr<IDXGIFactory2> factory;HeatShadow::check(create(IID_PPV_ARGS(&factory)),"factory observer create");
 auto target=(*reinterpret_cast<void***>(factory.Get()))[15];logger=log;
 if(MH_CreateHook(target,reinterpret_cast<void*>(&call),reinterpret_cast<void**>(&original))!=MH_OK||MH_EnableHook(target)!=MH_OK)throw std::runtime_error("factory observer hook failed");
}
}
