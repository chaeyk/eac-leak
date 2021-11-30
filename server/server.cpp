#include "stdafx.h"
#include "eos.h"

#pragma comment (lib, "Ws2_32.lib")

BOOL WINAPI CtrlHandler(DWORD fdwCtrlType)
{
    return TRUE;
}

SIZE_T CurrentMemorySize()
{
    HANDLE hProcess = GetCurrentProcess();

    PROCESS_MEMORY_COUNTERS_EX counters;
    ZeroMemory(&counters, sizeof(counters));

    if (GetProcessMemoryInfo(hProcess, (PROCESS_MEMORY_COUNTERS*) &counters, sizeof(counters)))
    {
        CloseHandle(hProcess);
        return counters.PrivateUsage / 1024;
    }

    CloseHandle(hProcess);
    return 0;
}

int main()
{
    Eos eos;
    if (!eos.Init())
    {
        return 1;
    }
    if (!eos.RegisterCallbacks())
    {
        return 2;
    }
    if (!eos.BeginSession())
    {
        return 3;
    }

    SIZE_T beforeMemory = CurrentMemorySize();

    //Initialize Winsock
    std::cout << "Intializing Winsock..." << std::endl;
    WSADATA wsaData;
    int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (result != 0)
    {
        std::cout << "WSAStartup() failed: " << result << std::endl;
        return 4;
    }

    SetConsoleCtrlHandler(CtrlHandler, TRUE);

    for (int i = 1; i < 5000; i++)
    {
        eos.RegisterClient(i);
        Sleep(50);
        eos.UnregisterClient(i);
    }

    SIZE_T afterMemory = CurrentMemorySize();
    std::cout << "press enter..." << std::endl;
    fgetc(stdin);
    std::cout << "====== Memory: " << beforeMemory << "kb -> " << afterMemory << "kb ======" << std::endl;

    //Clean up Winsock
    WSACleanup();
    eos.EndSession();
    eos.UnregisterCallbacks();
    eos.Shutdown();
    std::cout << "Program has ended successfully" << std::endl;

    return 0;
}
