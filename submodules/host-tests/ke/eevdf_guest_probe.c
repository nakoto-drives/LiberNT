/*
 * PROJECT:     LiberNT scheduler guest validation
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Exercise scheduling, wakeups, priorities and processor migration
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#include <windows.h>
#include <stdio.h>
#include <stdint.h>

typedef struct _PROBE_WORKER
{
    HANDLE Start;
    volatile LONG Stop;
    volatile LONG64 Count;
} PROBE_WORKER;

typedef struct _PROBE_WAKE
{
    HANDLE Request;
    HANDLE Reply;
    unsigned Count;
} PROBE_WAKE;

static DWORD WINAPI
ProbeBusy(void *Parameter)
{
    PROBE_WORKER *Worker = Parameter;
    volatile LONG64 Count = 0;
    unsigned Index;

    WaitForSingleObject(Worker->Start, INFINITE);
    do
    {
        for (Index = 0; Index < 4096; Index++)
            Count++;
    } while (!InterlockedCompareExchange(&Worker->Stop, 0, 0));
    InterlockedExchange64(&Worker->Count, Count);
    return 0;
}

static DWORD WINAPI
ProbeWake(void *Parameter)
{
    PROBE_WAKE *Wake = Parameter;
    unsigned Index;

    for (Index = 0; Index < Wake->Count; Index++)
    {
        if (WaitForSingleObject(Wake->Request, 5000) != WAIT_OBJECT_0)
            return 1;
        if (!SetEvent(Wake->Reply))
            return 1;
    }
    return 0;
}

static void
ProbeReport(const char *Text)
{
    OutputDebugStringA(Text);
    printf("%s", Text);
    fflush(stdout);
}

int main(void)
{
    HANDLE Threads[2], Start;
    PROBE_WORKER Workers[2] = {{0}};
    PROBE_WAKE Wake;
    DWORD_PTR ProcessMask, SystemMask, First, Second, Remaining;
    ULONGLONG Before, Elapsed, WakeMaximum = 0;
    LONG64 Same0 = 0, Same1 = 0, High = 0, Low = 0, Migrated = 0;
    unsigned Index, Failed = 0, MigrationCalls = 0, PriorityCalls = 0;
    DWORD Status;
    char Message[512];

    ProbeReport("EEVDF_PROBE begin\n");
    if (!GetProcessAffinityMask(GetCurrentProcess(), &ProcessMask, &SystemMask) || !ProcessMask)
        return 2;
    First = ProcessMask & (~ProcessMask + 1);
    Remaining = ProcessMask & ~First;
    Second = Remaining & (~Remaining + 1);
    if (!SetThreadAffinityMask(GetCurrentThread(), First) ||
        !SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL))
        return 3;

    Start = CreateEventA(NULL, TRUE, FALSE, NULL);
    if (!Start)
        return 4;
    for (Index = 0; Index < 2; Index++)
    {
        Workers[Index].Start = Start;
        Threads[Index] = CreateThread(NULL, 0, ProbeBusy, &Workers[Index], 0, NULL);
        if (!Threads[Index] || !SetThreadAffinityMask(Threads[Index], First) ||
            !SetThreadPriorityBoost(Threads[Index], TRUE))
            return 5;
    }
    SetEvent(Start);
    Sleep(2000);
    for (Index = 0; Index < 2; Index++)
        InterlockedExchange(&Workers[Index].Stop, 1);
    Status = WaitForMultipleObjects(2, Threads, TRUE, 5000);
    Same0 = Workers[0].Count;
    Same1 = Workers[1].Count;
    if (Status != WAIT_OBJECT_0 || Same0 <= 0 || Same1 <= 0 ||
        Same0 > Same1 * 4 || Same1 > Same0 * 4)
        Failed++;
    for (Index = 0; Index < 2; Index++) CloseHandle(Threads[Index]);
    CloseHandle(Start);

    ZeroMemory(Workers, sizeof(Workers));
    Start = CreateEventA(NULL, TRUE, FALSE, NULL);
    if (!Start) return 6;
    for (Index = 0; Index < 2; Index++)
    {
        Workers[Index].Start = Start;
        Threads[Index] = CreateThread(NULL, 0, ProbeBusy, &Workers[Index], 0, NULL);
        if (!Threads[Index] || !SetThreadAffinityMask(Threads[Index], First) ||
            !SetThreadPriorityBoost(Threads[Index], TRUE) ||
            !SetThreadPriority(Threads[Index], Index == 0 ? THREAD_PRIORITY_HIGHEST : THREAD_PRIORITY_LOWEST))
            return 7;
    }
    SetEvent(Start);
    Sleep(300);
    for (Index = 0; Index < 2; Index++) InterlockedExchange(&Workers[Index].Stop, 1);
    Status = WaitForMultipleObjects(2, Threads, TRUE, 5000);
    High = Workers[0].Count;
    Low = Workers[1].Count;
    if (Status != WAIT_OBJECT_0 || High <= 0 || High <= Low * 4)
        Failed++;
    for (Index = 0; Index < 2; Index++) CloseHandle(Threads[Index]);
    CloseHandle(Start);

    if (Second)
    {
        ZeroMemory(&Workers[0], sizeof(Workers[0]));
        Start = CreateEventA(NULL, TRUE, FALSE, NULL);
        Workers[0].Start = Start;
        Threads[0] = CreateThread(NULL, 0, ProbeBusy, &Workers[0], 0, NULL);
        if (!Start || !Threads[0]) return 8;
        SetEvent(Start);
        for (Index = 0; Index < 100; Index++)
        {
            if (!SetThreadAffinityMask(Threads[0], (Index & 1) ? First : Second))
                Failed++;
            else
                MigrationCalls++;
            if (!SetThreadPriority(Threads[0], (Index & 1) ? THREAD_PRIORITY_NORMAL : THREAD_PRIORITY_ABOVE_NORMAL))
                Failed++;
            else
                PriorityCalls++;
            Sleep(1);
        }
        InterlockedExchange(&Workers[0].Stop, 1);
        Status = WaitForSingleObject(Threads[0], 5000);
        Migrated = Workers[0].Count;
        if (Status != WAIT_OBJECT_0 || Migrated <= 0 || MigrationCalls != 100 || PriorityCalls != 100)
            Failed++;
        CloseHandle(Threads[0]);
        CloseHandle(Start);
    }

    ZeroMemory(&Workers[0], sizeof(Workers[0]));
    Start = CreateEventA(NULL, TRUE, TRUE, NULL);
    Wake.Request = CreateEventA(NULL, FALSE, FALSE, NULL);
    Wake.Reply = CreateEventA(NULL, FALSE, FALSE, NULL);
    Wake.Count = 32;
    Workers[0].Start = Start;
    Threads[0] = CreateThread(NULL, 0, ProbeBusy, &Workers[0], 0, NULL);
    Threads[1] = CreateThread(NULL, 0, ProbeWake, &Wake, 0, NULL);
    if (!Start || !Wake.Request || !Wake.Reply || !Threads[0] || !Threads[1]) return 9;
    if (!SetThreadAffinityMask(Threads[0], First) || !SetThreadAffinityMask(Threads[1], First))
        return 10;
    for (Index = 0; Index < Wake.Count; Index++)
    {
        Before = GetTickCount64();
        SetEvent(Wake.Request);
        Status = WaitForSingleObject(Wake.Reply, 5000);
        Elapsed = GetTickCount64() - Before;
        if (Elapsed > WakeMaximum) WakeMaximum = Elapsed;
        if (Status != WAIT_OBJECT_0)
        {
            Failed++;
            break;
        }
    }
    InterlockedExchange(&Workers[0].Stop, 1);
    if (WaitForMultipleObjects(2, Threads, TRUE, 6000) != WAIT_OBJECT_0)
        Failed++;
    for (Index = 0; Index < 2; Index++) CloseHandle(Threads[Index]);
    CloseHandle(Start);
    CloseHandle(Wake.Request);
    CloseHandle(Wake.Reply);

    _snprintf(Message, sizeof(Message),
              "EEVDF_PROBE result=%s same=%lld,%lld priority=%lld,%lld migration=%u,%lld priority_changes=%u wake_max_ms=%llu failures=%u\n",
              Failed ? "FAIL" : "PASS", (long long)Same0, (long long)Same1,
              (long long)High, (long long)Low, MigrationCalls, (long long)Migrated, PriorityCalls,
              (unsigned long long)WakeMaximum, Failed);
    Message[sizeof(Message) - 1] = '\0';
    ProbeReport(Message);
    return Failed ? 1 : 0;
}
