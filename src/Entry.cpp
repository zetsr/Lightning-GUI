#include "Hook.h"
#include "D3D11.hpp"

static DWORD WINAPI StartHookThread(LPVOID)
{
    OutputDebugStringW(L"[Lightning] StartHookThread entered\r\n");
    ResolveD3D11Exports();
    Lightning::Hook::Install();
    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID)
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
    {
        OutputDebugStringW(L"[Lightning] DllMain PROCESS_ATTACH\r\n");
        DisableThreadLibraryCalls(hModule);
        HANDLE hThread = CreateThread(
            nullptr,
            0,
            StartHookThread,
            nullptr,
            0,
            nullptr);
        if (hThread)
            CloseHandle(hThread);
        break;
    }
    case DLL_PROCESS_DETACH:
    {
        OutputDebugStringW(L"[Lightning] DllMain PROCESS_DETACH\r\n");
        Lightning::Hook::Uninstall();
        GetD3D11Backend().Shutdown();
        break;
    }
    default:
        break;
    }
    return TRUE;
}