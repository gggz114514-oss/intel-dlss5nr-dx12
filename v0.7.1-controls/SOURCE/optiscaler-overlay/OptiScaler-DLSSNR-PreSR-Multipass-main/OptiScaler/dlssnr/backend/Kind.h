#pragma once
#include <string_view>

namespace DlssNr::Backend
{
enum class Kind
{
    Daniel,
    Lmxxf,
    Mochizuki,
    Intel,
};

// What the ini asked for. Config load migrates legacy "off"/"none" to
// Enabled=false; any unrecognized value resolves as Auto.
enum class Request
{
    Auto,
    Daniel,
    Lmxxf,
    Mochizuki,
    Intel,
};

inline bool KindEq(std::string_view a, std::string_view b)
{
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i)
    {
        const unsigned char ca = static_cast<unsigned char>(a[i]);
        const unsigned char cb = static_cast<unsigned char>(b[i]);
        const auto lo = [](unsigned char c) {
            return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : static_cast<char>(c);
        };
        if (lo(ca) != lo(cb))
            return false;
    }
    return true;
}

inline Request ParseRequest(std::string_view raw)
{
    if (KindEq(raw, "intel")) return Request::Intel;
    if (KindEq(raw, "mochizuki")) return Request::Mochizuki;
    if (KindEq(raw, "lmxxf"))
        return Request::Lmxxf;
    if (KindEq(raw, "daniel"))
        return Request::Daniel;
    return Request::Auto;
}

// Intel edition: legacy backend names never select an AMD NR runtime.
inline Kind ParseKind(std::string_view) { return Kind::Intel; }

// lmxxf is a shipped production backend alongside daniel.
inline bool LmxxfWired() { return true; }

inline bool PrepareSubmissionAtStartup(Kind active, bool convenience, bool hasLmxxf)
{
    return active == Kind::Intel || active == Kind::Lmxxf || active == Kind::Mochizuki || (convenience && hasLmxxf);
}


// Daniel-only state capture is useful for the active Daniel backend or a
// possible hot switch. Pure lmxxf must not pay for it even when new wait is on.
inline bool PrepareDanielGraphicsAtStartup(Kind active, bool convenience, bool hasDaniel, bool graphicsWait)
{
    return graphicsWait && (active == Kind::Daniel || (convenience && hasDaniel));
}

// Legacy enum values remain for shared upstream helpers; routing is Intel-only.
inline const char* Name(Kind kind)
{
    switch (kind) { case Kind::Intel: return "intel"; case Kind::Lmxxf: return "lmxxf"; case Kind::Mochizuki: return "mochizuki"; default: return "daniel"; }
}
inline Kind ResolveInstalled(Request, bool, bool, bool, bool = false)
{
    return Kind::Intel;
}
} // namespace DlssNr::Backend
