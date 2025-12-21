/*
 * Zixiao VDI Agent - Main Entry Point
 *
 * Copyright (c) 2025 Zixiao System
 * SPDX-License-Identifier: Apache-2.0
 *
 * Entry point for Windows VDI guest agent.
 * Supports running as Windows service or console application.
 */

#include "service.h"
#include <iostream>

using namespace zixiao::vdi;

void PrintUsage() {
    std::wcout << L"Zixiao VDI Agent v" << VERSION_MAJOR << L"." << VERSION_MINOR << L"." << VERSION_PATCH << std::endl;
    std::wcout << L"Copyright (c) 2025 Zixiao System" << std::endl;
    std::wcout << std::endl;
    std::wcout << L"Usage: ZixiaoVDIAgent.exe [command]" << std::endl;
    std::wcout << std::endl;
    std::wcout << L"Commands:" << std::endl;
    std::wcout << L"  install    Install as Windows service" << std::endl;
    std::wcout << L"  uninstall  Uninstall Windows service" << std::endl;
    std::wcout << L"  start      Start the service" << std::endl;
    std::wcout << L"  stop       Stop the service" << std::endl;
    std::wcout << L"  console    Run in console mode (for debugging)" << std::endl;
    std::wcout << L"  help       Show this help message" << std::endl;
    std::wcout << std::endl;
    std::wcout << L"If run without arguments, starts as Windows service." << std::endl;
}

int wmain(int argc, wchar_t* argv[]) {
    // Initialize COM for the process
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr)) {
        std::wcerr << L"Failed to initialize COM: 0x" << std::hex << hr << std::endl;
        return 1;
    }

    int result = 0;

    if (argc > 1) {
        std::wstring command = argv[1];

        if (command == L"install" || command == L"-install" || command == L"/install") {
            result = InstallService() ? 0 : 1;
        }
        else if (command == L"uninstall" || command == L"-uninstall" || command == L"/uninstall") {
            result = UninstallService() ? 0 : 1;
        }
        else if (command == L"start" || command == L"-start" || command == L"/start") {
            result = StartServiceManual() ? 0 : 1;
        }
        else if (command == L"stop" || command == L"-stop" || command == L"/stop") {
            result = StopServiceManual() ? 0 : 1;
        }
        else if (command == L"console" || command == L"-console" || command == L"/console" ||
                 command == L"-d" || command == L"/d" || command == L"debug") {
            RunAsConsole();
        }
        else if (command == L"help" || command == L"-help" || command == L"/help" ||
                 command == L"-h" || command == L"/h" || command == L"-?" || command == L"/?") {
            PrintUsage();
        }
        else {
            std::wcerr << L"Unknown command: " << command << std::endl;
            PrintUsage();
            result = 1;
        }
    }
    else {
        // No arguments - run as Windows service
        SERVICE_TABLE_ENTRYW serviceTable[] = {
            { const_cast<LPWSTR>(SERVICE_NAME), ServiceMain },
            { nullptr, nullptr }
        };

        if (!StartServiceCtrlDispatcherW(serviceTable)) {
            DWORD error = GetLastError();
            if (error == ERROR_FAILED_SERVICE_CONTROLLER_CONNECT) {
                // Not running as service - show help
                std::wcout << L"Not running as a service. Use 'console' for debug mode." << std::endl;
                std::wcout << std::endl;
                PrintUsage();
            }
            else {
                std::wcerr << L"Failed to start service dispatcher: " << error << std::endl;
                result = 1;
            }
        }
    }

    CoUninitialize();
    return result;
}
