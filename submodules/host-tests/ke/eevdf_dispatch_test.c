/*
 * PROJECT:     LiberNT host-native tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Exercise EEVDF selection on dispatcher ready queues
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#include <ntoskrnl.h>
#include <stdio.h>
#include <stdlib.h>
#include "eevdf_dispatch.h"
#include "eevdf_state.h"

#define CHECK(Condition) do { if (!(Condition)) { \
    fprintf(stderr, "line %d: %s\n", __LINE__, #Condition); exit(1); \
} } while (0)

ULONG KiCyclesPerClockQuantum = 10;

KIRQL KeRaiseIrqlToSynchLevel(void) { return DISPATCH_LEVEL; }
void KeLowerIrql(KIRQL OldIrql) { CHECK(OldIrql == DISPATCH_LEVEL); }
void KeAcquireSpinLockAtDpcLevel(KSPIN_LOCK *Lock) { CHECK(*Lock == 0); *Lock = 1; }
void KeReleaseSpinLockFromDpcLevel(KSPIN_LOCK *Lock) { CHECK(*Lock == 1); *Lock = 0; }
void *ExAllocatePoolWithTag(int Pool, size_t Size, ULONG Tag)
{
    (void)Pool;
    (void)Tag;
    return malloc(Size);
}
void ExFreePoolWithTag(void *Pointer, ULONG Tag)
{
    (void)Tag;
    free(Pointer);
}

typedef struct _TEST_CONTEXT
{
    unsigned Calls;
    unsigned FailAt;
} TEST_CONTEXT;

static BOOLEAN FASTCALL
Query(PKTHREAD Thread, PVOID Context, KI_EEVDF_CANDIDATE *Candidate)
{
    TEST_CONTEXT *Test = Context;
    Test->Calls++;
    if (Test->FailAt == Test->Calls)
        return FALSE;
    Candidate->VirtualRuntime = Thread->VirtualRuntime;
    Candidate->Deadline = Thread->Deadline;
    Candidate->Weight = Thread->Weight;
    return TRUE;
}

static void Insert(PLIST_ENTRY Head, PLIST_ENTRY Entry)
{
    Entry->Blink = Head->Blink;
    Entry->Flink = Head;
    Head->Blink->Flink = Entry;
    Head->Blink = Entry;
}

int main(void)
{
    KPRCB Prcb = {0};
    KTHREAD Threads[3] = {0};
    PKTHREAD Selected = NULL;
    TEST_CONTEXT Context = {0};
    PLIST_ENTRY Head = &Prcb.DispatcherReadyListHead[8];
    unsigned Index;

    Prcb.PrcbLock = 1;
    Head->Flink = Head;
    Head->Blink = Head;
    CHECK(!KiEevdfSelectReady(&Prcb, 8, Query, &Context, &Selected));
    for (Index = 0; Index < 3; Index++)
    {
        Threads[Index].State = Ready;
        Threads[Index].Priority = 8;
        Threads[Index].Weight = 1;
        Threads[Index].VirtualRuntime = (Index + 1) * 10;
        Threads[Index].Deadline = 100 / (Index + 1);
        Insert(Head, &Threads[Index].WaitListEntry);
    }
    CHECK(KiEevdfSelectReady(&Prcb, 8, Query, &Context, &Selected));
    CHECK(Selected == &Threads[1]);
    CHECK(Head->Flink == &Threads[0].WaitListEntry &&
          Head->Blink == &Threads[2].WaitListEntry);
    Selected = &Threads[2];
    Threads[1].Leaving = TRUE;
    CHECK(!KiEevdfSelectReady(&Prcb, 8, Query, &Context, &Selected));
    CHECK(Selected == &Threads[2]);
    Threads[1].Leaving = FALSE;
    Context.Calls = 0;
    Context.FailAt = 7;
    CHECK(!KiEevdfSelectReady(&Prcb, 8, Query, &Context, &Selected));
    CHECK(Selected == &Threads[2]);
    Context.FailAt = 0;
    CHECK(!KiEevdfSelectReady(&Prcb, 16, Query, &Context, &Selected));
    CHECK(Selected == &Threads[2]);
    Threads[2].VirtualRuntime = UINT64_MAX;
    Threads[2].Weight = 2;
    CHECK(!KiEevdfSelectReady(&Prcb, 8, Query, &Context, &Selected));
    CHECK(Selected == &Threads[2]);
    Threads[2].VirtualRuntime = 30;
    Threads[2].Weight = 1;
    KiEevdfEnabled = TRUE;
    for (Index = 0; Index < 3; Index++)
    {
        KIRQL OldIrql;
        PKI_EEVDF_THREAD_STATE State;
        CHECK(KiEevdfCreateThreadState(&Threads[Index]));
        OldIrql = KiEevdfAcquireStateLock();
        State = KiEevdfFindThreadStateLocked(&Threads[Index]);
        CHECK(State != NULL);
        State->Initialized = TRUE;
        State->Priority = 8;
        State->Processor = 0;
        State->Entity.Clock.VirtualRuntime = Threads[Index].VirtualRuntime;
        State->Entity.Deadline = Threads[Index].Deadline;
        KiEevdfReleaseStateLock(OldIrql);
    }
    CHECK(KiEevdfTrySelectReady(&Prcb, 8, &Selected));
    CHECK(Selected == &Threads[1]);
    {
        KIRQL OldIrql = KiEevdfAcquireStateLock();
        PKI_EEVDF_THREAD_STATE State = KiEevdfFindThreadStateLocked(&Threads[1]);
        State->Processor = 1;
        KiEevdfReleaseStateLock(OldIrql);
        CHECK(!KiEevdfTrySelectReady(&Prcb, 8, &Selected));
        OldIrql = KiEevdfAcquireStateLock();
        State->Processor = 0;
        State->Failed = TRUE;
        KiEevdfReleaseStateLock(OldIrql);
        CHECK(!KiEevdfTrySelectReady(&Prcb, 8, &Selected));
        OldIrql = KiEevdfAcquireStateLock();
        State->Failed = FALSE;
        KiEevdfReleaseStateLock(OldIrql);
    }
    CHECK(!KiEevdfTrySelectReady(&Prcb, 16, &Selected));
    KiEevdfDeleteThreadState(&Threads[1]);
    Selected = &Threads[2];
    CHECK(!KiEevdfTrySelectReady(&Prcb, 8, &Selected));
    CHECK(Selected == &Threads[2]);
    KiEevdfEnabled = FALSE;
    CHECK(!KiEevdfTrySelectReady(&Prcb, 8, &Selected));
    KiEevdfDeleteThreadState(&Threads[0]);
    KiEevdfDeleteThreadState(&Threads[2]);
    {
        KTHREAD Current = {0}, Waiting = {0}, Stray = {0};
        PLIST_ENTRY QuantumHead = &Prcb.DispatcherReadyListHead[7];

        QuantumHead->Flink = QuantumHead->Blink = QuantumHead;
        Current.State = Running;
        Current.Priority = Waiting.Priority = 7;
        Current.QuantumReset = Waiting.QuantumReset = 6;
        Current.Affinity = Waiting.Affinity = 1;
        Waiting.State = Ready;
        Prcb.CurrentThread = &Current;
        Prcb.ReadySummary = PRIORITY_MASK(7);
        Insert(QuantumHead, &Waiting.WaitListEntry);
        CHECK(KiEevdfCreateThreadState(&Current));
        CHECK(KiEevdfCreateThreadState(&Waiting));
        CHECK(KiEevdfPlaceThread(&Current, 0, 7, 1024, 60));
        CHECK(KiEevdfPlaceThread(&Waiting, 0, 7, 1024, 60));
        KiEevdfEnabled = TRUE;
        CHECK(KiEevdfTrySelectQuantum(&Prcb, &Current, &Selected) && Selected == &Current);
        Current.CycleTime = 20;
        CHECK(KiEevdfTrySelectQuantum(&Prcb, &Current, &Selected) && Selected == &Waiting);
        Waiting.Leaving = TRUE;
        Selected = &Current;
        CHECK(!KiEevdfTrySelectQuantum(&Prcb, &Current, &Selected) && Selected == &Current);
        Waiting.Leaving = FALSE;
        Stray.State = Ready;
        Stray.Priority = 7;
        CHECK(KiEevdfCreateThreadState(&Stray));
        CHECK(KiEevdfPlaceThread(&Stray, 0, 7, 1024, 60));
        CHECK(!KiEevdfTrySelectQuantum(&Prcb, &Current, &Selected) && Selected == &Current);
        KiEevdfDeleteThreadState(&Stray);
        Prcb.ReadySummary |= PRIORITY_MASK(8);
        CHECK(!KiEevdfTrySelectQuantum(&Prcb, &Current, &Selected) && Selected == &Current);
        Prcb.ReadySummary = PRIORITY_MASK(7);
        KiEevdfDeleteThreadState(&Waiting);
        CHECK(!KiEevdfTrySelectQuantum(&Prcb, &Current, &Selected) && Selected == &Current);
        KiEevdfDeleteThreadState(&Current);
    }
    return 0;
}
