#pragma once
#include "Host.h"
#include <filesystem>
#include <memory>
namespace DlssNr::Backend {
class IntelBackend final : public Host {
public:
    struct Impl;
    explicit IntelBackend(ID3D12Device*, ID3D12CommandQueue*, const std::filesystem::path&);
    ~IntelBackend() override;
    ID3D12Resource* Record(ID3D12GraphicsCommandList*, const AmdPreSr::Frame&, const AmdPreSr::Settings&) override;
    int PendingListIndex(UINT, ID3D12CommandList* const*) const override { return -1; }
    void Submitting(ID3D12CommandQueue*,UINT,ID3D12CommandList* const*) override {}
    void Submitted(ID3D12CommandQueue*,UINT,ID3D12CommandList* const*) override;
    void TraceBoundary(const std::string&) override {}
    bool Shutdown() override;
    void ReleaseSession() override;
    bool PollRelease() override;
    void ResetGraphicsWaitState() override {}
    void InvalidateHistory() override;
    std::string Status() const override;
    bool IsRunning() const override;
    bool GraphicsRestartNeeded(UINT) const override { return false; }
private:
    std::shared_ptr<Impl> p;
};
}
