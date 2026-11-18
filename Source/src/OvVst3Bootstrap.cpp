#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <iterator>
#include <mutex>
#include <string>

extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace
{
    HMODULE coreModule = nullptr;
    std::once_flag loadOnce;

    void loadCoreModule()
    {
        wchar_t buffer[32768];
        const auto length = GetModuleFileNameW (reinterpret_cast<HMODULE> (&__ImageBase),
                                                buffer,
                                                static_cast<DWORD> (std::size (buffer)));
        if (length == 0 || length >= std::size (buffer))
            return;

        std::wstring path (buffer, length);
        const auto separator = path.find_last_of (L"\\/");
        if (separator == std::wstring::npos)
            return;

        path.resize (separator + 1);
        path += L"OverteonCore.dll";
        coreModule = LoadLibraryExW (path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    }

    template <typename Function>
    Function coreFunction (const char* name)
    {
        std::call_once (loadOnce, loadCoreModule);
        return coreModule != nullptr
                 ? reinterpret_cast<Function> (GetProcAddress (coreModule, name))
                 : nullptr;
    }
}

extern "C" __declspec(dllexport) bool InitDll()
{
    using Function = bool (*)();
    const auto function = coreFunction<Function> ("InitDll");
    return function != nullptr && function();
}

extern "C" __declspec(dllexport) bool ExitDll()
{
    using Function = bool (*)();
    const auto function = coreFunction<Function> ("ExitDll");
    return function != nullptr && function();
}

extern "C" __declspec(dllexport) void* GetPluginFactory()
{
    using Function = void* (*)();
    const auto function = coreFunction<Function> ("GetPluginFactory");
    return function != nullptr ? function() : nullptr;
}
