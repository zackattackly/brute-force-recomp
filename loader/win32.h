/* Minimal Win32 declarations for the freestanding loader (no SDK needed). */
#pragma once
typedef unsigned int u32; typedef unsigned short u16; typedef unsigned char u8; typedef int s32;
typedef unsigned long long u64; typedef long long s64;
typedef void *HANDLE; typedef int BOOL; typedef u32 DWORD;
#define WINAPI __stdcall
#define IMP __declspec(dllimport)
IMP void *WINAPI VirtualAlloc(void *, u32, DWORD, DWORD);
IMP BOOL WINAPI VirtualFree(void *, u32, DWORD);
IMP u32 WINAPI VirtualQuery(const void *, void *, u32);
IMP BOOL WINAPI VirtualProtect(void *, u32, DWORD, DWORD *);
IMP HANDLE WINAPI CreateFileA(const char *, DWORD, DWORD, void *, DWORD, DWORD, HANDLE);
IMP BOOL WINAPI ReadFile(HANDLE, void *, DWORD, DWORD *, void *);
IMP BOOL WINAPI WriteFile(HANDLE, const void *, DWORD, DWORD *, void *);
IMP DWORD WINAPI GetFileSize(HANDLE, DWORD *);
IMP BOOL WINAPI CloseHandle(HANDLE);
IMP void WINAPI ExitProcess(u32);
IMP BOOL WINAPI TerminateProcess(HANDLE, u32);
#define STOP(code) TerminateProcess((HANDLE)-1, code)   /* ends at once, without waiting on the game's threads */
IMP DWORD WINAPI GetLastError(void);
IMP DWORD WINAPI GetModuleFileNameA(void *, char *, DWORD);
IMP char *WINAPI GetCommandLineA(void);
IMP HANDLE WINAPI GetProcessHeap(void);
IMP void *WINAPI HeapAlloc(HANDLE, DWORD, u32);
IMP BOOL WINAPI HeapFree(HANDLE, DWORD, void *);
IMP u32 WINAPI HeapSize(HANDLE, DWORD, const void *);
IMP HANDLE WINAPI CreateEventA(void *, BOOL, BOOL, const char *);
IMP BOOL WINAPI SetEvent(HANDLE);
IMP BOOL WINAPI ResetEvent(HANDLE);
IMP DWORD WINAPI WaitForSingleObject(HANDLE, DWORD);
IMP void WINAPI Sleep(DWORD);
IMP BOOL WINAPI QueryPerformanceCounter(s64 *);
IMP BOOL WINAPI QueryPerformanceFrequency(s64 *);
IMP void WINAPI GetSystemTimeAsFileTime(u64 *);
IMP DWORD WINAPI GetTickCount(void);
IMP DWORD WINAPI GetCurrentThreadId(void);
IMP void *WINAPI SetUnhandledExceptionFilter(void *);
IMP void *WINAPI AddVectoredExceptionHandler(u32, void *);
IMP void WINAPI RtlUnwind(void *, void *, void *, void *);
IMP BOOL WINAPI FlushFileBuffers(HANDLE);
#define MEM_COMMIT 0x1000
#define MEM_RESERVE 0x2000
#define MEM_RELEASE 0x8000
#define MEM_DECOMMIT 0x4000
#define PAGE_READWRITE 0x04
#define PAGE_EXECUTE_READWRITE 0x40
#define GENERIC_READ 0x80000000
#define GENERIC_WRITE 0x40000000
#define INVALID_HANDLE ((HANDLE)-1)
IMP DWORD WINAPI TlsAlloc(void);
IMP BOOL WINAPI TlsSetValue(DWORD, void *);
IMP void *WINAPI TlsGetValue(DWORD);
IMP HANDLE WINAPI CreateThread(void *, u32, void *, void *, DWORD, DWORD *);
IMP void WINAPI ExitThread(DWORD);
IMP DWORD WINAPI ResumeThread(HANDLE);
IMP HANDLE WINAPI CreateMutexA(void *, BOOL, const char *);
IMP BOOL WINAPI ReleaseMutex(HANDLE);
IMP DWORD WINAPI WaitForMultipleObjects(DWORD, const HANDLE *, BOOL, DWORD);
