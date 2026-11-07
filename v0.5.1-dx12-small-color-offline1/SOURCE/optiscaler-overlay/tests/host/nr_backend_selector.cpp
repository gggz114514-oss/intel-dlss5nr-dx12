#include "OptiScaler-DLSSNR-PreSR-Multipass-main/OptiScaler/dlssnr/backend/Kind.h"
#include <cassert>
#include <iostream>
int main() {
    using namespace DlssNr::Backend;
    for (const auto* raw : {"intel", "INTEL", "daniel", "lmxxf", "mochizuki", "auto", "off", "none", "", "unknown"}) {
        assert(ParseKind(raw) == Kind::Intel);
        for (bool daniel : {false, true}) for (bool lmxxf : {false, true})
            for (bool mochi : {false, true})
                assert(ResolveInstalled(ParseRequest(raw), daniel, lmxxf, true, mochi) == Kind::Intel);
    }
    assert(PrepareSubmissionAtStartup(Kind::Intel, false, false));
    assert(!PrepareDanielGraphicsAtStartup(Kind::Intel, false, true, true));
    std::cout << "nr_backend_selector: Intel-only routing PASS\n";
}
