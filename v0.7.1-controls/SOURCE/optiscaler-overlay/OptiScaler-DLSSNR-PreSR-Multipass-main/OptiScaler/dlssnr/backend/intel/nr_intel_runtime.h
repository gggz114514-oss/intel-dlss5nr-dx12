// Thin adapter around the Intel NR component (nrvk_bridge.dll).
//
// The AMD bridge owns the integration: frame capture, the output slot ring, the serial
// discipline, output composition and the Frame-Generation contact surface. This file
// deliberately owns none of that. It only turns a frame into NRB_Frame, calls the
// component and hands its result back, so the two backends can share one pipeline
// instead of the Intel path reimplementing submission, queueing and composition.
#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "nr_bridge.h"

namespace DlssNr::Backend::Intel
{
// One attached component instance (one network). The caller keeps it alive for as long
// as it wants to process frames; Shutdown() must run before the object dies.
class Runtime
{
public:
    Runtime();
    ~Runtime();
    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;

    // Loads the component for `modelDir` (the folder holding manifest\ and
    // nrvk_bridge.dll), attaches it to `device` and registers the callback.
    // `statusJsonPath` may be empty. Returns 0 on success and stores why otherwise.
    int Attach(ID3D12Device* device, const std::wstring& modelDir, const std::wstring& statusJsonPath);
    // Tears the component down. Safe to call more than once. Blocks on in-flight work,
    // so callers must have stopped submitting frames first.
    int Shutdown(); // Nonzero means the runtime still owns resources; retain it.
    bool Attached() const;

    // Runs one frame through the component. On 0 the caller may consume `out`; on any
    // non-zero code the result is a bypass and `out` must be ignored.
    int Process(const NRB_Frame& frame, NRB_Result& out);
    // Releases the component's hold on a delivered output once the consumer fence has
    // completed. Optional: a component without retire() frees on the next Process.
    int Retire(ID3D12Fence* fence, uint64_t value);

    const std::string& LastError() const { return lastError; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
    std::string lastError;
};
}
