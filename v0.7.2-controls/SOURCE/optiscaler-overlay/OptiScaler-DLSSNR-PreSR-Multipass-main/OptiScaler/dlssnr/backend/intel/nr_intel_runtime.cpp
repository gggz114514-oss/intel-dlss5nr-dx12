#include "nr_intel_runtime.h"

#include <windows.h>

#include <atomic>
#include <filesystem>

namespace DlssNr::Backend::Intel
{
namespace
{
// Layout shared with the component. It is copied by value on attach, so the strings it
// points at must outlive the attachment (they live in Impl below).
struct RuntimeConfig
{
    uint32_t abi_size;
    const char* manifest_dir;
    const char* cache_dir;
    const char* status_json_path;
    uint32_t output_ring_slots;
    uint32_t log_verbose;
};

using RegisterFn = int (*)(const void* processor);
using AttachFn = int (*)(ID3D12Device* device, const RuntimeConfig* cfg, RegisterFn register_);
using ShutdownFn = int (*)(void);

// The component cannot carry a user pointer through its registration callback, so the
// processor it hands back is published here and claimed by the instance that is being
// attached. Detach calls the callback with nullptr, which must be tolerated.
std::atomic<NRB_Processor*> g_registerSlot{nullptr};
std::atomic<int> g_insideDetach{0};

int Register(const void* value)
{
    const auto* processor = static_cast<const NRB_Processor*>(value);
    if (!processor) return 1;                        // deregistration: accept and ignore
    NRB_Processor* slot = g_registerSlot.load(std::memory_order_acquire);
    if (!slot || processor->abi_size != sizeof(NRB_Processor) ||
        !processor->process || !processor->retire) return 0;
    *slot = *processor;                              // the component's callbacks, by value
    return 1;                                        // 1 = accepted, anything else aborts attach
}
}

struct Runtime::Impl
{
    HMODULE dll = nullptr;
    AttachFn attach = nullptr;
    ShutdownFn shutdown = nullptr;
    NRB_Processor processor{};
    std::filesystem::path directory;
    std::string manifestDirText;
    std::string statusPathText;

    void Unload()
    {
        if (dll) { FreeLibrary(dll); dll = nullptr; }
        attach = nullptr;
        shutdown = nullptr;
        processor = {};
    }
};

Runtime::Runtime() : impl(std::make_unique<Impl>()) {}
Runtime::~Runtime() {
    if (Shutdown() == 0) impl->Unload();
    else impl.release(); // Keep callback/config storage alive if retirement is unfinished.
}

bool Runtime::Attached() const { return impl->processor.process != nullptr; }

int Runtime::Attach(ID3D12Device* device, const std::wstring& modelDir, const std::wstring& statusJsonPath)
{
    const int stopped = Shutdown();
    if (stopped != 0) { lastError = "previous runtime has not retired"; return stopped; }
    impl->Unload();
    if (!device) { lastError = "no device"; return -1; }

    impl->directory = modelDir;
    const auto dllPath = std::filesystem::path(modelDir) / L"nrvk_bridge.dll";
    impl->dll = LoadLibraryExW(dllPath.c_str(), nullptr,
                              LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!impl->dll) { lastError = "nrvk_bridge.dll not found or rejected: " + dllPath.string(); return -2; }

    impl->attach = reinterpret_cast<AttachFn>(GetProcAddress(impl->dll, "NRVK_AttachToBridge"));
    impl->shutdown = reinterpret_cast<ShutdownFn>(GetProcAddress(impl->dll, "NRVK_Shutdown"));
    if (!impl->attach || !impl->shutdown) { lastError = "component exports missing"; impl->Unload(); return -3; }

    impl->manifestDirText = (std::filesystem::path(modelDir) / L"manifest").string();
    impl->statusPathText = statusJsonPath.empty() ? std::string() : std::filesystem::path(statusJsonPath).string();

    const bool verbose = GetEnvironmentVariableW(L"OPTISCALER_INTEL_NR_VERBOSE", nullptr, 0) > 0;
    RuntimeConfig cfg{sizeof(RuntimeConfig), impl->manifestDirText.c_str(), nullptr,
                      impl->statusPathText.empty() ? nullptr : impl->statusPathText.c_str(), 3u, verbose ? 1u : 0u};

    g_registerSlot.store(&impl->processor, std::memory_order_release);
    int rc = impl->attach(device, &cfg, Register);
    g_registerSlot.store(nullptr, std::memory_order_release);
    if (rc == 0 && !impl->processor.process)
    {
        // The component still had a context from an earlier session and registered
        // nothing; tear it down and attach again for a callback that actually lands.
        std::atomic_store(&g_insideDetach, 1);
        if (impl->shutdown() == 0)
        {
            g_registerSlot.store(&impl->processor, std::memory_order_release);
            rc = impl->attach(device, &cfg, Register);
            g_registerSlot.store(nullptr, std::memory_order_release);
        }
        std::atomic_store(&g_insideDetach, 0);
    }
    if (rc != 0 || !impl->processor.process)
    {
        lastError = "attach failed rc=" + std::to_string(rc);
        impl->Unload();
        return rc != 0 ? rc : -4;
    }
    return 0;
}

int Runtime::Shutdown()
{
    if (!impl->dll || !impl->shutdown) return 0;
    g_registerSlot.store(&impl->processor, std::memory_order_release);
    const int rc = impl->shutdown();
    g_registerSlot.store(nullptr, std::memory_order_release);
    if (rc != 0) return rc;
    impl->processor = {};
    return 0;
}

int Runtime::Process(const NRB_Frame& frame, NRB_Result& out)
{
    if (!impl->processor.process) { lastError = "not attached"; return -5; }
    out = {};
    out.abi_size = sizeof(out);
    return impl->processor.process(impl->processor.context, &frame, &out);
}

int Runtime::Retire(ID3D12Fence* fence, uint64_t value)
{
    if (!impl->processor.retire) return 0;
    return impl->processor.retire(impl->processor.context, fence, value);
}
}
