/* Brute Force PC loader - bflaunch.exe.
 * Linked at 0x10000 (/FIXED) with a large zero-filled section, so its image covers the whole Xbox
 * game range 0x10000-0x4C5000 from the moment the process starts. It loads bfcore.dll and hands
 * over; bfcore then copies default.xbe over this image (this file's own code is overwritten, which is
 * fine because control never comes back here). */
typedef unsigned int u32; typedef int BOOL; typedef void *HMODULE;
#define WINAPI __stdcall
__declspec(dllimport) HMODULE WINAPI LoadLibraryA(const char *);
__declspec(dllimport) void *WINAPI GetProcAddress(HMODULE, const char *);
__declspec(dllimport) void WINAPI ExitProcess(u32);
__declspec(dllimport) int WINAPI MessageBoxA(void *, const char *, const char *, u32);
__declspec(dllimport) void *WINAPI CreateMutexA(void *, BOOL, const char *);
__declspec(dllimport) u32 WINAPI GetLastError(void);
__declspec(dllimport) BOOL WINAPI MoveFileExA(const char *, const char *, u32);
__declspec(dllimport) BOOL WINAPI DeleteFileA(const char *);

/* Reserves the rest of the Xbox range (image size >= 0x4B5000). Zero-filled, so the file stays small. */
#pragma section(".xbox", read, write, execute)
__declspec(allocate(".xbox")) unsigned char g_xbox_space[0x4C0000];
unsigned char *volatile g_keep = g_xbox_space;

void __stdcall start(void) {
    /* only one copy at a time: two copies running together can overload the PC */
    CreateMutexA(0, 1, "BruteForcePCLoader_OneCopy");
    if (GetLastError() == 183 /* ERROR_ALREADY_EXISTS */) {
        MessageBoxA(0, "The Brute Force loader is already running.\nClose its window first, then start it again.", "Brute Force loader", 0x30);
        ExitProcess(1);
    }
    DeleteFileA("stop_loader.txt");   /* a leftover stop request (see the watcher in kernel.c) */
    MoveFileExA("bfcore_new.dll", "bfcore.dll", 1 /* replace */);   /* take a newer core if one was delivered */
    HMODULE core = LoadLibraryA("bfcore.dll");
    void (__cdecl *main_)(const char *) = core ? (void (__cdecl *)(const char *))GetProcAddress(core, "BfMain") : 0;
    if (!main_) { MessageBoxA(0, "bfcore.dll is missing (it must be next to bflaunch.exe).", "Brute Force loader", 0); ExitProcess(1); }
    g_keep[0] = 0;
    main_(0);
    ExitProcess(0);
}
