#include <windows.h>
#include <cstdio>
#include <cstdint>
#include <vector>

// Forward all 17 exports of version.dll to System32 version.dll
#pragma comment(linker, "/export:GetFileVersionInfoA=C:\\Windows\\System32\\version.GetFileVersionInfoA")
#pragma comment(linker, "/export:GetFileVersionInfoByHandle=C:\\Windows\\System32\\version.GetFileVersionInfoByHandle")
#pragma comment(linker, "/export:GetFileVersionInfoExA=C:\\Windows\\System32\\version.GetFileVersionInfoExA")
#pragma comment(linker, "/export:GetFileVersionInfoExW=C:\\Windows\\System32\\version.GetFileVersionInfoExW")
#pragma comment(linker, "/export:GetFileVersionInfoSizeA=C:\\Windows\\System32\\version.GetFileVersionInfoSizeA")
#pragma comment(linker, "/export:GetFileVersionInfoSizeExA=C:\\Windows\\System32\\version.GetFileVersionInfoSizeExA")
#pragma comment(linker, "/export:GetFileVersionInfoSizeExW=C:\\Windows\\System32\\version.GetFileVersionInfoSizeExW")
#pragma comment(linker, "/export:GetFileVersionInfoSizeW=C:\\Windows\\System32\\version.GetFileVersionInfoSizeW")
#pragma comment(linker, "/export:GetFileVersionInfoW=C:\\Windows\\System32\\version.GetFileVersionInfoW")
#pragma comment(linker, "/export:VerFindFileA=C:\\Windows\\System32\\version.VerFindFileA")
#pragma comment(linker, "/export:VerFindFileW=C:\\Windows\\System32\\version.VerFindFileW")
#pragma comment(linker, "/export:VerInstallFileA=C:\\Windows\\System32\\version.VerInstallFileA")
#pragma comment(linker, "/export:VerInstallFileW=C:\\Windows\\System32\\version.VerInstallFileW")
#pragma comment(linker, "/export:VerLanguageNameA=C:\\Windows\\System32\\version.VerLanguageNameA")
#pragma comment(linker, "/export:VerLanguageNameW=C:\\Windows\\System32\\version.VerLanguageNameW")
#pragma comment(linker, "/export:VerQueryValueA=C:\\Windows\\System32\\version.VerQueryValueA")
#pragma comment(linker, "/export:VerQueryValueW=C:\\Windows\\System32\\version.VerQueryValueW")

// Global Configuration
struct Config {
    bool enabled = true;
    bool bypassAffinity = true;
    int workerThreadCount = 0; // 0 = Auto (P-cores / max 8), -1 = Disabled, >0 = manual
    bool enableLog = true;
} g_Config;

FILE* g_LogFile = nullptr;

void Log(const char* format, ...) {
    if (!g_Config.enableLog || !g_LogFile) return;
    va_list args;
    va_start(args, format);
    SYSTEMTIME st;
    GetLocalTime(&st);
    fprintf(g_LogFile, "[%02d:%02d:%02d.%03d] ", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    vfprintf(g_LogFile, format, args);
    fprintf(g_LogFile, "\n");
    fflush(g_LogFile);
    va_end(args);
}

// Read INI Configuration
void LoadConfig(const wchar_t* iniPath) {
    g_Config.enabled = GetPrivateProfileIntW(L"Hoi4Optimizer", L"Enabled", 1, iniPath) != 0;
    g_Config.bypassAffinity = GetPrivateProfileIntW(L"Hoi4Optimizer", L"BypassAffinityPinning", 1, iniPath) != 0;
    g_Config.workerThreadCount = GetPrivateProfileIntW(L"Hoi4Optimizer", L"WorkerThreadCount", 0, iniPath);
    g_Config.enableLog = GetPrivateProfileIntW(L"Hoi4Optimizer", L"EnableLog", 1, iniPath) != 0;

    // Create default INI if it doesn't exist
    if (GetFileAttributesW(iniPath) == INVALID_FILE_ATTRIBUTES) {
        WritePrivateProfileStringW(L"Hoi4Optimizer", L"Enabled", L"1", iniPath);
        WritePrivateProfileStringW(L"Hoi4Optimizer", L"BypassAffinityPinning", L"1", iniPath);
        WritePrivateProfileStringW(L"Hoi4Optimizer", L"WorkerThreadCount", L"0", iniPath);
        WritePrivateProfileStringW(L"Hoi4Optimizer", L"EnableLog", L"1", iniPath);
    }
}

// Topology Detection: Count physical cores & P-cores
struct CpuTopology {
    int logicalProcessors = 0;
    int physicalCores = 0;
    int performanceCores = 0;
    int efficiencyCores = 0;
    bool isHybrid = false;
};

CpuTopology DetectCpuTopology() {
    CpuTopology topo = {};
    SYSTEM_INFO si;
    GetNativeSystemInfo(&si);
    topo.logicalProcessors = si.dwNumberOfProcessors;

    DWORD length = 0;
    GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &length);
    if (GetLastError() == ERROR_INSUFFICIENT_BUFFER && length > 0) {
        std::vector<uint8_t> buffer(length);
        if (GetLogicalProcessorInformationEx(RelationProcessorCore, (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)buffer.data(), &length)) {
            uint8_t* ptr = buffer.data();
            uint8_t* end = buffer.data() + length;
            while (ptr < end) {
                PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX info = (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)ptr;
                if (info->Relationship == RelationProcessorCore) {
                    topo.physicalCores++;
                    if (info->Processor.EfficiencyClass > 0) {
                        topo.performanceCores++;
                    } else {
                        topo.efficiencyCores++;
                    }
                }
                ptr += info->Size;
            }
        }
    }

    if (topo.performanceCores > 0 && topo.efficiencyCores > 0) {
        topo.isHybrid = true;
    } else {
        // Uniform architecture (e.g. AMD Ryzen or older Intel)
        topo.performanceCores = topo.physicalCores;
        topo.efficiencyCores = 0;
    }

    return topo;
}

// Original API pointer
typedef DWORD_PTR(WINAPI* PFN_SetThreadAffinityMask)(HANDLE hThread, DWORD_PTR dwThreadAffinityMask);
PFN_SetThreadAffinityMask g_Original_SetThreadAffinityMask = nullptr;

// Hooked SetThreadAffinityMask
DWORD_PTR WINAPI Hooked_SetThreadAffinityMask(HANDLE hThread, DWORD_PTR dwThreadAffinityMask) {
    if (g_Config.bypassAffinity) {
        // Log once in a while or per thread
        Log("[AffinityHook] Intercepted SetThreadAffinityMask (Thread: 0x%p, Mask: 0x%llX) -> BYPASSED. Windows Thread Director will dynamically balance workload.",
            hThread, (unsigned long long)dwThreadAffinityMask);
        return (DWORD_PTR)1; // Return non-zero for success
    }
    return g_Original_SetThreadAffinityMask(hThread, dwThreadAffinityMask);
}

// Helper to patch memory safely
bool PatchMemory(uintptr_t targetAddress, const uint8_t* patchBytes, size_t size) {
    DWORD oldProtect;
    if (!VirtualProtect((LPVOID)targetAddress, size, PAGE_EXECUTE_READWRITE, &oldProtect)) {
        return false;
    }
    memcpy((LPVOID)targetAddress, patchBytes, size);
    VirtualProtect((LPVOID)targetAddress, size, oldProtect, &oldProtect);
    FlushInstructionCache(GetCurrentProcess(), (LPCVOID)targetAddress, size);
    return true;
}

// Main optimization injector
DWORD WINAPI OptimizerThread(LPVOID lpParam) {
    // Small delay to allow the PE loader to finish base relocations and IAT population
    Sleep(100);

    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);

    // Get game directory
    wchar_t gameDir[MAX_PATH];
    wcscpy_s(gameDir, exePath);
    wchar_t* lastSlash = wcsrchr(gameDir, L'\\');
    if (lastSlash) *(lastSlash + 1) = L'\0';

    wchar_t logPath[MAX_PATH];
    swprintf_s(logPath, L"%shoi4_optimizer.log", gameDir);

    wchar_t iniPath[MAX_PATH];
    swprintf_s(iniPath, L"%shoi4_optimizer.ini", gameDir);

    LoadConfig(iniPath);

    if (g_Config.enableLog) {
        _wfopen_s(&g_LogFile, logPath, L"w");
    }

    Log("=== Hearts of Iron IV REA-Guided Performance Optimizer ===");
    Log("Host Binary: %ls", exePath);

    if (!g_Config.enabled) {
        Log("Optimizer is disabled via hoi4_optimizer.ini. Exiting.");
        if (g_LogFile) fclose(g_LogFile);
        return 0;
    }

    uintptr_t baseAddress = (uintptr_t)GetModuleHandle(NULL);
    Log("Image Base Address: 0x%p", (void*)baseAddress);

    // 1. Detect CPU Architecture
    CpuTopology topo = DetectCpuTopology();
    Log("CPU Topology Detected:");
    Log("  - Logical Processors : %d", topo.logicalProcessors);
    Log("  - Physical Cores     : %d", topo.physicalCores);
    if (topo.isHybrid) {
        Log("  - Intel Hybrid Architecture: YES (P-Cores: %d, E-Cores: %d)", topo.performanceCores, topo.efficiencyCores);
    } else {
        Log("  - Uniform Architecture (AMD Ryzen / Standard Intel): %d Cores", topo.physicalCores);
    }

    // 2. Optimization 1: Hook SetThreadAffinityMask (IAT Hook)
    // REA Evidence RVA: 0x273e670
    if (g_Config.bypassAffinity) {
        uintptr_t iatEntry = baseAddress + 0x273e670;
        DWORD oldProtect;
        if (VirtualProtect((LPVOID)iatEntry, sizeof(void*), PAGE_READWRITE, &oldProtect)) {
            g_Original_SetThreadAffinityMask = *(PFN_SetThreadAffinityMask*)iatEntry;
            *(PFN_SetThreadAffinityMask*)iatEntry = Hooked_SetThreadAffinityMask;
            VirtualProtect((LPVOID)iatEntry, sizeof(void*), oldProtect, &oldProtect);
            Log("[SUCCESS] Hooked SetThreadAffinityMask in IAT (RVA: 0x273e670). Anti-E-Core stutter enabled.");
        } else {
            Log("[ERROR] Failed to hook SetThreadAffinityMask in IAT!");
        }
    }

    // 3. Optimization 2: Thread Pool Scaler (Inline Hook at RVA 0x2506f60)
    // REA Evidence: Function at 0x2506f60 returns total logical cores (clamped min 4).
    int targetThreads = g_Config.workerThreadCount;
    if (targetThreads == 0) {
        // Auto mode: If hybrid, match P-cores. If uniform, match min(physicalCores, 8).
        if (topo.isHybrid) {
            targetThreads = topo.performanceCores;
        } else {
            targetThreads = (topo.physicalCores > 8) ? 8 : topo.physicalCores;
            if (targetThreads < 4) targetThreads = 4;
        }
    }

    if (targetThreads > 0) {
        uintptr_t coreClampFn = baseAddress + 0x2506f60;

        // Verify function signature before patching:
        // Expected bytes: 48 83 ec 28 e8 7f cb 04 00
        const uint8_t expectedPrefix[] = { 0x48, 0x83, 0xEC, 0x28 };
        if (memcmp((const void*)coreClampFn, expectedPrefix, sizeof(expectedPrefix)) == 0) {
            // mov eax, targetThreads; ret
            uint8_t patchCode[6];
            patchCode[0] = 0xB8; // mov eax, imm32
            *(uint32_t*)&patchCode[1] = (uint32_t)targetThreads;
            patchCode[5] = 0xC3; // ret

            if (PatchMemory(coreClampFn, patchCode, sizeof(patchCode))) {
                Log("[SUCCESS] Patched Core Count Evaluator (RVA: 0x2506f60) -> Forced %d Threads (1 Main + %d Workers).",
                    targetThreads, targetThreads - 1);
            } else {
                Log("[ERROR] Failed to patch RVA 0x2506f60!");
            }
        } else {
            Log("[WARNING] Signature mismatch at RVA 0x2506f60! Skipped thread count patch to ensure safety.");
        }
    } else {
        Log("[INFO] Thread pool override disabled (using game default).");
    }

    Log("[INIT COMPLETE] Optimizer is active. Game execution proceeding seamlessly.");
    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        // Start initialization thread
        HANDLE hThread = CreateThread(NULL, 0, OptimizerThread, NULL, 0, NULL);
        if (hThread) CloseHandle(hThread);
    }
    return TRUE;
}
