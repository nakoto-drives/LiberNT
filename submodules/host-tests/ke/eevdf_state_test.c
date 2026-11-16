/*
 * PROJECT:     LiberNT host-native tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Exercise EEVDF state lifetime and synchronization
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#include <ntoskrnl.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include "eevdf_state.h"

#define CHECK(Condition) do { if (!(Condition)) { \
    fprintf(stderr, "line %d: %s\n", __LINE__, #Condition); abort(); \
} } while (0)

ULONG KiCyclesPerClockQuantum = 10;

static atomic_flag LockFlag = ATOMIC_FLAG_INIT;
static atomic_int Allocations;
static atomic_int FailAllocation;

KIRQL KeRaiseIrqlToSynchLevel(void) { return DISPATCH_LEVEL; }
void KeLowerIrql(KIRQL OldIrql) { CHECK(OldIrql == DISPATCH_LEVEL); }
void KeAcquireSpinLockAtDpcLevel(KSPIN_LOCK *Lock)
{
    (void)Lock;
    while (atomic_flag_test_and_set_explicit(&LockFlag, memory_order_acquire)) {}
}
void KeReleaseSpinLockFromDpcLevel(KSPIN_LOCK *Lock)
{
    (void)Lock;
    atomic_flag_clear_explicit(&LockFlag, memory_order_release);
}
void *ExAllocatePoolWithTag(int Pool, size_t Size, ULONG Tag)
{
    void *Pointer;
    (void)Pool;
    (void)Tag;
    if (atomic_load(&FailAllocation)) return NULL;
    Pointer = malloc(Size);
    if (Pointer != NULL) atomic_fetch_add(&Allocations, 1);
    return Pointer;
}
void ExFreePoolWithTag(void *Pointer, ULONG Tag)
{
    (void)Tag;
    free(Pointer);
    atomic_fetch_sub(&Allocations, 1);
}

static void *Exercise(void *Argument)
{
    KTHREAD *Thread = Argument;
    unsigned Index;

    for (Index = 0; Index < 5000; Index++)
    {
        PKI_EEVDF_THREAD_STATE State;
        KIRQL OldIrql;
        CHECK(KiEevdfCreateThreadState(Thread));
        OldIrql = KiEevdfAcquireStateLock();
        State = KiEevdfFindThreadStateLocked(Thread);
        CHECK(State != NULL && State->Thread == Thread && State->Entity.Clock.Weight == 1);
        State->AccountedCycles = Index;
        KiEevdfReleaseStateLock(OldIrql);
        KiEevdfDeleteThreadState(Thread);
    }
    return NULL;
}

static void *ExerciseDomains(void *Argument)
{
    KTHREAD *Thread = Argument;
    unsigned Index;

    CHECK(KiEevdfCreateThreadState(Thread));
    for (Index = 0; Index < 5000; Index++)
    {
        CHECK(KiEevdfPlaceThread(Thread, Index % 2, 9, Index % 3 + 1, 100));
        KiEevdfAccountThread(Thread, Index + 1);
    }
    KiEevdfDeleteThreadState(Thread);
    return NULL;
}

int main(void)
{
    KTHREAD Threads[4] = {0};
    KI_EEVDF_DOMAIN Domain;
    EPROCESS ForegroundProcess = {0}, BackgroundProcess = {0};
    PKI_EEVDF_THREAD_STATE State;
    PKTHREAD Selected;
    pthread_t Workers[4];
    KIRQL OldIrql;
    unsigned Index;

    atomic_store(&FailAllocation, 1);
    CHECK(!KiEevdfCreateThreadState(&Threads[0]));
    atomic_store(&FailAllocation, 0);
    CHECK(KiEevdfCreateThreadState(&Threads[0]));
    CHECK(!KiEevdfCreateThreadState(&Threads[0]));
    CHECK(atomic_load(&Allocations) == 1);
    KiEevdfAccountThread(&Threads[0], 10);
    KiEevdfAccountThread(&Threads[0], 20);
    OldIrql = KiEevdfAcquireStateLock();
    CHECK(KiEevdfFindThreadStateLocked(&Threads[0])->Entity.Clock.VirtualRuntime == 20480);
    CHECK(KiEevdfFindThreadStateLocked(&Threads[0])->AccountedCycles == 20);
    KiEevdfReleaseStateLock(OldIrql);
    KiEevdfAccountThread(&Threads[0], 19);
    OldIrql = KiEevdfAcquireStateLock();
    CHECK(KiEevdfFindThreadStateLocked(&Threads[0])->Failed);
    CHECK(KiEevdfFindThreadStateLocked(&Threads[0])->AccountedCycles == 20);
    KiEevdfReleaseStateLock(OldIrql);
    KiEevdfDeleteThreadState(&Threads[0]);
    KiEevdfDeleteThreadState(&Threads[0]);
    OldIrql = KiEevdfAcquireStateLock();
    CHECK(KiEevdfFindThreadStateLocked(&Threads[0]) == NULL);
    KiEevdfReleaseStateLock(OldIrql);
    CHECK(atomic_load(&Allocations) == 0);

    KiEevdfInitializeDomain(&Domain);
    CHECK(KiEevdfCreateThreadState(&Threads[0]));
    OldIrql = KiEevdfAcquireStateLock();
    State = KiEevdfFindThreadStateLocked(&Threads[0]);
    State->Entity.Slice = 100;
    CHECK(KiEevdfJoinDomain(&Domain, &State->Entity) == KiEevdfOk);
    KiEevdfReleaseStateLock(OldIrql);
    KiEevdfAccountThread(&Threads[0], 10);
    OldIrql = KiEevdfAcquireStateLock();
    CHECK(State->Entity.Clock.VirtualRuntime == Domain.Baseline + 10240);
    CHECK(State->AccountedCycles == 10 && !State->Failed);
    KiEevdfReleaseStateLock(OldIrql);
    KiEevdfAccountThread(&Threads[0], UINT64_MAX);
    OldIrql = KiEevdfAcquireStateLock();
    CHECK(State->Failed && State->Entity.Domain == NULL && Domain.First == NULL);
    CHECK(State->AccountedCycles == 10);
    KiEevdfReleaseStateLock(OldIrql);
    KiEevdfDeleteThreadState(&Threads[0]);
    CHECK(Domain.First == NULL && atomic_load(&Allocations) == 0);

    CHECK(KiEevdfCreateThreadState(&Threads[0]));
    CHECK(KiEevdfCreateThreadState(&Threads[1]));
    CHECK(KiEevdfCreateThreadState(&Threads[2]));
    Threads[0].Priority = Threads[1].Priority = Threads[2].Priority = 8;
    Threads[0].State = Threads[1].State = Threads[2].State = Ready;
    Threads[0].NextProcessor = Threads[1].NextProcessor = 0;
    Threads[2].NextProcessor = 1;
    CHECK(KiEevdfPlaceThread(&Threads[0], 0, 8, 1, 100));
    CHECK(KiEevdfPlaceThread(&Threads[1], 0, 8, 1, 100));
    CHECK(KiEevdfPlaceThread(&Threads[2], 1, 8, 1, 100));
    KiEevdfAccountThread(&Threads[0], 20);
    CHECK(KiEevdfPlaceThread(&Threads[0], 1, 8, 2, 100));
    Threads[0].NextProcessor = 1;
    Selected = NULL;
    OldIrql = KiEevdfAcquireStateLock();
    CHECK(KiEevdfSelectDomainThreadLocked(0, 8, &Selected) && Selected == &Threads[1]);
    CHECK(KiEevdfSelectDomainThreadLocked(1, 8, &Selected) && Selected == &Threads[2]);
    CHECK(!KiEevdfSelectDomainThreadLocked(2, 8, &Selected) && Selected == &Threads[2]);
    Threads[2].Priority = 7;
    CHECK(!KiEevdfSelectDomainThreadLocked(1, 8, &Selected) && Selected == &Threads[2]);
    Threads[2].Priority = 8;
    Threads[2].Leaving = TRUE;
    CHECK(!KiEevdfSelectDomainThreadLocked(1, 8, &Selected) && Selected == &Threads[2]);
    Threads[2].Leaving = FALSE;
    Threads[0].Priority = 7;
    CHECK(!KiEevdfSelectDomainThreadLocked(1, 8, &Selected) && Selected == &Threads[2]);
    Threads[0].Priority = 8;
    State = KiEevdfFindThreadStateLocked(&Threads[0]);
    CHECK(State->Initialized && State->Processor == 1 && State->Priority == 8);
    CHECK(State->Entity.Clock.Weight == 2 && State->Entity.Lag == -10240);
    CHECK(State->Entity.Clock.VirtualRuntime == (UINT64_C(1) << 60) + 15360);
    KiEevdfReleaseStateLock(OldIrql);
    CHECK(!KiEevdfPlaceThread(&Threads[0], MAXIMUM_PROCESSORS, 8, 1, 100));
    CHECK(!KiEevdfPlaceThread(&Threads[0], 1, LOW_REALTIME_PRIORITY, 1, 100));
    CHECK(!KiEevdfPlaceThread(&Threads[0], 1, 8, 0, 100));
    CHECK(!KiEevdfPlaceThread(&Threads[0], 0, 8, 1, UINT64_MAX));
    OldIrql = KiEevdfAcquireStateLock();
    CHECK(State->Failed && !State->Initialized && State->Entity.Domain == NULL);
    CHECK(KiEevdfFindThreadStateLocked(&Threads[1])->Entity.Domain != NULL);
    KiEevdfReleaseStateLock(OldIrql);
    KiEevdfDeleteThreadState(&Threads[0]);
    KiEevdfDeleteThreadState(&Threads[1]);
    KiEevdfDeleteThreadState(&Threads[2]);
    CHECK(atomic_load(&Allocations) == 0);

    Threads[3].Priority = 8;
    Threads[3].NextProcessor = 2;
    Threads[3].State = Ready;
    Threads[3].QuantumReset = 6;
    Threads[3].Leaving = TRUE;
    CHECK(KiEevdfCreateThreadState(&Threads[3]));
    KiEevdfReadyThread(&Threads[3], FALSE);
    OldIrql = KiEevdfAcquireStateLock();
    State = KiEevdfFindThreadStateLocked(&Threads[3]);
    CHECK(!State->Initialized && State->Entity.Domain == NULL);
    KiEevdfReleaseStateLock(OldIrql);
    KiEevdfCompleteThreadSwitch(&Threads[3], 4);
    OldIrql = KiEevdfAcquireStateLock();
    CHECK(State->Initialized && State->Entity.Domain != NULL && State->AccountedCycles == 4);
    CHECK(State->Entity.Clock.Weight == 1024 && State->Entity.Slice == 60);
    CHECK(State->Entity.Deadline == (UINT64_C(1) << 60) + 60);
    KiEevdfReleaseStateLock(OldIrql);
    KiEevdfAccountThread(&Threads[3], 64);
    OldIrql = KiEevdfAcquireStateLock();
    CHECK(State->Entity.Clock.VirtualRuntime == (UINT64_C(1) << 60) + 60);
    CHECK(State->Entity.Deadline == (UINT64_C(1) << 60) + 120);
    KiEevdfReleaseStateLock(OldIrql);
    Threads[3].QuantumReset = 18;
    KiEevdfReadyThread(&Threads[3], TRUE);
    OldIrql = KiEevdfAcquireStateLock();
    CHECK(State->Entity.Clock.Weight == 3072 && State->Entity.Slice == 60);
    CHECK(State->Entity.Deadline == (UINT64_C(1) << 60) + 80);
    KiEevdfReleaseStateLock(OldIrql);
    Threads[3].QuantumReset = 127;
    KiEevdfReadyThread(&Threads[3], TRUE);
    OldIrql = KiEevdfAcquireStateLock();
    CHECK(State->Entity.Clock.Weight == 6144 && State->Entity.Slice == 60);
    CHECK(State->Entity.Deadline == (UINT64_C(1) << 60) + 70);
    KiEevdfReleaseStateLock(OldIrql);
    Threads[3].Priority = LOW_REALTIME_PRIORITY;
    KiEevdfReadyThread(&Threads[3], TRUE);
    OldIrql = KiEevdfAcquireStateLock();
    CHECK(!State->Initialized && State->Entity.Domain == NULL);
    KiEevdfReleaseStateLock(OldIrql);
    Threads[3].Priority = 8;
    KiEevdfReadyThread(&Threads[3], TRUE);
    OldIrql = KiEevdfAcquireStateLock();
    CHECK(State->Initialized && State->Entity.Domain != NULL);
    KiEevdfReleaseStateLock(OldIrql);
    KiEevdfDeleteThreadState(&Threads[3]);
    Threads[3].Leaving = FALSE;
    CHECK(atomic_load(&Allocations) == 0);

    Threads[0].Priority = Threads[1].Priority = 11;
    Threads[0].State = Threads[1].State = Ready;
    Threads[0].NextProcessor = Threads[1].NextProcessor = 0;
    CHECK(KiEevdfCreateThreadState(&Threads[0]));
    CHECK(KiEevdfCreateThreadState(&Threads[1]));
    CHECK(KiEevdfPlaceThread(&Threads[0], 0, 11, 1024, 100));
    CHECK(KiEevdfPlaceThread(&Threads[1], 0, 11, 1024, 100));
    KiEevdfAccountThread(&Threads[0], 20);
    Threads[0].Priority = 12;
    CHECK(KiEevdfPlaceThread(&Threads[0], 0, 12, 1024, 100));
    OldIrql = KiEevdfAcquireStateLock();
    State = KiEevdfFindThreadStateLocked(&Threads[0]);
    CHECK((State->PriorityLagValid & PRIORITY_MASK(11)) && State->PriorityLag[11] == -10);
    KiEevdfReleaseStateLock(OldIrql);
    Threads[0].Priority = 11;
    CHECK(KiEevdfPlaceThread(&Threads[0], 0, 11, 1024, 100));
    OldIrql = KiEevdfAcquireStateLock();
    CHECK(State->Entity.Clock.VirtualRuntime == (UINT64_C(1) << 60) + 20);
    CHECK((State->PriorityLagValid & PRIORITY_MASK(12)) && State->PriorityLag[12] == 0);
    KiEevdfReleaseStateLock(OldIrql);
    Threads[0].State = DeferredReady;
    KiEevdfCompleteThreadSwitch(&Threads[0], 20);
    OldIrql = KiEevdfAcquireStateLock();
    CHECK(State->Entity.Domain == NULL && State->PriorityLag[11] == -10);
    KiEevdfReleaseStateLock(OldIrql);
    Threads[0].State = Ready;
    CHECK(KiEevdfPlaceThread(&Threads[0], 0, 11, 1024, 100));
    OldIrql = KiEevdfAcquireStateLock();
    CHECK(State->Entity.Clock.VirtualRuntime == (UINT64_C(1) << 60) + 20);
    KiEevdfReleaseStateLock(OldIrql);
    Threads[0].State = DeferredReady;
    KiEevdfCompleteThreadSwitch(&Threads[0], 20);
    KiEevdfAccountThread(&Threads[1], 5);
    Threads[0].State = Ready;
    CHECK(KiEevdfPlaceThread(&Threads[0], 0, 11, 1024, 100));
    OldIrql = KiEevdfAcquireStateLock();
    CHECK(State->Entity.Clock.VirtualRuntime == (UINT64_C(1) << 60) + 15);
    KiEevdfReleaseStateLock(OldIrql);
    Threads[0].State = DeferredReady;
    KiEevdfCompleteThreadSwitch(&Threads[0], 20);
    KiEevdfAccountThread(&Threads[1], 25);
    Threads[0].State = Ready;
    CHECK(KiEevdfPlaceThread(&Threads[0], 0, 11, 1024, 100));
    OldIrql = KiEevdfAcquireStateLock();
    CHECK(State->Entity.Clock.VirtualRuntime == (UINT64_C(1) << 60) + 25);
    KiEevdfReleaseStateLock(OldIrql);
    KiEevdfDeleteThreadState(&Threads[0]);
    KiEevdfDeleteThreadState(&Threads[1]);
    CHECK(atomic_load(&Allocations) == 0);

    Threads[0].Priority = Threads[1].Priority = Threads[2].Priority = 13;
    Threads[0].State = Threads[1].State = Threads[2].State = Ready;
    Threads[0].NextProcessor = Threads[1].NextProcessor = 0;
    Threads[2].NextProcessor = 1;
    for (Index = 0; Index < 3; Index++) CHECK(KiEevdfCreateThreadState(&Threads[Index]));
    CHECK(KiEevdfPlaceThread(&Threads[0], 0, 13, 1024, 100));
    CHECK(KiEevdfPlaceThread(&Threads[1], 0, 13, 1024, 100));
    CHECK(KiEevdfPlaceThread(&Threads[2], 1, 13, 1024, 100));
    KiEevdfAccountThread(&Threads[0], 20);
    OldIrql = KiEevdfAcquireStateLock();
    State = KiEevdfFindThreadStateLocked(&Threads[0]);
    State->Entity.Domain->Progress = UINT64_MAX - 2;
    KiEevdfReleaseStateLock(OldIrql);
    Threads[0].State = DeferredReady;
    KiEevdfCompleteThreadSwitch(&Threads[0], 20);
    KiEevdfAccountThread(&Threads[1], 5);
    Threads[0].State = Ready;
    Threads[0].NextProcessor = 1;
    CHECK(KiEevdfPlaceThread(&Threads[0], 1, 13, 1024, 100));
    OldIrql = KiEevdfAcquireStateLock();
    CHECK(State->Entity.Lag == -5);
    CHECK(State->Entity.Clock.VirtualRuntime == (UINT64_C(1) << 60) + 10);
    KiEevdfReleaseStateLock(OldIrql);
    for (Index = 0; Index < 3; Index++) KiEevdfDeleteThreadState(&Threads[Index]);
    CHECK(atomic_load(&Allocations) == 0);

    Threads[0].Priority = Threads[1].Priority = 14;
    Threads[0].State = Threads[1].State = Ready;
    Threads[0].NextProcessor = Threads[1].NextProcessor = 0;
    CHECK(KiEevdfCreateThreadState(&Threads[0]));
    CHECK(KiEevdfCreateThreadState(&Threads[1]));
    CHECK(KiEevdfPlaceThread(&Threads[0], 0, 14, 1024, 100));
    CHECK(KiEevdfPlaceThread(&Threads[1], 0, 14, 1024, 100));
    KiEevdfAccountThread(&Threads[0], 20);
    Threads[0].State = DeferredReady;
    KiEevdfCompleteThreadSwitch(&Threads[0], 20);
    Threads[0].State = Ready;
    CHECK(KiEevdfPlaceThread(&Threads[0], 0, 14, 2048, 100));
    OldIrql = KiEevdfAcquireStateLock();
    State = KiEevdfFindThreadStateLocked(&Threads[0]);
    CHECK(State->Entity.Lag == -5 && State->Entity.Clock.VirtualRuntime == (UINT64_C(1) << 60) + 15);
    KiEevdfReleaseStateLock(OldIrql);
    KiEevdfDeleteThreadState(&Threads[0]);
    KiEevdfDeleteThreadState(&Threads[1]);
    CHECK(atomic_load(&Allocations) == 0);

    ForegroundProcess.Pcb.BasePriority = PROCESS_PRIORITY_NORMAL_FOREGROUND;
    ForegroundProcess.PriorityClass = PROCESS_PRIORITY_CLASS_NORMAL;
    BackgroundProcess.Pcb.BasePriority = 8;
    BackgroundProcess.PriorityClass = PROCESS_PRIORITY_CLASS_NORMAL;
    Threads[0].Process = &ForegroundProcess.Pcb;
    Threads[1].Process = &BackgroundProcess.Pcb;
    Threads[0].Priority = Threads[1].Priority = 8;
    Threads[0].State = Threads[1].State = Ready;
    Threads[0].NextProcessor = Threads[1].NextProcessor = 0;
    Threads[0].QuantumReset = Threads[1].QuantumReset = 6;
    CHECK(KiEevdfCreateThreadState(&Threads[0]));
    CHECK(KiEevdfCreateThreadState(&Threads[1]));
    KiEevdfReadyThread(&Threads[0], TRUE);
    KiEevdfReadyThread(&Threads[1], TRUE);
    OldIrql = KiEevdfAcquireStateLock();
    State = KiEevdfFindThreadStateLocked(&Threads[0]);
    CHECK(State->Foreground && State->Entity.Clock.Weight == 3072);
    CHECK(KiEevdfFindThreadStateLocked(&Threads[1])->Entity.Clock.Weight == 1024);
    KiEevdfReleaseStateLock(OldIrql);
    KiEevdfSetProcessForeground(&ForegroundProcess.Pcb, FALSE);
    OldIrql = KiEevdfAcquireStateLock();
    CHECK(!State->Foreground && State->Entity.Clock.Weight == 1024);
    KiEevdfReleaseStateLock(OldIrql);
    Threads[2].Process = &ForegroundProcess.Pcb;
    CHECK(KiEevdfCreateThreadState(&Threads[2]));
    OldIrql = KiEevdfAcquireStateLock();
    CHECK(!KiEevdfFindThreadStateLocked(&Threads[2])->Foreground);
    KiEevdfReleaseStateLock(OldIrql);
    KiEevdfSetProcessForeground(&ForegroundProcess.Pcb, TRUE);
    OldIrql = KiEevdfAcquireStateLock();
    CHECK(State->Foreground && State->Entity.Clock.Weight == 3072);
    CHECK(KiEevdfFindThreadStateLocked(&Threads[2])->Foreground);
    CHECK(KiEevdfFindThreadStateLocked(&Threads[1])->Entity.Clock.Weight == 1024);
    KiEevdfReleaseStateLock(OldIrql);
    KiEevdfDeleteThreadState(&Threads[0]);
    KiEevdfDeleteThreadState(&Threads[1]);
    KiEevdfDeleteThreadState(&Threads[2]);
    Threads[0].Process = Threads[1].Process = Threads[2].Process = NULL;
    CHECK(atomic_load(&Allocations) == 0);

    for (Index = 0; Index < 4; Index++)
        CHECK(pthread_create(&Workers[Index], NULL, Exercise, &Threads[Index]) == 0);
    for (Index = 0; Index < 4; Index++)
        CHECK(pthread_join(Workers[Index], NULL) == 0);
    CHECK(atomic_load(&Allocations) == 0);
    for (Index = 0; Index < 4; Index++)
        CHECK(pthread_create(&Workers[Index], NULL, ExerciseDomains, &Threads[Index]) == 0);
    for (Index = 0; Index < 4; Index++)
        CHECK(pthread_join(Workers[Index], NULL) == 0);
    CHECK(atomic_load(&Allocations) == 0);
    return 0;
}
