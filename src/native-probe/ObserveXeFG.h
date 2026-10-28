#pragma once
// Original diagnostic: observes SDK handles and queries documented present status.
#include <atomic>
#include <xefg_swapchain_d3d12.h>
#include "minhook/include/MinHook.h"
namespace ObserveXeFG {
inline decltype(&xefgSwapChainD3D12CreateContext) originalCreate=nullptr;
inline decltype(&xefgSwapChainGetLastPresentStatus) getStatus=nullptr;
inline xefg_swapchain_handle_t current=nullptr;
inline unsigned generatedPresents=0,totalReportedFrames=0;
inline xefg_swapchain_result_t create(ID3D12Device* d,xefg_swapchain_handle_t* result){
 auto r=originalCreate(d,result);if(r==XEFG_SWAPCHAIN_RESULT_SUCCESS&&result)current=*result;return r;
}
inline void install(){
 HMODULE m=GetModuleHandleW(L"libxess_fg.dll");if(!m)throw std::runtime_error("XeFG observer: module not loaded");
 auto target=GetProcAddress(m,"xefgSwapChainD3D12CreateContext");
 getStatus=reinterpret_cast<decltype(getStatus)>(GetProcAddress(m,"xefgSwapChainGetLastPresentStatus"));
 if(!target||!getStatus)throw std::runtime_error("XeFG observer: missing SDK exports");
 auto init=MH_Initialize();if(init!=MH_OK&&init!=MH_ERROR_ALREADY_INITIALIZED)throw std::runtime_error("XeFG observer: MH init");
 if(MH_CreateHook(target,reinterpret_cast<void*>(&create),reinterpret_cast<void**>(&originalCreate))!=MH_OK||MH_EnableHook(target)!=MH_OK)
  throw std::runtime_error("XeFG observer: hook install");
}
inline std::string query(){
 if(!current||!getStatus)return "XEFG_STATUS noContext";
 xefg_swapchain_present_status_t s{};auto r=getStatus(current,&s);
 if(r==XEFG_SWAPCHAIN_RESULT_SUCCESS){totalReportedFrames+=s.framesPresented;if(s.framesPresented>1&&s.isFrameGenEnabled&&s.frameGenResult==XEFG_SWAPCHAIN_RESULT_SUCCESS)++generatedPresents;}
 return "XEFG_STATUS result="+std::to_string(r)+" framesPresented="+std::to_string(s.framesPresented)+" enabled="+std::to_string(s.isFrameGenEnabled)+" frameGenResult="+std::to_string(s.frameGenResult)+" generatedPresents="+std::to_string(generatedPresents);
}
}
