import ctypes
from ctypes import wintypes
import sys
import time

kernel32 = ctypes.windll.kernel32
psapi = ctypes.windll.psapi

PROCESS_ALL_ACCESS = 0x1F0FFF

def get_hoi4_pid_and_base():
    # Use Toolhelp32Snapshot to find hoi4.exe and its base address
    TH32CS_SNAPPROCESS = 0x00000002
    TH32CS_SNAPMODULE = 0x00000008
    TH32CS_SNAPMODULE32 = 0x00000010
    
    class PROCESSENTRY32W(ctypes.Structure):
        _fields_ = [
            ('dwSize', wintypes.DWORD),
            ('cntUsage', wintypes.DWORD),
            ('th32ProcessID', wintypes.DWORD),
            ('th32DefaultHeapID', ctypes.c_size_t),
            ('th32ModuleID', wintypes.DWORD),
            ('cntThreads', wintypes.DWORD),
            ('th32ParentProcessID', wintypes.DWORD),
            ('pcPriClassBase', wintypes.LONG),
            ('dwFlags', wintypes.DWORD),
            ('szExeFile', wintypes.WCHAR * wintypes.MAX_PATH)
        ]

    class MODULEENTRY32W(ctypes.Structure):
        _fields_ = [
            ('dwSize', wintypes.DWORD),
            ('th32ModuleID', wintypes.DWORD),
            ('th32ProcessID', wintypes.DWORD),
            ('GlblcntUsage', wintypes.DWORD),
            ('ProccntUsage', wintypes.DWORD),
            ('modBaseAddr', ctypes.c_void_p),
            ('modBaseSize', wintypes.DWORD),
            ('hModule', wintypes.HMODULE),
            ('szModule', wintypes.WCHAR * 256),
            ('szExePath', wintypes.WCHAR * wintypes.MAX_PATH)
        ]

    snap = kernel32.CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0)
    pe = PROCESSENTRY32W()
    pe.dwSize = ctypes.sizeof(PROCESSENTRY32W)
    
    target_pid = None
    if kernel32.Process32FirstW(snap, ctypes.byref(pe)):
        while True:
            if pe.szExeFile.lower() == "hoi4.exe":
                target_pid = pe.th32ProcessID
                break
            if not kernel32.Process32NextW(snap, ctypes.byref(pe)):
                break
    kernel32.CloseHandle(snap)
    
    if not target_pid:
        return None, None
        
    mod_snap = kernel32.CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, target_pid)
    me = MODULEENTRY32W()
    me.dwSize = ctypes.sizeof(MODULEENTRY32W)
    base_addr = None
    if kernel32.Module32FirstW(mod_snap, ctypes.byref(me)):
        while True:
            if me.szModule.lower() == "hoi4.exe":
                base_addr = me.modBaseAddr
                break
            if not kernel32.Module32NextW(mod_snap, ctypes.byref(me)):
                break
    kernel32.CloseHandle(mod_snap)
    return target_pid, base_addr

def patch_hoi4(target_threads=8):
    print("=" * 60)
    print("  Hearts of Iron IV REA-Guided Memory Patcher (Zero Disk Mutation)")
    print("=" * 60)
    print("Searching for running hoi4.exe process...")
    pid, base = get_hoi4_pid_and_base()
    if not pid:
        print("[!] hoi4.exe is not currently running. Please launch the game first, then run this tool.")
        return
        
    print(f"[+] Found hoi4.exe (PID: {pid}) Base Address: {hex(base)}")
    hProc = kernel32.OpenProcess(PROCESS_ALL_ACCESS, False, pid)
    if not hProc:
        print(f"[-] Failed to open process (error code: {kernel32.GetLastError()})")
        return
        
    try:
        # Patch Target: RVA 0x2506f60 (Core Count Evaluator)
        target_fn = base + 0x2506f60
        old_protect = wintypes.DWORD()
        
        # Verify signature
        buf = (ctypes.c_uint8 * 4)()
        bytesRead = ctypes.c_size_t()
        kernel32.ReadProcessMemory(hProc, ctypes.c_void_p(target_fn), buf, 4, ctypes.byref(bytesRead))
        expected = bytes([0x48, 0x83, 0xec, 0x28])
        if bytes(buf) != expected:
            print(f"[!] Signature mismatch at {hex(target_fn)}. Expected {expected.hex()}, found {bytes(buf).hex()}. Aborting patch for safety.")
            return

        # Patch: mov eax, target_threads; ret (B8 <imm32> C3)
        patch = bytearray([0xB8, 0, 0, 0, 0, 0xC3])
        patch[1:5] = target_threads.to_bytes(4, 'little')
        
        PAGE_EXECUTE_READWRITE = 0x40
        if kernel32.VirtualProtectEx(hProc, ctypes.c_void_p(target_fn), len(patch), PAGE_EXECUTE_READWRITE, ctypes.byref(old_protect)):
            bytesWritten = ctypes.c_size_t()
            kernel32.WriteProcessMemory(hProc, ctypes.c_void_p(target_fn), (ctypes.c_uint8 * len(patch))(*patch), len(patch), ctypes.byref(bytesWritten))
            kernel32.VirtualProtectEx(hProc, ctypes.c_void_p(target_fn), len(patch), old_protect, ctypes.byref(old_protect))
            print(f"[SUCCESS] Patched Thread Count Evaluator at {hex(target_fn)} -> Set to {target_threads} Threads.")
        else:
            print("[-] VirtualProtectEx failed on function target.")
            
    finally:
        kernel32.CloseHandle(hProc)

if __name__ == "__main__":
    threads = 8
    if len(sys.argv) > 1:
        try:
            threads = int(sys.argv[1])
        except:
            pass
    patch_hoi4(threads)

