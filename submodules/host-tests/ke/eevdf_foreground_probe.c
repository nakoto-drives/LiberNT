/*
 * PROJECT:     LiberNT scheduler guest validation
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Compare foreground and background service at one priority
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#include <windows.h>
#include <winternl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct _FOREGROUND_SHARED
{
    volatile LONG Ready;
    volatile LONG Start;
    volatile LONG Stop;
    LONG Reserved;
    volatile LONG64 Counts[2];
} FOREGROUND_SHARED;

typedef NTSTATUS (NTAPI *SET_PROCESS_INFORMATION)(HANDLE, PROCESSINFOCLASS, PVOID, ULONG);
typedef NTSTATUS (NTAPI *QUERY_THREAD_INFORMATION)(HANDLE, THREADINFOCLASS, PVOID, ULONG, PULONG);

typedef struct _PROBE_THREAD_BASIC_INFORMATION
{
    NTSTATUS ExitStatus;
    PVOID TebBaseAddress;
    CLIENT_ID ClientId;
    ULONG_PTR AffinityMask;
    LONG Priority;
    LONG BasePriority;
} PROBE_THREAD_BASIC_INFORMATION;

static const char SharedName[] = "LiberntEevdfForegroundProbe";

static int
RunChild(unsigned Index)
{
    HANDLE Mapping;
    FOREGROUND_SHARED *Shared;
    volatile LONG64 Count = 0;
    unsigned Step;

    Mapping = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, SharedName);
    if (!Mapping || Index > 1)
        return 2;
    Shared = MapViewOfFile(Mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(*Shared));
    if (!Shared)
        return 3;
    InterlockedIncrement(&Shared->Ready);
    while (!InterlockedCompareExchange(&Shared->Start, 0, 0))
        Sleep(1);
    do
    {
        for (Step = 0; Step < 4096; Step++) Count++;
    } while (!InterlockedCompareExchange(&Shared->Stop, 0, 0));
    InterlockedExchange64(&Shared->Counts[Index], Count);
    UnmapViewOfFile(Shared);
    CloseHandle(Mapping);
    return 0;
}

int main(int argc, char **argv)
{
    HANDLE Mapping;
    FOREGROUND_SHARED *Shared;
    PROCESS_INFORMATION Processes[2] = {{0}};
    STARTUPINFOA Startup = {0};
    union { FARPROC Generic; SET_PROCESS_INFORMATION Typed; } SetInformation;
    union { FARPROC Generic; QUERY_THREAD_INFORMATION Typed; } QueryThread;
    PROBE_THREAD_BASIC_INFORMATION ThreadInfo[2];
    DWORD_PTR ProcessMask, SystemMask, Processor;
    DWORD ExitCode[2] = {1, 1};
    BYTE Foreground = TRUE;
    char Path[MAX_PATH], Command[MAX_PATH + 32], Message[256];
    unsigned Index, Created = 0;
    int Failed = 0;
    ULONGLONG Start;

    if (argc == 3 && !strcmp(argv[1], "child"))
        return RunChild((unsigned)atoi(argv[2]));
    OutputDebugStringA("EEVDF_FOREGROUND begin\n");
    SetInformation.Generic = GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtSetInformationProcess");
    QueryThread.Generic = GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtQueryInformationThread");
    if (!SetInformation.Generic || !QueryThread.Generic ||
        !GetProcessAffinityMask(GetCurrentProcess(), &ProcessMask, &SystemMask) || !ProcessMask)
        return 2;
    Processor = ProcessMask & (~ProcessMask + 1);
    if (!SetThreadAffinityMask(GetCurrentThread(), Processor) ||
        !SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL))
        return 3;
    Mapping = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, sizeof(*Shared), SharedName);
    if (!Mapping || GetLastError() == ERROR_ALREADY_EXISTS)
        return 4;
    Shared = MapViewOfFile(Mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(*Shared));
    if (!Shared || !GetModuleFileNameA(NULL, Path, sizeof(Path)))
        return 5;
    ZeroMemory(Shared, sizeof(*Shared));
    Startup.cb = sizeof(Startup);
    for (Index = 0; Index < 2; Index++)
    {
        _snprintf(Command, sizeof(Command), "\"%s\" child %u", Path, Index);
        Command[sizeof(Command) - 1] = '\0';
        if (!CreateProcessA(NULL, Command, NULL, NULL, FALSE, CREATE_SUSPENDED | CREATE_NO_WINDOW,
                            NULL, NULL, &Startup, &Processes[Index]))
        {
            Failed = 1;
            break;
        }
        Created++;
        if (!SetProcessAffinityMask(Processes[Index].hProcess, Processor) ||
            !SetThreadPriorityBoost(Processes[Index].hThread, TRUE))
            Failed = 1;
    }
    if (!Failed && SetInformation.Typed(Processes[0].hProcess, ProcessForegroundInformation,
                                        &Foreground, sizeof(Foreground)) < 0)
        Failed = 1;
    if (!Failed && (!SetThreadPriority(Processes[0].hThread, THREAD_PRIORITY_BELOW_NORMAL) ||
                    !SetThreadPriority(Processes[1].hThread, THREAD_PRIORITY_NORMAL)))
        Failed = 1;
    ZeroMemory(ThreadInfo, sizeof(ThreadInfo));
    if (!Failed)
    {
        for (Index = 0; Index < 2; Index++)
        {
            if (QueryThread.Typed(Processes[Index].hThread, ThreadBasicInformation,
                                  &ThreadInfo[Index], sizeof(ThreadInfo[Index]), NULL) < 0)
                Failed = 1;
        }
        if (ThreadInfo[0].Priority != ThreadInfo[1].Priority)
            Failed = 1;
    }
    for (Index = 0; Index < Created; Index++) ResumeThread(Processes[Index].hThread);
    Start = GetTickCount64();
    while (!Failed && InterlockedCompareExchange(&Shared->Ready, 0, 0) != 2 &&
           GetTickCount64() - Start < 5000)
        Sleep(10);
    if (Shared->Ready != 2) Failed = 1;
    InterlockedExchange(&Shared->Start, 1);
    if (!Failed) Sleep(2000);
    InterlockedExchange(&Shared->Stop, 1);
    for (Index = 0; Index < Created; Index++)
    {
        if (WaitForSingleObject(Processes[Index].hProcess, 5000) != WAIT_OBJECT_0)
        {
            TerminateProcess(Processes[Index].hProcess, 1);
            Failed = 1;
        }
        if (!GetExitCodeProcess(Processes[Index].hProcess, &ExitCode[Index]) || ExitCode[Index] != 0)
            Failed = 1;
        CloseHandle(Processes[Index].hThread);
        CloseHandle(Processes[Index].hProcess);
    }
    if (Shared->Counts[0] <= 0 || Shared->Counts[1] <= 0) Failed = 1;
    _snprintf(Message, sizeof(Message), "EEVDF_FOREGROUND result=%s foreground=%lld background=%lld priority=%ld,%ld base=%ld,%ld\n",
              Failed ? "FAIL" : "PASS", (long long)Shared->Counts[0], (long long)Shared->Counts[1],
              ThreadInfo[0].Priority, ThreadInfo[1].Priority,
              ThreadInfo[0].BasePriority, ThreadInfo[1].BasePriority);
    Message[sizeof(Message) - 1] = '\0';
    OutputDebugStringA(Message);
    printf("%s", Message);
    UnmapViewOfFile(Shared);
    CloseHandle(Mapping);
    return Failed;
}
