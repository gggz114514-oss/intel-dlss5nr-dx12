#include "pch.h"
#include "Selector.h"
#include "InstallStatus.h"
#include "../NrEffectsSettings.h"
#include <Config.h>
#include <Util.h>
#include <filesystem>
#include <mutex>

namespace DlssNr::Backend
{
namespace
{
// Disk probe is not free on the render/submit path. Old ActiveKindFromConfig only
// touched disk for auto; this runs on Before/Execute too, so cache the probe.
struct InstallProbe
{
    std::mutex mu;
    bool hasDaniel = false;
    bool hasLmxxf = false;
    bool hasMochizuki = false;
    bool valid = false;
    unsigned long long tick = 0;
    std::string issues[4];
    unsigned long long issueTick[4]{};
    bool issueValid[4]{};
};
InstallProbe g_probe;

void RefreshProbeLocked(InstallProbe& p)
{
    std::error_code ec;
    const auto dir = Util::DllPath().parent_path();
    p.hasDaniel = std::filesystem::exists(dir / L"dlssnr_amd_pass1.dll", ec);
    p.hasLmxxf = std::filesystem::exists(dir / L"LmxxfNrRuntime.dll", ec);
    p.hasMochizuki = std::filesystem::exists(dir / L"MochizukiNrRuntime.dll", ec);
    p.valid = true;
    p.tick = GetTickCount64();
}

void EnsureProbe(InstallProbe& p)
{
    const unsigned long long now = GetTickCount64();
    if (p.valid)
    {
        // Still missing both: the proxy path may appear late, so retry more often.
        const unsigned long long ttl = (p.hasDaniel || p.hasLmxxf || p.hasMochizuki) ? 1000ull : 100ull;
        if (now - p.tick < ttl)
            return;
    }
    RefreshProbeLocked(p);
}
} // namespace

void InvalidateInstallProbe()
{
    std::lock_guard lock(g_probe.mu);
    g_probe.valid = false;
    for (auto& valid : g_probe.issueValid) valid = false;
}

std::string InstallIssue(Kind kind)
{
    std::lock_guard lock(g_probe.mu);
    const auto index = static_cast<int>(kind);
    const auto now = GetTickCount64();
    if (!g_probe.issueValid[index] || now - g_probe.issueTick[index] >= 1000ull) {
        g_probe.issues[index] = ProbeInstallIssue(kind, Util::DllPath().parent_path());
        g_probe.issueTick[index] = now;
        g_probe.issueValid[index] = true;
    }
    return g_probe.issues[index];
}

bool HasDanielInstalled()
{
    std::lock_guard lock(g_probe.mu);
    EnsureProbe(g_probe);
    return g_probe.hasDaniel;
}

bool HasLmxxfInstalled()
{
    std::lock_guard lock(g_probe.mu);
    EnsureProbe(g_probe);
    return g_probe.hasLmxxf;
}

bool HasMochizukiInstalled()
{
    std::lock_guard lock(g_probe.mu);
    EnsureProbe(g_probe);
    return g_probe.hasMochizuki;
}

Kind RequestedKind() { return Kind::Intel; }

bool HasIntelInstalled() { return std::filesystem::exists(Util::DllPath().parent_path()/L"nrvk_bridge.dll"); }

Kind ActiveKindFromConfig() { return Kind::Intel; }

bool SubmissionHooksWanted()
{
    // Must NOT depend on DlssNrEnabled: device hooks are installed once at
    // CreateDevice. If we skip ArmCreate when NR starts off, enabling NR
    // in-game has no submission path and the pass cannot start.
    return ProxyWrapWanted();
}

bool DanielGraphicsHooksWanted()
{
    return false; // Intel edition never arms Daniel-specific graphics hooks.
}

bool ProxyWrapWanted()
{
    // Common effects need recording lifetime callbacks even with a single backend.
    // This requests only the proxy; it never instantiates the inactive backend.
    if (!Config::Instance()->DlssNrRunBeforeSr.value_or_default() ||
        Config::Instance()->NrStabilizerEnabled.value_or_default() ||
        DlssNr::OverallIntensity(Config::Instance()->NrOverallIntensity.value_or_default()) != 1.0f ||
        DlssNr::ResidualSettings {Config::Instance()->NrResidualLowGain.value_or_default(),
            Config::Instance()->NrResidualDetailGain.value_or_default(),
            Config::Instance()->NrResidualSkinProtection.value_or_default(),
            Config::Instance()->NrResidualEdgeProtection.value_or_default()}.Active())
        return true;
    if (!LmxxfWired())
        return false;
    return PrepareSubmissionAtStartup(ActiveKindFromConfig(),
        Config::Instance()->NrConvenience.value_or_default() != 0, HasLmxxfInstalled() || HasMochizukiInstalled() || HasIntelInstalled());
}
} // namespace DlssNr::Backend
