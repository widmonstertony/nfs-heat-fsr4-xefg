// Original, bounded diagnostic probe. No replacement shaders or game patches.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <d3d11_1.h>
#include <wrl/client.h>
#include <atomic>
#include <mutex>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <cstring>
#include <string>
#include <unordered_set>
#include <algorithm>
#ifdef HEAT_SHADER_CAPTURE
#include <bcrypt.h>
#include <unordered_map>
#include <unordered_set>
#endif
#include "minhook/include/MinHook.h"
#ifdef HEAT_SHADOW_BACKEND
#include "HeatShadowBackend.h"
#endif
#ifdef HEAT_FINAL_SR
#include "HeatFinalUpscale.h"
#endif
#ifdef HEAT_FG_GAME
#include "ObserveXeFG.h"
#ifdef HEAT_FG_FACTORY_OBSERVE
#include "ObserveFactory.h"
#endif
#endif

using Microsoft::WRL::ComPtr;
namespace {
HMODULE moduleHandle = nullptr;
std::atomic<bool> stopping{false};
std::atomic<bool> isReady{false};
#ifdef HEAT_FG_GAME
std::atomic<bool> backendStopRequested{false}, backendStopAcknowledged{false};
#endif
std::atomic<unsigned> activeCallbacks{0}, candidateCount{0};
std::atomic<unsigned long long> totalDrawCalls{0}, totalDispatchCalls{0};
std::mutex logMutex, stopMutex;
std::mutex uniqueMutex;
std::unordered_set<std::string> seenBindings;
std::ofstream logFile;
std::wstring directory;
thread_local bool insideProbe = false;
bool readConstantData = false;
constexpr unsigned maxCandidates = 256;
#ifdef HEAT_FG_ARMED_TEST
std::atomic<bool> testArmed{false};
std::atomic<ULONGLONG> testArmedAt{0};
std::atomic<unsigned> tonemapSamples{0};
const std::string targetTonemapHash="6a05f85f0a95fc9d66f82bba02e121376342da4f8d6a3e60c8cde4236fadb394";
#ifdef HEAT_FINAL_SR
const std::string targetFinalHash="154d83ae0a470897c15fb631fe4773951174cb00b37c96f9fe2fcf088f81c86b";
const std::string targetFinalNativeHash="05be27d52d01a67d99d09872100014921b9f050a25831440545150cad953d1b3";
#endif
#endif
#ifdef HEAT_SHADOW_LONG
constexpr ULONGLONG lifetimeMs = 120000;
#else
constexpr ULONGLONG lifetimeMs = 30000;
#endif
struct Hook { void* target = nullptr; void* original = nullptr; void* replacement = nullptr; };
Hook hooks[8];
unsigned hookCount = 0;
bool minHookInitialized = false;
bool hooksDisabled = false;
#ifdef HEAT_SHADER_CAPTURE
std::atomic<ULONGLONG> firstActualDrawAt{0};
ULONGLONG initializedAt = 0;
ID3D11Device* scratchDeviceIdentity = nullptr;
ComPtr<ID3D11Device> scratchKeepAlive;
std::mutex shaderMutex;
std::unordered_map<const void*, std::string> shaderHashes;
std::unordered_set<std::string> savedShaderHashes;
unsigned long long savedShaderBytes = 0;
unsigned shaderCreateCount = 0;
unsigned pixelShaderCreateCount = 0;
unsigned computeShaderCreateCount = 0;
#ifdef HEAT_SHADER_CAPTURE_V5
unsigned vertexShaderCreateCount = 0;
constexpr unsigned maxVertexShaderCreates = 8192;
std::atomic<unsigned> taaConstantReadCount{0}, velocityConstantReadCount{0};
#ifdef HEAT_DRIVING_TAA
std::string targetTaaHash = "7c20edc92b263b0c3125bde2dfb5cd3a99f20f12ac5d86778cf28c2eecf59909";
#else
std::string targetTaaHash = "dba66e8c94b02f2a7f5d2a948172304f7f0b4ba3ead18848ad595304e68a560e";
#endif
std::string targetVelocityHash = "b1a51efe2f17fb1dff0a709b3c87b5bc78c7336678c165a5ff20c20c5b7cf3c4";
void captureTargetConstants(ID3D11DeviceContext* context, bool compute) noexcept;
#endif
#ifdef HEAT_SHADER_CAPTURE_V4
#ifdef HEAT_SHADER_CAPTURE_V5
constexpr unsigned maxShaderCreates = 40960;
#else
constexpr unsigned maxShaderCreates = 32768;
#endif
constexpr unsigned maxPixelShaderCreates = 4096;
constexpr unsigned maxComputeShaderCreates = 28672;
#else
constexpr unsigned maxShaderCreates = 4096;
constexpr unsigned maxPixelShaderCreates = 4096;
constexpr unsigned maxComputeShaderCreates = 4096;
#endif
std::wstring shaderDirectory;
std::atomic<bool> initializeCalled{false};
#endif

void writeLog(const std::string& message) {
    std::lock_guard<std::mutex> lock(logMutex);
    if (logFile) { logFile << message << '\n'; logFile.flush(); }
}

void describeResource(std::ostream& out, const char* label, unsigned slot,
                      ID3D11Resource* resource, DXGI_FORMAT viewFormat) {
    if (!resource) return;
    D3D11_RESOURCE_DIMENSION dimension{};
    resource->GetType(&dimension);
    out << "  " << label << '[' << slot << "] resource=" << resource
        << " viewFormat=" << static_cast<unsigned>(viewFormat);
    ComPtr<ID3D11Texture2D> texture;
    if (SUCCEEDED(resource->QueryInterface(IID_PPV_ARGS(&texture)))) {
        D3D11_TEXTURE2D_DESC d{}; texture->GetDesc(&d);
        out << " Tex2D=" << d.Width << 'x' << d.Height
            << " format=" << static_cast<unsigned>(d.Format)
            << " mips=" << d.MipLevels << " array=" << d.ArraySize
            << " samples=" << d.SampleDesc.Count << " bind=0x" << std::hex
            << d.BindFlags << std::dec;
    } else {
        ComPtr<ID3D11Buffer> buffer;
        if (SUCCEEDED(resource->QueryInterface(IID_PPV_ARGS(&buffer)))) {
            D3D11_BUFFER_DESC d{}; buffer->GetDesc(&d);
            out << " Buffer=" << d.ByteWidth << " stride=" << d.StructureByteStride;
        } else out << " dimension=" << static_cast<unsigned>(dimension);
    }
    char name[256]{}; UINT nameSize = sizeof(name) - 1;
    if (SUCCEEDED(resource->GetPrivateData(WKPDID_D3DDebugObjectName, &nameSize, name)))
        out << " debugName=" << name;
    out << '\n';
}

template<typename View, typename Desc>
void describeView(std::ostream& out, const char* label, unsigned slot, View* view) {
    if (!view) return;
    ComPtr<ID3D11Resource> resource; view->GetResource(&resource);
    Desc desc{}; view->GetDesc(&desc);
    describeResource(out, label, slot, resource.Get(), desc.Format);
}

void describeConstants(std::ostream& out, ID3D11DeviceContext* context,
                       ID3D11Device* device, bool compute, unsigned eventId) {
    ID3D11Buffer* buffers[8]{};
    UINT firstConstant[8]{}, constantCount[8]{};
    ComPtr<ID3D11DeviceContext1> context1;
    const bool hasRanges = SUCCEEDED(context->QueryInterface(IID_PPV_ARGS(&context1)));
    if (hasRanges) {
        if (compute) context1->CSGetConstantBuffers1(0, 8, buffers, firstConstant, constantCount);
        else context1->PSGetConstantBuffers1(0, 8, buffers, firstConstant, constantCount);
    } else {
        if (compute) context->CSGetConstantBuffers(0, 8, buffers);
        else context->PSGetConstantBuffers(0, 8, buffers);
    }
    for (unsigned i = 0; i != 8; ++i) {
        ComPtr<ID3D11Buffer> buffer; buffer.Attach(buffers[i]);
        if (!buffer) continue;
        D3D11_BUFFER_DESC d{}; buffer->GetDesc(&d);
        out << "  " << (compute ? "CS" : "PS") << "CB[" << i << "] object="
            << buffer.Get() << " bytes=" << d.ByteWidth;
        const UINT offset = hasRanges ? firstConstant[i] * 16 : 0;
        const UINT range = hasRanges ? constantCount[i] * 16 : d.ByteWidth;
        out << " firstConstant=" << firstConstant[i] << " numConstants=" << constantCount[i]
            << " offsetBytes=" << offset << " rangeBytes=" << range
            << " context1Ranges=" << hasRanges << '\n';
        // Optional, deliberately tiny readback: at most first 4 candidates,
        // first 2 CB slots, 1 KiB each. Disabled in normal probe runs.
        if (!readConstantData || eventId > 4 || i > 1 || !range || offset >= d.ByteWidth) continue;
        const UINT captureBytes = std::min(1024u, std::min(range, d.ByteWidth - offset));
        D3D11_BUFFER_DESC stagingDesc{};
        stagingDesc.ByteWidth = captureBytes;
        stagingDesc.Usage = D3D11_USAGE_STAGING;
        stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Buffer> staging;
        if (FAILED(device->CreateBuffer(&stagingDesc, nullptr, &staging))) continue;
        D3D11_BOX box{offset, 0, 0, offset + captureBytes, 1, 1};
        context->CopySubresourceRegion(staging.Get(), 0, 0, 0, 0, buffer.Get(), 0, &box);
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) continue;
        const auto* bytes = static_cast<const unsigned char*>(mapped.pData);
        out << "    cbHex=";
        for (unsigned j = 0; j < captureBytes; ++j)
            out << std::hex << std::setw(2) << std::setfill('0') << unsigned(bytes[j]);
        out << std::dec << "\n    float4Candidates=";
        for (unsigned j = 0; j + 16 <= captureBytes; j += 16) {
            float values[4]; std::memcpy(values, bytes + j, sizeof(values));
            bool finite = true, nonzero = false;
            for (float value : values) {
                finite = finite && std::isfinite(value) && std::abs(value) < 1e7f;
                nonzero = nonzero || std::abs(value) > 1e-8f;
            }
            if (finite && nonzero) out << " @" << j << '(' << values[0] << ','
                << values[1] << ',' << values[2] << ',' << values[3] << ')';
        }
        context->Unmap(staging.Get(), 0);
        out << "\n    NOTE: float4 candidates are not validated camera matrices/jitter.\n";
    }
}

void snapshot(ID3D11DeviceContext* context, const char* operation, bool compute,
              unsigned x, unsigned y, unsigned z) noexcept {
#ifdef HEAT_PUBLIC_BUILD
    return; // Do not dump game bindings, shaders or constant buffers.
#endif
    if (!isReady.load() || stopping.load() || insideProbe) return;
#ifdef HEAT_CAPTURE_ACTUAL_SCENE
    if(!testArmed.load()||(compute&&(x<32||y<16)))return;
#endif
#ifdef HEAT_SHADER_CAPTURE
#ifdef HEAT_SHADER_CAPTURE_V5
    if (!firstActualDrawAt.load()) return;
#else
    if (!firstActualDrawAt.load() || candidateCount.load() >= maxCandidates) return;
#endif
#endif
    insideProbe = true;
    try {
#ifdef HEAT_SHADER_CAPTURE_V5
        // Deliberately before resource dedup: collect up to16 ACTUAL uses,
        // not just the first unique state. Only the two exact target hashes.
        captureTargetConstants(context, compute);
        if (candidateCount.load() >= maxCandidates) { insideProbe = false; return; }
#endif
        std::ostringstream key;
        key << context << ':' << operation;
        if (compute) {
            ComPtr<ID3D11ComputeShader> shader; context->CSGetShader(&shader, nullptr, nullptr);
            ComPtr<ID3D11UnorderedAccessView> output; context->CSGetUnorderedAccessViews(0, 1, &output);
            key << ':' << shader.Get() << ':' << output.Get();
        } else {
            ComPtr<ID3D11PixelShader> shader; context->PSGetShader(&shader, nullptr, nullptr);
            ComPtr<ID3D11RenderTargetView> output; context->OMGetRenderTargets(1, &output, nullptr);
            key << ':' << shader.Get() << ':' << output.Get();
        }
        {
            std::lock_guard<std::mutex> lock(uniqueMutex);
            if (!seenBindings.insert(key.str()).second) { insideProbe = false; return; }
        }
        const unsigned eventId = candidateCount.fetch_add(1) + 1;
        if (eventId <= maxCandidates) {
            ComPtr<ID3D11Device> device; context->GetDevice(&device);
            std::ostringstream out;
            out << "EVENT " << eventId << ' ' << operation << " args=" << x << ','
                << y << ',' << z << " context=" << context << " device=" << device.Get()
                << " thread=" << GetCurrentThreadId() << '\n';
            ID3D11ShaderResourceView* srvs[32]{};
            if (compute) {
                ID3D11ComputeShader* shader = nullptr;
                context->CSGetShader(&shader, nullptr, nullptr);
                out << "  CS=" << shader;
#ifdef HEAT_SHADER_CAPTURE
                { std::lock_guard<std::mutex> lock(shaderMutex);
                  auto found = shaderHashes.find(shader);
                  if (found != shaderHashes.end()) out << " sha256=" << found->second; }
#endif
                out << '\n'; if (shader) shader->Release();
                context->CSGetShaderResources(0, 32, srvs);
                ID3D11UnorderedAccessView* uavs[8]{};
                context->CSGetUnorderedAccessViews(0, 8, uavs);
                for (unsigned i = 0; i < 8; ++i) {
                    ComPtr<ID3D11UnorderedAccessView> view; view.Attach(uavs[i]);
                    describeView<ID3D11UnorderedAccessView, D3D11_UNORDERED_ACCESS_VIEW_DESC>
                        (out, "CSUAV", i, view.Get());
                }
            } else {
#ifdef HEAT_SHADER_CAPTURE_V5
                ID3D11VertexShader* vertexShader = nullptr;
                context->VSGetShader(&vertexShader, nullptr, nullptr);
                out << "  VS=" << vertexShader;
                { std::lock_guard<std::mutex> lock(shaderMutex);
                  auto found = shaderHashes.find(vertexShader);
                  if (found != shaderHashes.end()) out << " sha256=" << found->second; }
                out << '\n'; if (vertexShader) vertexShader->Release();
#endif
                ID3D11PixelShader* shader = nullptr;
                context->PSGetShader(&shader, nullptr, nullptr);
                out << "  PS=" << shader;
#ifdef HEAT_SHADER_CAPTURE
                { std::lock_guard<std::mutex> lock(shaderMutex);
                  auto found = shaderHashes.find(shader);
                  if (found != shaderHashes.end()) out << " sha256=" << found->second; }
#endif
                out << '\n'; if (shader) shader->Release();
                context->PSGetShaderResources(0, 32, srvs);
                ID3D11RenderTargetView* rtvs[8]{}; ID3D11DepthStencilView* dsv = nullptr;
                context->OMGetRenderTargets(8, rtvs, &dsv);
                for (unsigned i = 0; i < 8; ++i) {
                    ComPtr<ID3D11RenderTargetView> view; view.Attach(rtvs[i]);
                    describeView<ID3D11RenderTargetView, D3D11_RENDER_TARGET_VIEW_DESC>
                        (out, "RTV", i, view.Get());
                }
                ComPtr<ID3D11DepthStencilView> depth; depth.Attach(dsv);
                describeView<ID3D11DepthStencilView, D3D11_DEPTH_STENCIL_VIEW_DESC>
                    (out, "DSV", 0, depth.Get());
                D3D11_VIEWPORT viewports[16]{}; UINT count = 16;
                context->RSGetViewports(&count, viewports);
                if (count) out << "  viewport=" << viewports[0].Width << 'x'
                    << viewports[0].Height << '\n';
            }
            for (unsigned i = 0; i < 32; ++i) {
                ComPtr<ID3D11ShaderResourceView> view; view.Attach(srvs[i]);
                describeView<ID3D11ShaderResourceView, D3D11_SHADER_RESOURCE_VIEW_DESC>
                    (out, compute ? "CSSRV" : "PSSRV", i, view.Get());
            }
            describeConstants(out, context, device.Get(), compute, eventId);
            writeLog(out.str());
        }
#ifndef HEAT_SHADER_CAPTURE
        if (eventId >= maxCandidates) stopping.store(true);
#endif
    } catch (...) { stopping.store(true); }
    insideProbe = false;
}

struct CallbackGuard {
    CallbackGuard() { activeCallbacks.fetch_add(1); }
    ~CallbackGuard() { activeCallbacks.fetch_sub(1); }
};
#ifdef HEAT_SHADER_CAPTURE
// SHA-256 is computed from the exact valid DXBC passed by the application.
// No signatures, shader bytes, flags, or game decisions are modified.
void captureShader(ID3D11Device* device, const char* stage, const void* code,
                   SIZE_T length, const void* shader) noexcept {
    if (stopping.load() || !isReady.load() || !shader || !code || !length ||
        length > 4u * 1024 * 1024 || device == scratchDeviceIdentity) return;
    try {
        std::lock_guard<std::mutex> lock(shaderMutex);
        unsigned* stageCount = stage[0] == 'C' ? &computeShaderCreateCount : &pixelShaderCreateCount;
        unsigned stageLimit = stage[0] == 'C' ? maxComputeShaderCreates : maxPixelShaderCreates;
#ifdef HEAT_SHADER_CAPTURE_V5
        if (stage[0] == 'V') { stageCount = &vertexShaderCreateCount; stageLimit = maxVertexShaderCreates; }
#endif
        if (shaderCreateCount >= maxShaderCreates || *stageCount >= stageLimit) return;
        ++shaderCreateCount;
        ++*stageCount;
        if (length < 4 || std::memcmp(code, "DXBC", 4)) {
            writeLog(std::string("SHADER_SKIP not_DXBC stage=") + stage); return;
        }
        unsigned char digest[32]{};
        NTSTATUS result = BCryptHash(BCRYPT_SHA256_ALG_HANDLE, nullptr, 0,
            static_cast<PUCHAR>(const_cast<void*>(code)), static_cast<ULONG>(length),
            digest, sizeof(digest));
        if (result < 0) { writeLog("SHADER_SKIP SHA256_failed"); return; }
        std::ostringstream hex;
        for (unsigned char b : digest) hex << std::hex << std::setw(2) << std::setfill('0') << unsigned(b);
        const std::string hash = hex.str();
#if defined(HEAT_SHADOW_LONG) && !defined(HEAT_CAPTURE_ACTUAL_SCENE)
        // The diagnostic target is established; do not dump unrelated shaders.
        if(hash!=targetTaaHash
#ifdef HEAT_FG_ARMED_TEST
           &&hash!=targetTonemapHash
#ifdef HEAT_FINAL_SR
           &&hash!=targetFinalHash&&hash!=targetFinalNativeHash
#endif
#endif
        ) {shaderHashes.erase(shader);return;}
#endif
        const std::string filename = std::string(stage) + "_" + hash + ".dxbc";
#ifdef HEAT_PUBLIC_BUILD
        shaderHashes[shader]=hash;
        return; // Hash identification only; never export game bytecode.
#endif
        bool saved = savedShaderHashes.count(hash) != 0;
        if (!saved && savedShaderBytes + length <= 128ull * 1024 * 1024) {
            std::wstring wideName(filename.begin(), filename.end());
            std::ofstream file(shaderDirectory + wideName, std::ios::binary | std::ios::out | std::ios::trunc);
            if (file) {
                file.write(static_cast<const char*>(code), static_cast<std::streamsize>(length));
                file.close();
                if (file) { savedShaderHashes.insert(hash); savedShaderBytes += length; saved = true; }
            }
        }
        shaderHashes[shader] = hash;
        std::ostringstream line;
        line << "SHADER stage=" << stage << " object=" << shader << " device=" << device
            << " bytes=" << length << " sha256=" << hash << " saved=" << saved
            << " file=" << filename;
        writeLog(line.str());
    } catch (...) { writeLog("SHADER_SKIP diagnostic_exception"); }
}
using CreatePixelShaderFn = HRESULT (STDMETHODCALLTYPE*)(ID3D11Device*, const void*, SIZE_T, ID3D11ClassLinkage*, ID3D11PixelShader**);
using CreateComputeShaderFn = HRESULT (STDMETHODCALLTYPE*)(ID3D11Device*, const void*, SIZE_T, ID3D11ClassLinkage*, ID3D11ComputeShader**);
HRESULT STDMETHODCALLTYPE onCreatePixelShader(ID3D11Device* d, const void* bytes, SIZE_T size,
    ID3D11ClassLinkage* linkage, ID3D11PixelShader** shader) {
    CallbackGuard guard;
    const HRESULT result = reinterpret_cast<CreatePixelShaderFn>(hooks[5].original)(d, bytes, size, linkage, shader);
    if (SUCCEEDED(result) && shader) captureShader(d, "PS", bytes, size, *shader);
    return result;
}
HRESULT STDMETHODCALLTYPE onCreateComputeShader(ID3D11Device* d, const void* bytes, SIZE_T size,
    ID3D11ClassLinkage* linkage, ID3D11ComputeShader** shader) {
    CallbackGuard guard;
    const HRESULT result = reinterpret_cast<CreateComputeShaderFn>(hooks[6].original)(d, bytes, size, linkage, shader);
    if (SUCCEEDED(result) && shader) captureShader(d, "CS", bytes, size, *shader);
    return result;
}
#ifdef HEAT_SHADER_CAPTURE_V5
using CreateVertexShaderFn = HRESULT (STDMETHODCALLTYPE*)(ID3D11Device*, const void*, SIZE_T, ID3D11ClassLinkage*, ID3D11VertexShader**);
HRESULT STDMETHODCALLTYPE onCreateVertexShader(ID3D11Device* d, const void* bytes, SIZE_T size,
    ID3D11ClassLinkage* linkage, ID3D11VertexShader** shader) {
    CallbackGuard guard;
    const HRESULT result = reinterpret_cast<CreateVertexShaderFn>(hooks[7].original)(d, bytes, size, linkage, shader);
    if (SUCCEEDED(result) && shader) captureShader(d, "VS", bytes, size, *shader);
    return result;
}

void captureTargetConstants(ID3D11DeviceContext* context, bool compute) noexcept {
#ifdef HEAT_PUBLIC_BUILD
    return;
#endif
    try {
        ComPtr<ID3D11ComputeShader> cs; ComPtr<ID3D11PixelShader> ps;
        const void* object = nullptr;
        if (compute) { context->CSGetShader(&cs, nullptr, nullptr); object = cs.Get(); }
        else { context->PSGetShader(&ps, nullptr, nullptr); object = ps.Get(); }
        {
            std::lock_guard<std::mutex> lock(shaderMutex);
            const auto found = shaderHashes.find(object);
            if (found == shaderHashes.end() || found->second != (compute ? targetTaaHash : targetVelocityHash)) return;
        }
        auto& counter = compute ? taaConstantReadCount : velocityConstantReadCount;
        if (counter.load() >= 16) return;
        const unsigned index = counter.fetch_add(1) + 1;
        if (index > 16) return;
        ComPtr<ID3D11DeviceContext1> context1;
        if (FAILED(context->QueryInterface(IID_PPV_ARGS(&context1)))) {
            writeLog("TARGET_CB_SKIP no_context1_ranges"); return;
        }
        ComPtr<ID3D11Buffer> source; UINT first = 0, count = 0;
        if (compute) context1->CSGetConstantBuffers1(0,1,&source,&first,&count);
        else context1->PSGetConstantBuffers1(0,1,&source,&first,&count);
        if (!source || !count) { writeLog("TARGET_CB_SKIP no_CB0"); return; }
        D3D11_BUFFER_DESC sourceDesc{}; source->GetDesc(&sourceDesc);
        const UINT offset = first * 16, range = count * 16;
        if (offset >= sourceDesc.ByteWidth) { writeLog("TARGET_CB_SKIP bad_offset"); return; }
        const UINT available = std::min(range, sourceDesc.ByteWidth - offset);
        const UINT bytes = compute ? 512 : available;
        if (bytes > available || bytes > 4096 || !bytes) { writeLog("TARGET_CB_SKIP range_not_supported"); return; }
        ComPtr<ID3D11Device> device; context->GetDevice(&device);
        D3D11_BUFFER_DESC desc{}; desc.ByteWidth = bytes;
        desc.Usage = D3D11_USAGE_STAGING; desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Buffer> staging;
        if (FAILED(device->CreateBuffer(&desc,nullptr,&staging))) { writeLog("TARGET_CB_SKIP staging_failed"); return; }
        D3D11_BOX box{offset,0,0,offset+bytes,1,1};
        context->CopySubresourceRegion(staging.Get(),0,0,0,0,source.Get(),0,&box);
        D3D11_MAPPED_SUBRESOURCE mapped{};
        const HRESULT result = context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped);
        if (FAILED(result)) { writeLog("TARGET_CB_SKIP Map_failed HRESULT="+std::to_string(result)); return; }
        const std::wstring name = std::wstring(compute ? L"TAA_CS_CB0_" : L"Velocity_PS_CB0_") + std::to_wstring(index) + L".bin";
        std::ofstream file(shaderDirectory+name,std::ios::binary|std::ios::out|std::ios::trunc);
        if (file) file.write(static_cast<const char*>(mapped.pData),bytes);
        context->Unmap(staging.Get(),0);
        std::ostringstream line;
        line << "TARGET_CB stage=" << (compute ? "CS" : "PS") << " sample=" << index
            << " source=" << source.Get() << " sourceBytes=" << sourceDesc.ByteWidth
            << " firstConstant=" << first << " numConstants=" << count
            << " offsetBytes=" << offset << " boundRangeBytes=" << range
            << " capturedBytes=" << bytes << " saved=" << bool(file)
            << " file=" << (compute ? "TAA_CS_CB0_" : "Velocity_PS_CB0_") << index << ".bin";
        if (!compute) {
            ComPtr<ID3D11VertexShader> vs; context->VSGetShader(&vs,nullptr,nullptr);
            line << " VS=" << vs.Get();
            std::lock_guard<std::mutex> lock(shaderMutex);
            auto found=shaderHashes.find(vs.Get());
            if(found!=shaderHashes.end()) line << " VSsha256=" << found->second;
        }
        writeLog(line.str());
        if (!compute) {
            // Read only target-pass VS CB0. Slots1/2 are metadata-only.
            ID3D11Buffer* vsBuffers[3]{}; UINT vsFirst[3]{}, vsCount[3]{};
            context1->VSGetConstantBuffers1(0,3,vsBuffers,vsFirst,vsCount);
            for (unsigned slot=0;slot<3;++slot) {
                ComPtr<ID3D11Buffer> vsBuffer; vsBuffer.Attach(vsBuffers[slot]);
                if (!vsBuffer) continue;
                D3D11_BUFFER_DESC sourceVsDesc{}; vsBuffer->GetDesc(&sourceVsDesc);
                const UINT vsOffset=vsFirst[slot]*16, vsRange=vsCount[slot]*16;
                std::ostringstream meta;
                meta << "TARGET_VS_CB slot=" << slot << " sample=" << index
                    << " object=" << vsBuffer.Get() << " sourceBytes=" << sourceVsDesc.ByteWidth
                    << " firstConstant=" << vsFirst[slot] << " numConstants=" << vsCount[slot]
                    << " offsetBytes=" << vsOffset << " rangeBytes=" << vsRange;
                if (slot==0 && vsRange && vsRange<=4096 && vsOffset<sourceVsDesc.ByteWidth &&
                    vsRange<=sourceVsDesc.ByteWidth-vsOffset) {
                    D3D11_BUFFER_DESC stagingVsDesc{}; stagingVsDesc.ByteWidth=vsRange;
                    stagingVsDesc.Usage=D3D11_USAGE_STAGING;
                    stagingVsDesc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
                    ComPtr<ID3D11Buffer> vsStaging;
                    if (SUCCEEDED(device->CreateBuffer(&stagingVsDesc,nullptr,&vsStaging))) {
                        D3D11_BOX vsBox{vsOffset,0,0,vsOffset+vsRange,1,1};
                        context->CopySubresourceRegion(vsStaging.Get(),0,0,0,0,vsBuffer.Get(),0,&vsBox);
                        D3D11_MAPPED_SUBRESOURCE vsMapped{};
                        if (SUCCEEDED(context->Map(vsStaging.Get(),0,D3D11_MAP_READ,0,&vsMapped))) {
                            const std::wstring vsName=L"Velocity_VS_CB0_"+std::to_wstring(index)+L".bin";
                            std::ofstream vsFile(shaderDirectory+vsName,std::ios::binary|std::ios::out|std::ios::trunc);
                            if(vsFile) vsFile.write(static_cast<const char*>(vsMapped.pData),vsRange);
                            context->Unmap(vsStaging.Get(),0);
                            meta << " capturedBytes=" << vsRange << " saved=" << bool(vsFile)
                                << " file=Velocity_VS_CB0_" << index << ".bin";
                        } else meta << " readback=Map_failed";
                    } else meta << " readback=CreateBuffer_failed";
                } else meta << " metadata_only=1";
                writeLog(meta.str());
            }
        }
    } catch (...) { writeLog("TARGET_CB_SKIP diagnostic_exception"); }
}
#endif
void noteActualDraw(ID3D11DeviceContext* context) {
    if (!isReady.load() || stopping.load() || firstActualDrawAt.load()) return;
    ComPtr<ID3D11Device> device; context->GetDevice(&device);
    if (device.Get() == scratchDeviceIdentity) return;
    ULONGLONG unset = 0;
    if (firstActualDrawAt.compare_exchange_strong(unset, GetTickCount64()))
        writeLog("FIRST_ACTUAL_DRAW start_bounded_timer_ms="+std::to_string(lifetimeMs));
}
#define NOTE_ACTUAL_DRAW(c) noteActualDraw(c)
#else
#define NOTE_ACTUAL_DRAW(c) ((void)0)
#endif
using DrawFn = void (STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT);
using DrawIndexedFn = void (STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT, INT);
using DrawInstancedFn = void (STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT, UINT, UINT);
using DrawIndexedInstancedFn = void (STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT, UINT, INT, UINT);
using DispatchFn = void (STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT, UINT);

void STDMETHODCALLTYPE onDraw(ID3D11DeviceContext* c, UINT n, UINT first) {
    CallbackGuard guard; ++totalDrawCalls;
    NOTE_ACTUAL_DRAW(c);
    if (n <= 6) snapshot(c, "Draw", false, n, first, 0);
#ifdef HEAT_FINAL_SR
    if(n==3&&testArmed.load()&&!insideProbe&&!stopping.load()&&!backendStopRequested.load()&&!HeatShadow::instance().failed&&HeatFinal::instance().pending){
        ComPtr<ID3D11PixelShader> ps;c->PSGetShader(&ps,nullptr,nullptr);bool target=false;
        {std::lock_guard<std::mutex> lock(shaderMutex);auto i=shaderHashes.find(ps.Get());target=i!=shaderHashes.end()&&(i->second==targetFinalHash||i->second==targetFinalNativeHash);}
        if(target){insideProbe=true;bool drawn=false;
            try{ComPtr<ID3D11DeviceContext1> c1;HeatShadow::check(c->QueryInterface(IID_PPV_ARGS(&c1)),"SR context1");
                auto runtime=(std::filesystem::path(directory).parent_path().parent_path()/L"backend-probe"/L"fg-runtime").wstring();
                auto result=HeatFinal::instance().compose(c1.Get(),runtime,[&](){reinterpret_cast<DrawFn>(hooks[0].original)(c,n,first);drawn=true;});
                if(!result.empty())writeLog(result);
                if(HeatShadow::instance().frames%30==0)writeLog(ObserveXeFG::query());
            }catch(const std::exception& e){HeatShadow::instance().failed=true;writeLog(std::string("FINAL_SR_FAIL ")+e.what());}
            insideProbe=false;if(drawn)return;
        }
    }
#endif
#ifdef HEAT_FG_ARMED_TEST
    if(testArmed.load()&&tonemapSamples.load()<3&&!insideProbe){
        ComPtr<ID3D11PixelShader> ps;c->PSGetShader(&ps,nullptr,nullptr);bool target=false;
        {std::lock_guard<std::mutex> lock(shaderMutex);auto i=shaderHashes.find(ps.Get());target=i!=shaderHashes.end()&&i->second==targetTonemapHash;}
        if(target){++tonemapSamples;ComPtr<ID3D11ShaderResourceView> input;ComPtr<ID3D11RenderTargetView> output;
            c->PSGetShaderResources(0,1,&input);c->OMGetRenderTargets(1,&output,nullptr);
            ComPtr<ID3D11Resource> a,b;ComPtr<ID3D11Texture2D> at,bt;D3D11_TEXTURE2D_DESC ad{},bd{};
            if(input&&output){input->GetResource(&a);output->GetResource(&b);if(SUCCEEDED(a.As(&at))&&SUCCEEDED(b.As(&bt))){at->GetDesc(&ad);bt->GetDesc(&bd);
                writeLog("TONEMAP_DIMS input="+std::to_string(ad.Width)+"x"+std::to_string(ad.Height)+" output="+std::to_string(bd.Width)+"x"+std::to_string(bd.Height));}}
        }
    }
#endif
    reinterpret_cast<DrawFn>(hooks[0].original)(c, n, first);
}
void STDMETHODCALLTYPE onDrawIndexed(ID3D11DeviceContext* c, UINT n, UINT first, INT base) {
    CallbackGuard guard; ++totalDrawCalls;
    NOTE_ACTUAL_DRAW(c);
    if (n <= 6) snapshot(c, "DrawIndexed", false, n, first, unsigned(base));
    reinterpret_cast<DrawIndexedFn>(hooks[1].original)(c, n, first, base);
}
void STDMETHODCALLTYPE onDrawInstanced(ID3D11DeviceContext* c, UINT n, UINT instances, UINT first, UINT firstInstance) {
    CallbackGuard guard; ++totalDrawCalls;
    NOTE_ACTUAL_DRAW(c);
    if (n <= 6 && instances <= 2) snapshot(c, "DrawInstanced", false, n, instances, first);
    reinterpret_cast<DrawInstancedFn>(hooks[2].original)(c, n, instances, first, firstInstance);
}
void STDMETHODCALLTYPE onDrawIndexedInstanced(ID3D11DeviceContext* c, UINT n, UINT instances, UINT first, INT base, UINT firstInstance) {
    CallbackGuard guard; ++totalDrawCalls;
    NOTE_ACTUAL_DRAW(c);
    if (n <= 6 && instances <= 2) snapshot(c, "DrawIndexedInstanced", false, n, instances, first);
    reinterpret_cast<DrawIndexedInstancedFn>(hooks[3].original)(c, n, instances, first, base, firstInstance);
}
void STDMETHODCALLTYPE onDispatch(ID3D11DeviceContext* c, UINT x, UINT y, UINT z) {
    CallbackGuard guard; ++totalDispatchCalls;
    snapshot(c, "Dispatch", true, x, y, z);
#ifdef HEAT_FG_ARMED_TEST
#ifdef HEAT_PLAY_SESSION
    // Session build auto-arms only on the verified driving main-color TAA.
    // F9 is a one-way emergency off; restart the game to enable again.
    if(!insideProbe&&(GetAsyncKeyState(VK_F9)&0x8000)){
        DWORD pid=0;GetWindowThreadProcessId(GetForegroundWindow(),&pid);
        if(pid==GetCurrentProcessId())backendStopRequested.store(true);
    }
    if(!testArmed.load()&&!backendStopRequested.load()&&!insideProbe){
        ComPtr<ID3D11ComputeShader> shader;c->CSGetShader(&shader,nullptr,nullptr);bool driving=false;
        {std::lock_guard<std::mutex> lock(shaderMutex);auto i=shaderHashes.find(shader.Get());driving=i!=shaderHashes.end()&&i->second==targetTaaHash;}
        if(driving){testArmedAt.store(GetTickCount64());testArmed.store(true);writeLog("SESSION_AUTO_ARM verified driving TAA; no two-minute expiry; F9 emergency off");}
    }
#else
    if(!testArmed.load()&&(GetAsyncKeyState(VK_F8)&0x8000)){
        DWORD pid=0;GetWindowThreadProcessId(GetForegroundWindow(),&pid);
        if(pid==GetCurrentProcessId()){testArmedAt.store(GetTickCount64());testArmed.store(true);writeLog("ARMED F8 bounded real-scene test");}
    }
#endif
#endif
#ifdef HEAT_SHADOW_BACKEND
    auto& backend=HeatShadow::instance();
    bool displayThisPass=false;
#ifdef HEAT_DISPLAY_TEST
#ifdef HEAT_BACKEND_FRAME_LIMIT
    constexpr unsigned backendFrames=HEAT_BACKEND_FRAME_LIMIT;
#else
    constexpr unsigned backendFrames=120;
#endif
#else
    constexpr unsigned backendFrames=3;
#endif
    if(isReady.load()&&!stopping.load()&&!insideProbe&&!backend.failed&&backend.frames<backendFrames
#ifdef HEAT_FG_GAME
       &&!backendStopRequested.load()
#endif
#if defined(HEAT_CAPTURE_ACTUAL_SCENE) && !defined(HEAT_ENABLE_ACTUAL_CAPTURE_BACKEND)
       &&false
#endif
    ) {
#ifdef HEAT_FG_ARMED_TEST
        if(!testArmed.load()){reinterpret_cast<DispatchFn>(hooks[4].original)(c,x,y,z);return;}
#endif
        ComPtr<ID3D11ComputeShader> cs;c->CSGetShader(&cs,nullptr,nullptr);
        bool target=false;
        { std::lock_guard<std::mutex> lock(shaderMutex);auto i=shaderHashes.find(cs.Get());
          target=i!=shaderHashes.end()&&i->second==targetTaaHash; }
        if(target) {
            if(!backend.firstTarget)backend.firstTarget=GetTickCount64();
#ifdef HEAT_SHADOW_DELAYED
            if(GetTickCount64()-backend.firstTarget>=10000) {
#endif
            insideProbe=true;
            try {
#ifdef HEAT_FG_GAME
                if(backend.frames>0){auto status=ObserveXeFG::query();if(backend.frames%15==0)writeLog(status);}
                const wchar_t* runtimeName=L"fg-runtime";
#else
                const wchar_t* runtimeName=L"runtime";
#endif
#ifdef HEAT_FINAL_SR
                ComPtr<ID3D11DeviceContext1> c1;HeatShadow::check(c->QueryInterface(IID_PPV_ARGS(&c1)),"SR capture context1");
                HeatFinal::instance().capture(c1.Get());displayThisPass=true;
#else
                auto result=backend.run(c,(std::filesystem::path(directory).parent_path().parent_path()/L"backend-probe"/runtimeName).wstring());if(!result.empty())writeLog(result);displayThisPass=true;
#endif
            }
            catch(const std::exception& e){backend.failed=true;writeLog(std::string("SHADOW_FAIL ")+e.what());}
            catch(...){backend.failed=true;writeLog("SHADOW_FAIL unknown C++ exception");}
            insideProbe=false;
#ifdef HEAT_SHADOW_DELAYED
            }
#endif
        }
    }
#endif
    reinterpret_cast<DispatchFn>(hooks[4].original)(c, x, y, z);
#ifdef HEAT_DISPLAY_TEST
    if(displayThisPass&&!insideProbe){insideProbe=true;
        try{
#ifdef HEAT_FINAL_SR
            ComPtr<ID3D11DeviceContext1> c1;HeatShadow::check(c->QueryInterface(IID_PPV_ARGS(&c1)),"SR raw context1");HeatFinal::instance().rawAfterTaa(c1.Get());
#else
            if(backend.display(c)&&(backend.replacements==1||backend.replacements%30==0))writeLog("DISPLAY_REPLACED count="+std::to_string(backend.replacements)+" originalTaaHistoryPreserved=1");
#endif
        }
        catch(const std::exception& e){backend.failed=true;writeLog(std::string("DISPLAY_FAIL ")+e.what());}
        insideProbe=false;
    }
#ifdef HEAT_FG_GAME
    if(!insideProbe&&backend.feature&&(backend.frames>=backendFrames||backend.failed||backendStopRequested.load())){
        insideProbe=true;
        try{writeLog("FG_TEST_END "+ObserveXeFG::query());backend.end(c);writeLog("FG_BACKEND_RELEASED normal_game_rendering_resumed=1");}
        catch(const std::exception& e){writeLog(std::string("FG_END_FAIL ")+e.what());}
        insideProbe=false;
    }
    if(!insideProbe&&backendStopRequested.load()&&!backend.feature)backendStopAcknowledged.store(true);
#endif
#endif
}

DWORD stopAndRestore() {
    std::lock_guard<std::mutex> lock(stopMutex);
    stopping.store(true);
    isReady.store(false);
    if (hooksDisabled) return activeCallbacks.load() ? 1 : 0;
    for (unsigned i = 0; i < hookCount; ++i) {
        Hook& hook = hooks[i];
        if (!hook.target) continue;
        const MH_STATUS result = MH_DisableHook(hook.target);
        writeLog(std::string("UNHOOK ") + std::to_string(i) + " " + MH_StatusToString(result));
    }
    const ULONGLONG deadline = GetTickCount64() + 2000;
    while (activeCallbacks.load() && GetTickCount64() < deadline) Sleep(1);
    writeLog("STOP draws=" + std::to_string(totalDrawCalls.load()) + " dispatches="
        + std::to_string(totalDispatchCalls.load()) + " candidateAttempts="
        + std::to_string(candidateCount.load()) + " active=" + std::to_string(activeCallbacks.load()));
#ifdef HEAT_SHADER_CAPTURE
    { std::lock_guard<std::mutex> shaderLock(shaderMutex);
      writeLog("SHADER_TOTAL creates=" + std::to_string(shaderCreateCount) +
          " PS=" + std::to_string(pixelShaderCreateCount) + " CS=" + std::to_string(computeShaderCreateCount) +
#ifdef HEAT_SHADER_CAPTURE_V5
          " VS=" + std::to_string(vertexShaderCreateCount) +
#endif
          " unique=" + std::to_string(savedShaderHashes.size()) + " bytes=" + std::to_string(savedShaderBytes)); }
#endif
    // Keep disabled trampolines and pinned DLL alive for process lifetime;
    // this avoids freeing code from callbacks racing an unhook. Do not unload.
    hooksDisabled = true;
#ifdef HEAT_SHADER_CAPTURE
    if (!activeCallbacks.load()) scratchKeepAlive.Reset();
#endif
    return activeCallbacks.load() ? 1 : 0;
}

DWORD initializeProbe() {
    HMODULE pinned = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
        reinterpret_cast<LPCWSTR>(&initializeProbe), &pinned);
    wchar_t path[32768]{}; GetModuleFileNameW(moduleHandle, path, 32768);
    directory = path; directory.resize(directory.find_last_of(L"\\/") + 1);
#ifdef HEAT_SHADER_CAPTURE
    const std::wstring logPath = directory + L"heat-shader-probe-" + std::to_wstring(GetCurrentProcessId()) + L".log";
    shaderDirectory = directory + L"shaders-" + std::to_wstring(GetCurrentProcessId()) + L"\\";
#ifndef HEAT_PUBLIC_BUILD
    if (!CreateDirectoryW(shaderDirectory.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) return 3;
#endif
#else
    const std::wstring logPath = directory + L"heat-d3d11-probe-" + std::to_wstring(GetCurrentProcessId()) + L".log";
#endif
    logFile.open(logPath, std::ios::out | std::ios::trunc);
    if (!logFile) return 3; // Never patch if the output cannot be written.
    wchar_t readFlag[2]{};
    readConstantData = GetEnvironmentVariableW(L"HEAT_PROBE_READ_CB", readFlag, 2) == 1 && readFlag[0] == L'1';
#ifdef HEAT_SHADER_CAPTURE
#ifdef HEAT_SHADER_CAPTURE_V5
    writeLog("ORIGINAL D3D11 shader probe v5; total<=40960 PS<=4096 CS<=28672 VS<=8192 uniqueDXBC<=128MiB; exactTargetCBRead<=16_CS_512bytes+16_PS_boundRange<=4KiB; startup<=90s firstDraw<=30s");
    wchar_t executable[32768]{}; GetModuleFileNameW(nullptr,executable,32768);
    const wchar_t* basename = wcsrchr(executable,L'\\');
    if (basename && _wcsicmp(basename+1,L"shader_probe_self_test_v5.exe")==0) {
        // Only the own-process unit test uses known compiled self-test hashes.
        // The actual game always keeps the two immutable target hashes above.
        targetVelocityHash="53811a48f601b6c50146367313114d8010d7abee273f16de6ed6d2af1a8e22a7";
        targetTaaHash="28df93c7fb00b1e2fc44e34e1ee8cb1e28293e0097c703c6c453f72231670341";
    }
#else
#ifdef HEAT_SHADER_CAPTURE_V4
    writeLog("ORIGINAL D3D11 shader probe v4; limits=32768 creates total/PS<=4096/CS<=28672/4MiB each/128MiB total unique payload; hash maps retained after disk budget; stop=firstDraw+30s or startup+90s; resourceUniqueBudget=256; readCB=" + std::to_string(readConstantData));
#else
    writeLog("ORIGINAL D3D11 shader probe v3; limits=4096 creates/4MiB each/128MiB total; stop=firstDraw+30s or startup+90s; resourceUniqueBudget=256; readCB=" + std::to_string(readConstantData));
#endif
#endif
#else
    writeLog("ORIGINAL D3D11 passive probe v2; unique(shader+operation+output0); limit=256 candidates/30s; readCB=" + std::to_string(readConstantData));
#endif
    writeLog("Shader identities/resource formats are candidates, NOT validated TAA/motion-vector detections.");
#ifdef HEAT_SHADOW_LONG
    writeLog("SHADOW_LIMIT realInputFrames=3 delayAfterFirstTAA=10s firstDrawLifetime=120s startupLifetime=300s targetShaderOnly=1");
#endif
    ComPtr<ID3D11Device> device; ComPtr<ID3D11DeviceContext> context;
    D3D_FEATURE_LEVEL level{};
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
        D3D11_SDK_VERSION, &device, &level, &context);
    if (FAILED(hr)) {
        writeLog("ERROR: scratch hardware D3D11CreateDevice failed; no hooks installed"); return 1;
    }
#ifdef HEAT_SHADER_CAPTURE
    scratchDeviceIdentity = device.Get();
    scratchKeepAlive = device;
    initializedAt = GetTickCount64();
#endif
    auto** vtable = *reinterpret_cast<void***>(context.Get());
    const unsigned slots[] = {13, 12, 21, 20, 41};
    void* replacements[] = {reinterpret_cast<void*>(&onDraw), reinterpret_cast<void*>(&onDrawIndexed),
        reinterpret_cast<void*>(&onDrawInstanced), reinterpret_cast<void*>(&onDrawIndexedInstanced),
        reinterpret_cast<void*>(&onDispatch)};
    if (MH_Initialize() != MH_OK) { writeLog("ERROR: MinHook initialization failed"); return 2; }
    minHookInitialized = true;
    for (unsigned i = 0; i != 5; ++i) {
        hooks[i] = {vtable[slots[i]], nullptr, replacements[i]};
        hookCount = i + 1;
        const MH_STATUS createResult = MH_CreateHook(hooks[i].target,
            hooks[i].replacement, &hooks[i].original);
        if (createResult != MH_OK || MH_QueueEnableHook(hooks[i].target) != MH_OK) {
            writeLog("ERROR: hook install failed; restoring previous slots"); stopAndRestore(); return 2;
        }
    }
#ifdef HEAT_SHADER_CAPTURE
    // SDK d3d11.h: after 3 IUnknown methods, CreatePixelShader is slot15,
    // CreateComputeShader slot18; signatures match the original HRESULT ABI.
    auto** deviceVtable = *reinterpret_cast<void***>(device.Get());
    hooks[5] = {deviceVtable[15], nullptr, reinterpret_cast<void*>(&onCreatePixelShader)};
    hooks[6] = {deviceVtable[18], nullptr, reinterpret_cast<void*>(&onCreateComputeShader)};
    for (unsigned i = 5; i != 7; ++i) {
        hookCount = i + 1;
        if (MH_CreateHook(hooks[i].target, hooks[i].replacement, &hooks[i].original) != MH_OK ||
            MH_QueueEnableHook(hooks[i].target) != MH_OK) {
            writeLog("ERROR: shader hook install failed"); stopAndRestore(); return 2;
        }
    }
#ifdef HEAT_SHADER_CAPTURE_V5
    // ID3D11Device: CreateVertexShader is slot12, before GS/GS-with-SO/PS.
    hooks[7] = {deviceVtable[12], nullptr, reinterpret_cast<void*>(&onCreateVertexShader)};
    hookCount = 8;
    if (MH_CreateHook(hooks[7].target,hooks[7].replacement,&hooks[7].original)!=MH_OK ||
        MH_QueueEnableHook(hooks[7].target)!=MH_OK) {
        writeLog("ERROR: vertex shader hook install failed"); stopAndRestore(); return 2;
    }
#endif
#endif
    if (MH_ApplyQueued() != MH_OK) { writeLog("ERROR: atomic queue apply failed"); stopAndRestore(); return 2; }
#ifdef HEAT_FG_GAME
    try{ObserveXeFG::install();writeLog("XEFG_OBSERVER_READY");}
    catch(const std::exception& e){writeLog(std::string("XEFG_OBSERVER_FAIL ")+e.what());stopAndRestore();return 2;}
#ifdef HEAT_FG_FACTORY_OBSERVE
    try{wchar_t exe[32768]{};GetModuleFileNameW(nullptr,exe,32768);auto proxyPath=std::filesystem::path(exe).parent_path()/L"dxgi.dll";
        auto proxy=GetModuleHandleW(proxyPath.c_str());ObserveFactory::install(proxy,&writeLog);writeLog("FACTORY_OBSERVER_READY");}
    catch(const std::exception& e){writeLog(std::string("FACTORY_OBSERVER_FAIL ")+e.what());stopAndRestore();return 2;}
#endif
#endif
    isReady.store(true);
    writeLog("READY methodDetoursInstalled scratchVtable=" + std::to_string(reinterpret_cast<uintptr_t>(vtable)));
    return 0;
}
DWORD WINAPI monitor(LPVOID) {
#ifdef HEAT_SHADER_CAPTURE
    while (!stopping.load()) {
        const ULONGLONG now = GetTickCount64(), first = firstActualDrawAt.load();
#ifdef HEAT_SHADOW_LONG
#ifdef HEAT_FG_ARMED_TEST
        const ULONGLONG armed=testArmedAt.load();
#ifdef HEAT_PLAY_SESSION
        (void)armed;
        if(backendStopRequested.load())break;
#else
        if((armed&&now-armed>=lifetimeMs)||(!armed&&now-initializedAt>=600000))break;
#endif
#else
        if ((first && now - first >= lifetimeMs) || now - initializedAt >= 300000) break;
#endif
#else
        if ((first && now - first >= lifetimeMs) || now - initializedAt >= 90000) break;
#endif
        Sleep(20);
    }
#else
    const ULONGLONG deadline = GetTickCount64() + lifetimeMs;
    while (!stopping.load() && GetTickCount64() < deadline) Sleep(20);
#endif
#ifdef HEAT_FG_GAME
    // Release on a game render callback, never on this monitoring thread.
    backendStopRequested.store(true);
    const ULONGLONG releaseDeadline=GetTickCount64()+2000;
    while(!backendStopAcknowledged.load()&&GetTickCount64()<releaseDeadline)Sleep(10);
    writeLog(backendStopAcknowledged.load()?"RENDER_THREAD_RELEASE_ACK":"RENDER_THREAD_RELEASE_PENDING no dispatch; process exit will free remaining resources");
#endif
    return stopAndRestore();
}
DWORD WINAPI worker(LPVOID) {
    const DWORD result = initializeProbe();
    return result ? result : monitor(nullptr);
}
} // namespace

extern "C" __declspec(dllexport) DWORD WINAPI StopProbe(LPVOID) { return stopAndRestore(); }
extern "C" __declspec(dllexport) DWORD WINAPI ProbeReady(LPVOID) {
    return isReady.load() && !stopping.load() ? 1 : 0;
}
#ifdef HEAT_SHADER_CAPTURE
// Called synchronously by Ultimate ASI Loader outside the loader lock.
extern "C" __declspec(dllexport) void InitializeASI() {
    if (initializeCalled.exchange(true)) return;
    try {
        if (initializeProbe() != 0) return;
        HANDLE thread = CreateThread(nullptr, 0, &monitor, nullptr, 0, nullptr);
        if (thread) CloseHandle(thread);
        else { writeLog("ERROR: monitor thread failed, reverting all hooks"); stopAndRestore(); }
    } catch (...) { stopping.store(true); stopAndRestore(); }
}
#endif
BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        moduleHandle = module; DisableThreadLibraryCalls(module);
#ifndef HEAT_SHADER_CAPTURE
        HANDLE thread = CreateThread(nullptr, 0, &worker, nullptr, 0, nullptr);
        if (thread) CloseHandle(thread);
#endif
    }
    return TRUE;
}
