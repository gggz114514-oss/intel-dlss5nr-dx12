#pragma once
#include <windows.h>
#include <d3dcompiler.h>
#include <string>

namespace nrb {
// The game may already have loaded an old same-named compiler. Resolve Intel's
// input shader compiler from the full System32 path, without replacing game DLLs.
inline decltype(&D3DCompileFromFile) IntelInputCompiler() {
    static const auto compile=[]() -> decltype(&D3DCompileFromFile) {
        wchar_t system[MAX_PATH]{};
        const UINT length=GetSystemDirectoryW(system,MAX_PATH);
        if(!length || length>=MAX_PATH)return nullptr;
        const std::wstring path=std::wstring(system)+L"\\d3dcompiler_47.dll";
        const HMODULE module=LoadLibraryExW(path.c_str(),nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
        return module?reinterpret_cast<decltype(&D3DCompileFromFile)>(GetProcAddress(module,"D3DCompileFromFile")):nullptr;
    }();
    return compile;
}
}
