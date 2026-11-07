// Intel Vulkan NR host GPU contract test.
//
// Drives the product Host exactly as AmdBridge does: a wrapped logical command
// list, Record() cutting the list, then the consumer half recorded after the cut.
// The producer is submitted by the product's expansion path, the runtime runs in
// the between slot, and the continuation reads the replacement the host returned.
//
// Usage: intel_host_gpu.exe <stage-dir> [--size WxH] [--height N] [--frames N]
//                           [--small] [--no-nr] [--post-sr] [--replay] [--switch]
// <stage-dir> must contain nrvk_bridge.dll, nr_hdr_proxy.hlsl and
// nr-intel/{full,small}/{manifest,assets}.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>
#include <fstream>
#include <algorithm>
// The submission headers declare these themselves when absent; do not redefine them.
#include "OptiScaler-DLSSNR-PreSR-Multipass-main/OptiScaler/dlssnr/submission/SubmissionHooks.h"
#include "OptiScaler-DLSSNR-PreSR-Multipass-main/OptiScaler/dlssnr/backend/IntelBackend.h"

using Microsoft::WRL::ComPtr;
using DlssNr::Submission::Hooks::Arm;
using DlssNr::Submission::Hooks::SetProxyWrap;

namespace
{
struct Options
{
    UINT width = 1920, height = 1080;
    UINT height_p = 0;       // NR processing height, 0 = native input
    int frames = 4;
    bool useSmall = false;
    bool noNr = false;       // control: no Record call, plain copy
    bool postSr = false;     // host must refuse a post-SR frame
    bool replay = false;     // execute the same closed recording twice
    bool resize = false;
    bool switchModel = false;// Full -> Small inside one session
    bool gpuMs = false;      // enable the runtime timestamp diag and report gpu_ms
    bool sameFrame = true;   // shared split path always consumes this frame's output
    bool retirementProbe = false;
    bool structureSweep = false; // Reproduce live structure-slider changes without the game.
    bool retainRecordings = false; // Keep completed game lists alive beyond eight records.
    bool queued = false;    // static input, consecutive submissions; read back only the last frame
    std::string stage;
};

[[noreturn]] void Fail(const char* what)
{
    std::fprintf(stderr, "FAIL: %s\n", what);
    std::fflush(stderr);
    std::exit(1);
}

void Require(bool ok, const char* what)
{
    if (!ok) Fail(what);
}

void Check(HRESULT hr, const char* what)
{
    if (FAILED(hr))
    {
        char b[256];
        std::snprintf(b, sizeof(b), "%s (hr=%08lx)", what, static_cast<unsigned long>(hr));
        Fail(b);
    }
}

ComPtr<ID3D12Device> MakeDevice()
{
    ComPtr<IDXGIFactory4> factory;
    Check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)), "DXGI factory");
    ComPtr<ID3D12Device> picked;
    ComPtr<IDXGIAdapter1> adapter;
    bool sawHardware = false;
    for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i)
    {
        DXGI_ADAPTER_DESC1 desc{};
        adapter->GetDesc1(&desc);
        const bool software = (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0;
        ComPtr<ID3D12Device> candidate;
        if (!software && SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&candidate))))
        {
            sawHardware = true;
            // The runtime binds its Vulkan device by LUID, so prefer Intel like the product.
            if (!picked || desc.VendorId == 0x8086)
            {
                picked = candidate;
                wchar_t name[128]{};
                std::swprintf(name, 128, L"%s", desc.Description);
                std::printf("adapter: %ls (%04x:%04x)\n", name, desc.VendorId, desc.DeviceId);
                if (desc.VendorId == 0x8086) break;
            }
        }
        adapter.Reset();
    }
    Require(sawHardware || picked, "no hardware D3D12 adapter");
    Require(picked != nullptr, "no D3D12 device");
    return picked;
}

D3D12_RESOURCE_DESC TextureDesc(UINT w, UINT h, DXGI_FORMAT format)
{
    D3D12_RESOURCE_DESC d{};
    d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    d.Width = w;
    d.Height = h;
    d.DepthOrArraySize = 1;
    d.MipLevels = 1;
    d.Format = format;
    d.SampleDesc.Count = 1;
    d.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    d.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    return d;
}

ComPtr<ID3D12Resource> MakeTexture(ID3D12Device* device, UINT w, UINT h, DXGI_FORMAT format,
                                   D3D12_RESOURCE_STATES state)
{
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    const auto desc = TextureDesc(w, h, format);
    ComPtr<ID3D12Resource> r;
    Check(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr, IID_PPV_ARGS(&r)),
          "texture");
    return r;
}

ComPtr<ID3D12Resource> MakeBuffer(ID3D12Device* device, UINT64 bytes, D3D12_HEAP_TYPE type, D3D12_RESOURCE_STATES state)
{
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = type;
    D3D12_RESOURCE_DESC d{};
    d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    d.Width = bytes;
    d.Height = 1;
    d.DepthOrArraySize = 1;
    d.MipLevels = 1;
    d.SampleDesc.Count = 1;
    d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> r;
    Check(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &d, state, nullptr, IID_PPV_ARGS(&r)),
          "buffer");
    return r;
}

void WaitIdle(ID3D12Device* device, ID3D12CommandQueue* queue)
{
    ComPtr<ID3D12Fence> fence;
    Check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)), "fence");
    Check(queue->Signal(fence.Get(), 1), "signal");
    HANDLE ev = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    Require(ev != nullptr, "event");
    Check(fence->SetEventOnCompletion(1, ev), "set event");
    Require(WaitForSingleObject(ev, 30000) == WAIT_OBJECT_0, "wait idle timeout");
    CloseHandle(ev);
}

void Transition(ID3D12GraphicsCommandList* list, ID3D12Resource* r, D3D12_RESOURCE_STATES before,
                D3D12_RESOURCE_STATES after)
{
    if (!r || before == after) return;
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = r;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore = before;
    b.Transition.StateAfter = after;
    list->ResourceBarrier(1, &b);
}

float HalfToFloat(uint16_t h)
{
    const uint32_t sign = (h >> 15) & 1u, exp = (h >> 10) & 0x1fu, mant = h & 0x3ffu;
    uint32_t bits;
    if (exp == 0)
        bits = (sign << 31) | (mant ? ((120u + 1u - 15u) << 23) | (mant << 13) : 0u);
    else if (exp == 0x1f)
        bits = (sign << 31) | 0x7f800000u | (mant << 13);
    else
        bits = (sign << 31) | ((exp + 112u) << 23) | (mant << 13);
    float out;
    std::memcpy(&out, &bits, 4);
    return out;
}

struct Pixels
{
    UINT width = 0, height = 0, stride = 0;
    std::vector<float> rgba;
    bool finite = true;
    float minValue = 0, maxValue = 0;
    float At(UINT x, UINT y, UINT c) const { return rgba[size_t(y) * stride / 8 + size_t(x) * 4 + c]; }
};

Pixels Readback(ID3D12Device* device, ID3D12CommandQueue* queue, ID3D12Resource* texture)
{
    const auto desc = texture->GetDesc();
    const UINT w = UINT(desc.Width), h = desc.Height;
    const UINT bpp = 8; // RGBA16_FLOAT
    const UINT rowPitch = (w * bpp + 255u) & ~255u;
    const UINT64 bytes = UINT64(rowPitch) * h;
    auto readback = MakeBuffer(device, bytes, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
    ComPtr<ID3D12CommandAllocator> alloc;
    ComPtr<ID3D12GraphicsCommandList> list;
    Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&alloc)), "readback alloc");
    Check(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc.Get(), nullptr, IID_PPV_ARGS(&list)),
          "readback list");
    D3D12_TEXTURE_COPY_LOCATION dst{};
    dst.pResource = readback.Get();
    dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst.PlacedFootprint.Footprint.Format = desc.Format;
    dst.PlacedFootprint.Footprint.Width = w;
    dst.PlacedFootprint.Footprint.Height = h;
    dst.PlacedFootprint.Footprint.Depth = 1;
    dst.PlacedFootprint.Footprint.RowPitch = rowPitch;
    D3D12_TEXTURE_COPY_LOCATION src{};
    src.pResource = texture;
    src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    Check(list->Close(), "readback close");
    ID3D12CommandList* raw = list.Get();
    queue->ExecuteCommandLists(1, &raw);
    WaitIdle(device, queue);
    void* map = nullptr;
    Check(readback->Map(0, nullptr, &map), "readback map");
    Pixels out;
    out.width = w;
    out.height = h;
    out.stride = rowPitch;
    out.rgba.resize(size_t(w) * h * 4);
    const auto* rows = static_cast<const uint8_t*>(map);
    for (UINT y = 0; y < h; ++y)
    {
        const auto* row = reinterpret_cast<const uint16_t*>(rows + size_t(y) * rowPitch);
        for (UINT x = 0; x < w; ++x)
            for (UINT c = 0; c < 4; ++c)
            {
                const float v = HalfToFloat(row[x * 4 + c]);
                out.rgba[size_t(y) * w * 4 + size_t(x) * 4 + c] = v;
                if (!std::isfinite(v)) out.finite = false;
                out.minValue = std::min(out.minValue, v);
                out.maxValue = std::max(out.maxValue, v);
            }
    }
    readback->Unmap(0, nullptr);
    return out;
}

void UploadPattern(ID3D12Device* device, ID3D12CommandQueue* queue, ID3D12Resource* texture, UINT frame)
{
    const auto desc = texture->GetDesc();
    const UINT w = UINT(desc.Width), h = desc.Height;
    const UINT rowPitch = (w * 8u + 255u) & ~255u;
    const UINT64 bytes = UINT64(rowPitch) * h;
    auto upload = MakeBuffer(device, bytes, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
    void* map = nullptr;
    Check(upload->Map(0, nullptr, &map), "upload map");
    for (UINT y = 0; y < h; ++y)
    {
        auto* row = reinterpret_cast<uint16_t*>(static_cast<uint8_t*>(map) + size_t(y) * rowPitch);
        for (UINT x = 0; x < w; ++x)
        {
            const float checker = (((x / 32) + (y / 32)) & 1) ? 0.85f : 0.12f;
            const float ramp = 0.15f + 0.7f * (float(x) / float(w > 1 ? w - 1 : 1));
            const float wobble = 0.05f * std::sin(float(frame) * 0.7f + float(y) * 0.01f);
            row[x * 4 + 0] = 0; // half bits written below
            // Reuse the same half encoder as the runtime path: values stay in [0,1].
            auto enc = [](float v) -> uint16_t {
                if (v <= 0) return 0;
                if (v >= 1) return 0x3c00;
                uint32_t f;
                std::memcpy(&f, &v, 4);
                uint32_t sign = (f >> 16) & 0x8000u;
                int exp = int((f >> 23) & 0xff) - 127;
                uint32_t mant = f & 0x7fffffu;
                if (exp < -14) return uint16_t(sign);
                return uint16_t(sign | uint32_t(exp + 15) << 10 | (mant >> 13));
            };
            row[x * 4 + 0] = enc(checker * ramp + wobble < 0 ? 0 : (checker * ramp + wobble > 1 ? 1 : checker * ramp + wobble));
            row[x * 4 + 1] = enc(ramp);
            row[x * 4 + 2] = enc(checker);
            row[x * 4 + 3] = 0x3c00;
        }
    }
    upload->Unmap(0, nullptr);
    ComPtr<ID3D12CommandAllocator> alloc;
    ComPtr<ID3D12GraphicsCommandList> list;
    Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&alloc)), "upload alloc");
    Check(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc.Get(), nullptr, IID_PPV_ARGS(&list)),
          "upload list");
    Transition(list.Get(), texture, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
    D3D12_TEXTURE_COPY_LOCATION dst{};
    dst.pResource = texture;
    dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    D3D12_TEXTURE_COPY_LOCATION src{};
    src.pResource = upload.Get();
    src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    src.PlacedFootprint.Footprint.Format = desc.Format;
    src.PlacedFootprint.Footprint.Width = w;
    src.PlacedFootprint.Footprint.Height = h;
    src.PlacedFootprint.Footprint.Depth = 1;
    src.PlacedFootprint.Footprint.RowPitch = rowPitch;
    list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    Transition(list.Get(), texture, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Check(list->Close(), "upload close");
    ID3D12CommandList* raw = list.Get();
    queue->ExecuteCommandLists(1, &raw);
    WaitIdle(device, queue);
}

struct StatusFile
{
    bool present = false;
    unsigned long long processed = 0, bypass = 0;
    unsigned lastStatus = 0, initState = 0;
    UINT renderW = 0, renderH = 0, networkW = 0, networkH = 0;
};

StatusFile ReadStatus(const std::string& stage)
{
    StatusFile s;
    std::ifstream in(stage + "\\nr-intel\\status.json", std::ios::binary);
    if (!in) return s;
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    auto number = [&](const char* key) -> long long {
        const std::string needle = std::string("\"") + key + "\":";
        const auto at = text.find(needle);
        if (at == std::string::npos) return -1;
        return std::strtoll(text.c_str() + at + needle.size(), nullptr, 10);
    };
    auto pair = [&](const char* key, UINT& a, UINT& b) -> bool {
        const std::string needle = std::string("\"") + key + "\":[";
        const auto at = text.find(needle);
        if (at == std::string::npos) return false;
        const char* p = text.c_str() + at + needle.size();
        char* end = nullptr;
        a = UINT(std::strtoul(p, &end, 10));
        if (!end || *end != ',') return false;
        b = UINT(std::strtoul(end + 1, &end, 10));
        return true;
    };
    s.present = true;
    s.processed = (unsigned long long)number("processed_count");
    s.bypass = (unsigned long long)number("bypass");
    s.lastStatus = unsigned(number("last_status"));
    s.initState = unsigned(number("init_state"));
    pair("render", s.renderW, s.renderH);
    pair("network", s.networkW, s.networkH);
    return s;
}

struct Session
{
    std::vector<ComPtr<ID3D12CommandAllocator>> queuedAllocs;
    std::vector<ComPtr<ID3D12GraphicsCommandList>> queuedLists;
    ComPtr<ID3D12Fence> queuedFence;
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12CommandAllocator> alloc;
    ComPtr<ID3D12Resource> color, motion, consumer;
    std::unique_ptr<DlssNr::Backend::IntelBackend> backend;
    UINT width = 0, height = 0;
    int frame = 0;
    // The host composites the output the previous recording produced instead of
    // waiting for the current one, so a fresh pipeline owes exactly one frame.
    int pipelineWarmup = 1, lastModel = -1;
    float lastDiff = 0, lastInputDiff = 0;
};

void DrawFrame(Session& s, const Options& o, int model, UINT heightP, bool expectNr, bool replayTwice)
{
    if (o.queued || o.retainRecordings)
    {
        // Model a three-frame game ring, not an unlimited collection of live recordings.
        if (!o.retainRecordings && s.frame >= 3)
        {
            const UINT64 retired = UINT64(s.frame - 2);
            if (s.queuedFence->GetCompletedValue() < retired)
            {
                HANDLE ev = CreateEventW(nullptr, FALSE, FALSE, nullptr);
                Require(ev != nullptr, "queued event");
                Check(s.queuedFence->SetEventOnCompletion(retired, ev), "queued completion");
                const auto wait = WaitForSingleObject(ev, 30000);
                CloseHandle(ev);
                Require(wait == WAIT_OBJECT_0, "queued ring timeout");
            }
            s.queuedLists.erase(s.queuedLists.begin());
            s.queuedAllocs.erase(s.queuedAllocs.begin());
        }
        Check(s.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&s.alloc)), "queued alloc");
        s.queuedAllocs.push_back(s.alloc);
    }
    else Check(s.alloc->Reset(), "frame allocator reset");
    ComPtr<ID3D12GraphicsCommandList> list;
    Check(s.device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, s.alloc.Get(), nullptr, IID_PPV_ARGS(&list)),
          "create list");
    DlssNr::Submission::ILogicalCommandList* logical = nullptr;
    Require(SUCCEEDED(list->QueryInterface(__uuidof(DlssNr::Submission::ILogicalCommandList),
                                           reinterpret_cast<void**>(&logical))) && logical,
            "list is not a wrapped recording");
    logical->Release();

    // Producer half: the game writes its colour for this frame.
    if (!o.queued || s.frame == 0)
        UploadPattern(s.device.Get(), s.queue.Get(), s.color.Get(), s.frame);
    Transition(list.Get(), s.motion.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
               D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    AmdPreSr::Frame frame{};
    frame.colour = s.color.Get();
    frame.motion = s.motion.Get();
    frame.width = s.width;
    frame.height = s.height;
    // The texture contains motion pixels, so NGX scale is 1. UV normalization
    // belongs to the host-to-core adapter, not to this raw NGX input.
    frame.motionScaleX = 1.0f;
    frame.motionScaleY = 1.0f;
    frame.createFlagsKnown=true;frame.hdrInput=true;frame.jitterKnown=true;
    frame.featureIdentity=reinterpret_cast<uintptr_t>(&s);
    frame.motionWidth = 0;      // motion vectors at render resolution (MVLowRes)
    frame.motionHeight = 0;
    frame.jitterX = 0.0f;
    frame.jitterY = 0.0f;
    frame.preExposure = 1.0f;
    frame.exposureScale = 1.0f;
    frame.reset = s.frame == 0;
    frame.temporalInputsValid = true;
    frame.colourState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    frame.motionState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    frame.gameUpscalerInput = !o.postSr;
    AmdPreSr::Settings settings{};
    settings.encoding = 0;
    settings.intel.model = UINT(model);
    settings.intel.inputHeight = heightP;
    settings.intel.style = 0;
    settings.intel.displayStrength = 1.0f;
    settings.intel.intensity = 1.0f;
    settings.intel.structure = o.structureSweep ? float(s.frame % 201) / 100.0f : 1.0f;
    settings.intel.sameFrame = o.sameFrame;

    ID3D12Resource* replacement = nullptr;
    if (model != s.lastModel)
    {
        s.lastModel = model;
        s.pipelineWarmup = 1;
    }
    // The host composites the previous recording's output, so the first recording of a
    // fresh pipeline still carries the original colour: it is the fill frame, not NR.
    bool fillFrame = false;
    if (!o.noNr)
    {
        replacement = s.backend->Record(list.Get(), frame, settings);
        if (o.postSr)
            Require(replacement == nullptr, "post-SR frame must be refused, not processed");
        if (!o.postSr)
        {
            Require(replacement != nullptr, "host did not record a replacement");
            // Same-frame mode composites the frame it just submitted, so only the
            // pipelined default owes a fill frame.
            if (!o.sameFrame && s.pipelineWarmup > 0)
            {
                --s.pipelineWarmup;
                fillFrame = true;
                std::printf("host status (pipeline fill): %s\n", s.backend->Status().c_str());
            }
        }
    }

    // Continuation half: what the upscaler would read. The copy proves the consumer
    // sees the host's replacement only after its ready fence.
    ID3D12Resource* source = replacement ? replacement : s.color.Get();
    const auto before = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    Transition(list.Get(), source, before, D3D12_RESOURCE_STATE_COPY_SOURCE);
    Transition(list.Get(), s.consumer.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
               D3D12_RESOURCE_STATE_COPY_DEST);
    list->CopyResource(s.consumer.Get(), source);
    Transition(list.Get(), source, D3D12_RESOURCE_STATE_COPY_SOURCE, before);
    Transition(list.Get(), s.consumer.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
               D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Check(list->Close(), "close game list");

    ID3D12CommandList* raw = list.Get();
    if (o.queued || o.retainRecordings) s.queuedLists.push_back(list);
    LARGE_INTEGER submitStart{}, submitEnd{}, frequency{};
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&submitStart);
    s.queue->ExecuteCommandLists(1, &raw);
    QueryPerformanceCounter(&submitEnd);
    if (o.queued) Check(s.queue->Signal(s.queuedFence.Get(), UINT64(s.frame + 1)), "queued signal");
    if (o.queued)
    {
        std::printf("queued frame=%d submit_ms=%.3f status=%s\n", s.frame,
            1000.0 * double(submitEnd.QuadPart-submitStart.QuadPart)/double(frequency.QuadPart),
            s.backend->Status().c_str());
        std::fflush(stdout);
    }
    if (replayTwice)
    {
        // A closed recording may be executed again while the first execution is
        // still on the GPU. That must not drop the list or corrupt the output.
        s.queue->ExecuteCommandLists(1, &raw);
    }
    if (o.queued && s.frame + 1 < o.frames) { ++s.frame; return; }
    WaitIdle(s.device.Get(), s.queue.Get());

    const auto consumer = Readback(s.device.Get(), s.queue.Get(), s.consumer.Get());
    Require(consumer.width == s.width && consumer.height == s.height, "consumer extent");
    Require(consumer.finite, "consumer pixels are not finite");

    const auto input = Readback(s.device.Get(), s.queue.Get(), s.color.Get());
    double diff = 0;
    for (size_t i = 0; i < consumer.rgba.size(); ++i)
        diff += std::fabs(double(consumer.rgba[i]) - double(input.rgba[i]));
    s.lastDiff = float(diff / double(consumer.rgba.size()));
    s.lastInputDiff = s.lastDiff;
    std::printf("frame %d model=%s height=%u replacement=%s mean|nr-input|=%.6f range=[%.3f,%.3f]\n",
                s.frame, model ? "small" : "full", heightP, replacement ? "yes" : "no", s.lastDiff,
                consumer.minValue, consumer.maxValue);

    std::printf("host status: %s\n", s.backend->Status().c_str());
    if (!expectNr)
        Require(s.lastDiff <= 1e-5f, "NR-off control must reproduce the input exactly");
    else if (fillFrame)
        Require(s.lastDiff <= 1e-5f, "pipeline fill frame must be the original colour");
    else
        Require(s.lastDiff > 1e-4f, "NR did not change the frame (pure bypass)");
    ++s.frame;
}

struct RuntimeCfg
{
    unsigned abi_size;
    const char* manifest_dir;
    const char* cache_dir;
    const char* status_json_path;
    unsigned output_ring_slots, log_verbose;
};

// The shipped component validates its manifest through the same export the
// Cyberpunk ASI preflights with. A failing probe is reported, never guessed.
void ProbeRuntime(const Options& o)
{
    // Each network manifest is only valid for the component build that produced it,
    // so the model's own runtime wins over the shared one at the stage root.
    char dll[MAX_PATH]{};
    const char* model = o.useSmall ? "small" : "full";
    std::snprintf(dll, sizeof(dll), "%s\\nr-intel\\%s\\nrvk_bridge.dll", o.stage.c_str(), model);
    HMODULE module = LoadLibraryExA(dll, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!module)
    {
        std::snprintf(dll, sizeof(dll), "%s\\nrvk_bridge.dll", o.stage.c_str());
        module = LoadLibraryExA(dll, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    }
    Require(module != nullptr, "stage nrvk_bridge.dll did not load");
    auto self = reinterpret_cast<int (*)(const RuntimeCfg*, void*)>(
        GetProcAddress(module, "NRVK_ComponentSelfTest"));
    if (!self)
    {
        std::puts("runtime self test: export missing");
        return;
    }
    const std::string manifest = o.stage + "\\nr-intel\\" + (o.useSmall ? "small" : "full") + "\\manifest";
    RuntimeCfg cfg{40, manifest.c_str(), nullptr, nullptr, 3, 1};
    const int rc = self(&cfg, nullptr);
    std::printf("runtime self test: rc=%d manifest=%s\n", rc, manifest.c_str());
    if (rc != 0) Fail("runtime rejected the staged manifest");
}

// The runtime writes Vulkan timestamp deltas into its status JSON only while its
// diagnostic mode is on. The mode is set through the component export after the
// first processed frame, because the device exists only from then on.
void EnableGpuTiming()
{
    HMODULE module = GetModuleHandleA("nrvk_bridge.dll");
    if (!module) { std::puts("gpu timing: runtime module not loaded"); return; }
    auto diag = reinterpret_cast<void (*)(unsigned, unsigned, unsigned)>(
        GetProcAddress(module, "NRVK_DiagSetState"));
    if (!diag) { std::puts("gpu timing: NRVK_DiagSetState missing"); return; }
    diag(2u, 0u, 0u);
    std::puts("gpu timing: diag mode 2 enabled");
}

void ReportGpuMs(const std::string& stage)
{
    std::ifstream in(stage + "\\nr-intel\\status.json", std::ios::binary);
    if (!in) { std::puts("gpu_ms: no status file"); return; }
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const auto at = text.find("\"gpu_ms\":");
    if (at == std::string::npos) { std::puts("gpu_ms: absent"); return; }
    std::printf("gpu_ms: %s\n", text.substr(at + 9, 160).c_str());
}

void Run(const Options& o)
{
    Session s;
    ProbeRuntime(o);
    s.device = MakeDevice();
    D3D12_COMMAND_QUEUE_DESC qd{};
    qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    Check(s.device->CreateCommandQueue(&qd, IID_PPV_ARGS(&s.queue)), "queue");
    if (o.retirementProbe)
    {
        ComPtr<ID3D12Fence> gate;
        ComPtr<ID3D12CommandAllocator> alloc;
        ComPtr<ID3D12GraphicsCommandList> list;
        Check(s.device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&gate)), "probe gate");
        Check(s.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&alloc)), "probe allocator");
        Check(s.device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc.Get(), nullptr, IID_PPV_ARGS(&list)), "probe list");
        Check(s.queue->Wait(gate.Get(), 1), "probe queue wait");
        DlssNr::Submission::LogicalList logical;
        Check(logical.BindProducer(s.device.Get(), alloc.Get(), list.Get()), "probe bind");
        Check(logical.Execute(s.queue.Get()), "probe execute");
        alloc.Reset(); list.Reset(); // LogicalList now owns the submitted references.
        const auto start = GetTickCount64();
        logical.Release();
        const auto elapsed = GetTickCount64() - start;
        const auto queued = DlssNr::Submission::g_deferredReleaseQueued.load();
        const auto before = DlssNr::Submission::g_deferredReleaseCompleted.load();
        Check(gate->Signal(1), "probe release gate");
        WaitIdle(s.device.Get(), s.queue.Get());
        logical.Release(); // Poll after proven GPU completion.
        const auto after = DlssNr::Submission::g_deferredReleaseCompleted.load();
        std::printf("retirement probe release_ms=%llu queued=%llu reclaimed_before=%llu reclaimed_after=%llu\n",
            (unsigned long long)elapsed,(unsigned long long)queued,(unsigned long long)before,(unsigned long long)after);
        Require(elapsed < 1000, "release waited for an unsignaled GPU gate");
        Require(queued == 1 && before == 0 && after == 1, "deferred ownership did not follow the fence");
        Require(DlssNr::Submission::g_retireWaitCount.load() == 0, "release CPU-waited");
        std::puts("PASS"); return;
    }
    if (o.queued) Check(s.device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&s.queuedFence)), "queued fence");
    // The product installs this before the first wrapped list is exposed.
    Check(Arm(s.device.Get(), s.queue.Get()), "arm submission hooks");
    SetProxyWrap(true);
    DlssNr::Submission::Hooks::SetWrapOpenLists(true);

    s.width = o.width;
    s.height = o.height;
    s.color = MakeTexture(s.device.Get(), s.width, s.height, DXGI_FORMAT_R16G16B16A16_FLOAT,
                          D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    s.motion = MakeTexture(s.device.Get(), s.width, s.height, DXGI_FORMAT_R16G16_FLOAT,
                           D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    s.consumer = MakeTexture(s.device.Get(), s.width, s.height, DXGI_FORMAT_R16G16B16A16_FLOAT,
                             D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Check(s.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&s.alloc)), "alloc");
    s.backend = std::make_unique<DlssNr::Backend::IntelBackend>(s.device.Get(), s.queue.Get(),
                                                                std::filesystem::path(o.stage));

    int model = o.useSmall ? 1 : 0;
    for (int i = 0; i < o.frames; ++i)
    {
        if(o.resize&&(i==2||i==4)) {
            s.width=i==2?960:o.width;s.height=i==2?540:o.height;
            s.color=MakeTexture(s.device.Get(),s.width,s.height,DXGI_FORMAT_R16G16B16A16_FLOAT,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            s.motion=MakeTexture(s.device.Get(),s.width,s.height,DXGI_FORMAT_R16G16_FLOAT,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            s.consumer=MakeTexture(s.device.Get(),s.width,s.height,DXGI_FORMAT_R16G16B16A16_FLOAT,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            std::printf("resize same feature: %ux%u\n",s.width,s.height);
        }
        if (o.switchModel && i == o.frames / 2)
        {
            model = 1;
            std::puts("switching Full -> Small");
        }
        // A replayed recording degrades to the producer's original-colour copy on the
        // second execution; that is the contract, not a host bypass.
        if (o.gpuMs && i == 1) EnableGpuTiming();
        const bool replaying = o.replay && i == 1;
        DrawFrame(s, o, model, o.height_p, !o.noNr && !o.postSr && !replaying, replaying);
    }
    if (o.replay)
    {
        // One more ordinary frame proves the replay left a usable recording behind.
        DrawFrame(s, o, model, o.height_p, !o.noNr && !o.postSr, false);
    }

    if (!o.noNr && !o.postSr)
    {
        auto status = ReadStatus(o.stage);
        // Status snapshots are published asynchronously at 4 Hz. This waits for
        // telemetry only, after the final GPU readback has already completed.
        for(int poll=0;poll<10 && (!status.present || status.processed < ((o.queued || o.retainRecordings)?static_cast<unsigned long long>(o.frames):1ULL));++poll)
        { Sleep(100);status=ReadStatus(o.stage); }
        Require(status.present, "runtime status.json was not written");
        std::printf("status: processed=%llu bypass=%llu last_status=%u init_state=%u render=%ux%u network=%ux%u\n",
                    status.processed, status.bypass, status.lastStatus, status.initState,
                    status.renderW, status.renderH, status.networkW, status.networkH);
        Require(status.processed >= UINT64_C(1), "runtime never processed a frame");
        if (o.queued || o.retainRecordings)
        {
            Require(status.processed == static_cast<unsigned long long>(o.frames), "queued frames were lost or runtime rebuilt");
            Require(status.bypass == 0, "queued run bypassed NR");
        }
        Require(status.renderW == s.width && status.renderH == s.height, "runtime render extent mismatch");
        Require(status.networkW != 0 && status.networkH != 0, "runtime network extent missing");
        if (o.height_p)
            Require(status.networkH == o.height_p, "runtime ignored the requested processing height");
        else
            Require(status.networkH == s.height, "native request did not stay at the input height");
    }
    if (o.gpuMs) ReportGpuMs(o.stage);
    s.queuedLists.clear();
    s.queuedAllocs.clear();
    Require(s.backend->Shutdown(), "host did not shut the runtime down");
    std::printf("retirement waits=%llu deferred=%llu reclaimed=%llu\n",
        (unsigned long long)DlssNr::Submission::g_retireWaitCount.load(),
        (unsigned long long)DlssNr::Submission::g_deferredReleaseQueued.load(),
        (unsigned long long)DlssNr::Submission::g_deferredReleaseCompleted.load());
    Require(DlssNr::Submission::g_retireWaitCount.load() == 0, "retirement blocked the caller");
    std::puts("PASS");
}

} // namespace

int main(int argc, char** argv)
{
    Options o;
    if (argc < 2)
    {
        std::fprintf(stderr, "usage: %s <stage-dir> [--size WxH] [--height N] [--frames N] [--small] "
                             "[--no-nr] [--post-sr] [--replay] [--switch]\n", argv[0]);
        return 2;
    }
    o.stage = argv[1];
    for (int i = 2; i < argc; ++i)
    {
        const std::string a = argv[i];
        if (a == "--size" && i + 1 < argc)
        {
            unsigned w = 0, h = 0;
            if (std::sscanf(argv[++i], "%ux%u", &w, &h) != 2) return 2;
            o.width = w;
            o.height = h;
        }
        else if (a == "--height" && i + 1 < argc) o.height_p = UINT(std::atoi(argv[++i]));
        else if (a == "--frames" && i + 1 < argc) o.frames = std::atoi(argv[++i]);
        else if (a == "--small") o.useSmall = true;
        else if (a == "--retirement-probe") o.retirementProbe = true;
        else if (a == "--structure-sweep") o.structureSweep = true;
        else if (a == "--queued") o.queued = true;
        else if (a == "--retain-recordings") o.retainRecordings = true;
        else if (a == "--no-nr") o.noNr = true;
        else if (a == "--post-sr") o.postSr = true;
        else if (a == "--replay") o.replay = true;
        else if (a == "--same-frame") o.sameFrame = true;
        else if (a == "--resize") o.resize = true;
        else if (a == "--switch") o.switchModel = true;
        else if (a == "--gpu-ms") o.gpuMs = true;
        else { std::fprintf(stderr, "unknown argument: %s\n", a.c_str()); return 2; }
    }
    try
    {
        Run(o);
    }
    catch (const std::exception& e)
    {
        std::fprintf(stderr, "FAIL: exception: %s\n", e.what());
        return 1;
    }
    return 0;
}
