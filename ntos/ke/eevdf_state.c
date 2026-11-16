/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Lifetime of private EEVDF thread state
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#include <ntoskrnl.h>
#include "eevdf_state.h"

#define KI_EEVDF_STATE_BUCKETS 64
#define KI_EEVDF_STATE_TAG 'sFvE'
#define KI_EEVDF_QUANTUM_WEIGHT_LIMIT 36
#define KI_EEVDF_FOREGROUND_WEIGHT 3072

extern ULONG KiCyclesPerClockQuantum;

BOOLEAN KiEevdfEnabled;
static KSPIN_LOCK KiEevdfStateLock;
static PKI_EEVDF_THREAD_STATE KiEevdfStateBuckets[KI_EEVDF_STATE_BUCKETS];
static KI_EEVDF_DOMAIN KiEevdfDomains[MAXIMUM_PROCESSORS][LOW_REALTIME_PRIORITY];
static BOOLEAN KiEevdfDomainInitialized[MAXIMUM_PROCESSORS][LOW_REALTIME_PRIORITY];

static ULONG
KiEevdfThreadBucket(
    _In_ PKTHREAD Thread)
{
    return (ULONG)(((ULONG_PTR)Thread >> 4) & (KI_EEVDF_STATE_BUCKETS - 1));
}

KIRQL
FASTCALL
KiEevdfAcquireStateLock(VOID)
{
    KIRQL OldIrql;

    ASSERT(KeGetCurrentIrql() <= SYNCH_LEVEL);
    OldIrql = KeRaiseIrqlToSynchLevel();
    KeAcquireSpinLockAtDpcLevel(&KiEevdfStateLock);
    return OldIrql;
}

VOID
FASTCALL
KiEevdfReleaseStateLock(
    _In_ KIRQL OldIrql)
{
    ASSERT(KeGetCurrentIrql() >= SYNCH_LEVEL);
    KeReleaseSpinLockFromDpcLevel(&KiEevdfStateLock);
    KeLowerIrql(OldIrql);
}

PKI_EEVDF_THREAD_STATE
FASTCALL
KiEevdfFindThreadStateLocked(
    _In_ PKTHREAD Thread)
{
    PKI_EEVDF_THREAD_STATE State;

    ASSERT(Thread != NULL);
    ASSERT(KeGetCurrentIrql() >= SYNCH_LEVEL);
    for (State = KiEevdfStateBuckets[KiEevdfThreadBucket(Thread)];
         State != NULL;
         State = State->Next)
    {
        if (State->Thread == Thread)
            return State;
    }
    return NULL;
}

static
BOOLEAN
KiEevdfInitialForegroundLocked(
    _In_ PKTHREAD Thread)
{
    PKI_EEVDF_THREAD_STATE State;
    PEPROCESS Process;
    ULONG Bucket;

    if (Thread->Process == NULL)
        return FALSE;
    for (Bucket = 0; Bucket < KI_EEVDF_STATE_BUCKETS; Bucket++)
    {
        for (State = KiEevdfStateBuckets[Bucket]; State != NULL; State = State->Next)
        {
            if (State->Thread->Process == Thread->Process)
                return State->Foreground;
        }
    }
    if (Thread->Process->BasePriority != PROCESS_PRIORITY_NORMAL_FOREGROUND)
        return FALSE;
    Process = CONTAINING_RECORD(Thread->Process, EPROCESS, Pcb);
    return Process->PriorityClass == PROCESS_PRIORITY_CLASS_NORMAL;
}

static
VOID
KiEevdfAccountStateLocked(
    _In_ PKI_EEVDF_THREAD_STATE State,
    _In_ ULONGLONG Cycles)
{
    KI_EEVDF_RESULT Result;

    if (!State->Failed)
    {
        if (Cycles < State->AccountedCycles)
            Result = KiEevdfInvalid;
        else if (State->Entity.Domain != NULL)
            Result = KiEevdfChargeEntity(&State->Entity, Cycles - State->AccountedCycles);
        else
            Result = KiEevdfCharge(&State->Entity.Clock, Cycles - State->AccountedCycles);
        if (Result == KiEevdfOk && State->Entity.Domain != NULL &&
            State->Entity.Clock.VirtualRuntime >= State->Entity.Deadline)
            Result = KiEevdfRenewRequest(&State->Entity);
        if (Result != KiEevdfOk)
        {
            State->Failed = TRUE;
            if (State->Entity.Domain != NULL)
            {
                KiEevdfDiscardEntity(&State->Entity);
                State->Initialized = FALSE;
            }
        }
        else
        {
            State->AccountedCycles = Cycles;
        }
    }
}

static
VOID
KiEevdfRemoveStateLocked(
    _In_ PKI_EEVDF_THREAD_STATE State)
{
    KI_EEVDF_DOMAIN *Domain;
    KI_EEVDF_RESULT Result;

    if (State->Entity.Domain == NULL)
        return;
    Domain = State->Entity.Domain;
    Result = State->Failed ? KiEevdfInvalid : KiEevdfLeaveDomain(&State->Entity);
    if (Result != KiEevdfOk)
    {
        KiEevdfDiscardEntity(&State->Entity);
        State->Failed = TRUE;
    }
    else if (State->Priority > LOW_PRIORITY && State->Priority < LOW_REALTIME_PRIORITY)
    {
        State->PriorityLag[State->Priority] = State->Entity.Lag;
        State->PriorityProgress[State->Priority] = Domain->Progress;
        State->PriorityDomain[State->Priority] = Domain;
        State->PriorityWeight[State->Priority] = State->Entity.Clock.Weight;
        State->PriorityLagValid |= PRIORITY_MASK(State->Priority);
    }
    State->Initialized = FALSE;
}

static
LONGLONG
KiEevdfSavedLagLocked(
    _In_ PKI_EEVDF_THREAD_STATE State,
    _In_ KPRIORITY Priority)
{
    KI_EEVDF_DOMAIN *Domain;
    LONGLONG Lag;
    ULONGLONG Progress, Debt;

    if (!(State->PriorityLagValid & PRIORITY_MASK(Priority)))
        return 0;
    Lag = State->PriorityLag[Priority];
    Domain = State->PriorityDomain[Priority];
    if (Lag >= 0 || Domain == NULL)
        return Lag;
    Progress = Domain->Progress - State->PriorityProgress[Priority];
    Debt = (ULONGLONG)-Lag;
    return Progress >= Debt ? 0 : Lag + (LONGLONG)Progress;
}

static
KI_EEVDF_RESULT
KiEevdfRestoreLagLocked(
    _Inout_ PKI_EEVDF_THREAD_STATE State,
    _In_ KPRIORITY Priority,
    _In_ ULONG Weight)
{
    LONGLONG Lag = KiEevdfSavedLagLocked(State, Priority);
    KI_EEVDF_RESULT Result;

    if (State->PriorityLagValid & PRIORITY_MASK(Priority))
    {
        Result = KiEevdfScaleLag(Lag, State->PriorityWeight[Priority], Weight, &Lag);
        if (Result != KiEevdfOk)
            return Result;
    }
    State->Entity.Lag = Lag;
    return KiEevdfOk;
}

VOID
FASTCALL
KiEevdfAccountThread(
    _In_ PKTHREAD Thread,
    _In_ ULONGLONG Cycles)
{
    PKI_EEVDF_THREAD_STATE State;
    KIRQL OldIrql;

    ASSERT(Thread != NULL);
    OldIrql = KiEevdfAcquireStateLock();
    State = KiEevdfFindThreadStateLocked(Thread);
    if (State != NULL)
        KiEevdfAccountStateLocked(State, Cycles);
    KiEevdfReleaseStateLock(OldIrql);
}

VOID
FASTCALL
KiEevdfCompleteThreadSwitch(
    _In_ PKTHREAD Thread,
    _In_ ULONGLONG Cycles)
{
    PKI_EEVDF_THREAD_STATE State;
    KIRQL OldIrql;

    ASSERT(Thread != NULL);
    OldIrql = KiEevdfAcquireStateLock();
    State = KiEevdfFindThreadStateLocked(Thread);
    if (State != NULL)
    {
        KiEevdfAccountStateLocked(State, Cycles);
        if (State->Entity.Domain != NULL &&
            Thread->State != Ready && Thread->State != Standby && Thread->State != Running)
            KiEevdfRemoveStateLocked(State);
    }
    KiEevdfReleaseStateLock(OldIrql);
    if (Thread->State == Ready || Thread->State == Standby)
        KiEevdfReadyThread(Thread, TRUE);
}

BOOLEAN
FASTCALL
KiEevdfPlaceThread(
    _In_ PKTHREAD Thread,
    _In_ ULONG Processor,
    _In_ KPRIORITY Priority,
    _In_ ULONG Weight,
    _In_ ULONGLONG Slice)
{
    PKI_EEVDF_THREAD_STATE State;
    KI_EEVDF_DOMAIN *Domain;
    KI_EEVDF_ENTITY Previous;
    KI_EEVDF_RESULT Result;
    KIRQL OldIrql;
    BOOLEAN Moved;

    if (Thread == NULL || Processor >= MAXIMUM_PROCESSORS ||
        Priority <= LOW_PRIORITY || Priority >= LOW_REALTIME_PRIORITY ||
        Weight == 0 || Slice == 0)
        return FALSE;
    OldIrql = KiEevdfAcquireStateLock();
    State = KiEevdfFindThreadStateLocked(Thread);
    if (State == NULL || State->Failed)
    {
        KiEevdfReleaseStateLock(OldIrql);
        return FALSE;
    }
    if (State->Foreground && Weight < KI_EEVDF_FOREGROUND_WEIGHT)
        Weight = KI_EEVDF_FOREGROUND_WEIGHT;
    Domain = &KiEevdfDomains[Processor][Priority];
    if (!KiEevdfDomainInitialized[Processor][Priority])
    {
        KiEevdfInitializeDomain(Domain);
        KiEevdfDomainInitialized[Processor][Priority] = TRUE;
    }
    Previous = State->Entity;
    Moved = FALSE;
    if (State->Entity.Domain == NULL)
    {
        Result = KiEevdfRestoreLagLocked(State, Priority, Weight);
        if (Result == KiEevdfOk)
            Result = KiEevdfReweight(&State->Entity.Clock, Weight);
        if (Result == KiEevdfOk)
        {
            State->Entity.Slice = Slice;
            Result = KiEevdfJoinDomain(Domain, &State->Entity);
        }
        if (Result != KiEevdfOk)
            State->Entity = Previous;
    }
    else
    {
        Result = KiEevdfOk;
        if (State->Entity.Domain != Domain && State->Priority != Priority)
        {
            KiEevdfRemoveStateLocked(State);
            if (State->Failed)
                Result = KiEevdfInvalid;
            else
            {
                Result = KiEevdfRestoreLagLocked(State, Priority, Weight);
                if (Result == KiEevdfOk)
                    Result = KiEevdfReweight(&State->Entity.Clock, Weight);
                if (Result == KiEevdfOk)
                {
                    State->Entity.Slice = Slice;
                    Result = KiEevdfJoinDomain(Domain, &State->Entity);
                }
                if (Result != KiEevdfOk)
                    State->Failed = TRUE;
            }
        }
        else if (State->Entity.Domain != Domain)
        {
            Result = KiEevdfMoveDomain(Domain, &State->Entity);
            Moved = (Result == KiEevdfOk);
        }
        if (Result == KiEevdfOk &&
            (State->Entity.Clock.Weight != Weight || State->Entity.Slice != Slice))
            Result = KiEevdfReweightEntity(&State->Entity, Weight, Slice);
        if (Result != KiEevdfOk && Moved)
        {
            KiEevdfDiscardEntity(&State->Entity);
            State->Failed = TRUE;
        }
    }
    if (Result == KiEevdfOk)
    {
        State->Processor = Processor;
        State->Priority = Priority;
        State->Initialized = TRUE;
    }
    else if (State->Failed)
        State->Initialized = FALSE;
    KiEevdfReleaseStateLock(OldIrql);
    return Result == KiEevdfOk;
}

VOID
FASTCALL
KiEevdfReadyThread(
    _In_ PKTHREAD Thread,
    _In_ BOOLEAN SwitchComplete)
{
    PKI_EEVDF_THREAD_STATE State;
    KIRQL OldIrql;
    ULONGLONG Slice;
    ULONG Quantum, Cycles, Weight;

    ASSERT(Thread != NULL);
    if (!SwitchComplete && KiThreadLeavingProcessor(Thread))
        return;
    if (Thread->Priority <= LOW_PRIORITY || Thread->Priority >= LOW_REALTIME_PRIORITY)
    {
        OldIrql = KiEevdfAcquireStateLock();
        State = KiEevdfFindThreadStateLocked(Thread);
        if (State != NULL)
            KiEevdfRemoveStateLocked(State);
        KiEevdfReleaseStateLock(OldIrql);
        return;
    }
    Quantum = Thread->QuantumReset > 0 ? Thread->QuantumReset : 1;
    Cycles = KiCyclesPerClockQuantum != 0 ? KiCyclesPerClockQuantum : 1;
    Weight = min(Quantum, KI_EEVDF_QUANTUM_WEIGHT_LIMIT) * 1024 / KI_EEVDF_QUANTUM_BASE;
    Slice = (ULONGLONG)min(Quantum, KI_EEVDF_QUANTUM_BASE) * Cycles;
    KiEevdfPlaceThread(Thread, Thread->NextProcessor, Thread->Priority, Weight, Slice);
}

VOID
FASTCALL
KiEevdfSetProcessForeground(
    _In_ PKPROCESS Process,
    _In_ BOOLEAN Foreground)
{
    PKI_EEVDF_THREAD_STATE State;
    KIRQL OldIrql;
    ULONG Bucket, Quantum, Cycles, Weight;
    ULONGLONG Slice;

    ASSERT(Process != NULL);
    OldIrql = KiEevdfAcquireStateLock();
    for (Bucket = 0; Bucket < KI_EEVDF_STATE_BUCKETS; Bucket++)
    {
        for (State = KiEevdfStateBuckets[Bucket]; State != NULL; State = State->Next)
        {
            if (State->Thread->Process != Process)
                continue;
            State->Foreground = Foreground;
            if (State->Entity.Domain == NULL || State->Failed)
                continue;
            Quantum = State->Thread->QuantumReset > 0 ? State->Thread->QuantumReset : 1;
            Cycles = KiCyclesPerClockQuantum != 0 ? KiCyclesPerClockQuantum : 1;
            Weight = min(Quantum, KI_EEVDF_QUANTUM_WEIGHT_LIMIT) * 1024 / KI_EEVDF_QUANTUM_BASE;
            if (Foreground && Weight < KI_EEVDF_FOREGROUND_WEIGHT)
                Weight = KI_EEVDF_FOREGROUND_WEIGHT;
            Slice = (ULONGLONG)min(Quantum, KI_EEVDF_QUANTUM_BASE) * Cycles;
            if ((State->Entity.Clock.Weight != Weight || State->Entity.Slice != Slice) &&
                KiEevdfReweightEntity(&State->Entity, Weight, Slice) != KiEevdfOk)
            {
                KiEevdfDiscardEntity(&State->Entity);
                State->Failed = TRUE;
                State->Initialized = FALSE;
            }
        }
    }
    KiEevdfReleaseStateLock(OldIrql);
}

BOOLEAN
FASTCALL
KiEevdfSelectDomainThreadLocked(
    _In_ ULONG Processor,
    _In_ KPRIORITY Priority,
    _Out_ PKTHREAD *Selected)
{
    KI_EEVDF_ENTITY *Entity;
    KI_EEVDF_DOMAIN *Domain;
    PKI_EEVDF_THREAD_STATE State;
    KI_EEVDF_RESULT Result;
    BOOLEAN Valid;

    ASSERT(KeGetCurrentIrql() >= SYNCH_LEVEL);
    if (Selected == NULL || Processor >= MAXIMUM_PROCESSORS ||
        Priority <= LOW_PRIORITY || Priority >= LOW_REALTIME_PRIORITY)
        return FALSE;
    if (!KiEevdfDomainInitialized[Processor][Priority])
        return FALSE;
    Domain = &KiEevdfDomains[Processor][Priority];
    for (Entity = Domain->First; Entity != NULL; Entity = Entity->Next)
    {
        State = CONTAINING_RECORD(Entity, KI_EEVDF_THREAD_STATE, Entity);
        if (!State->Initialized || State->Failed ||
            State->Processor != Processor || State->Priority != Priority ||
            State->Thread->NextProcessor != Processor || State->Thread->Priority != Priority ||
            (State->Thread->State != Running && State->Thread->State != Ready &&
             State->Thread->State != Standby) ||
            (State->Thread->State != Running && KiThreadLeavingProcessor(State->Thread)))
            return FALSE;
    }
    Result = KiEevdfChooseDomain(Domain, &Entity);
    Valid = FALSE;
    if (Result == KiEevdfOk)
    {
        State = CONTAINING_RECORD(Entity, KI_EEVDF_THREAD_STATE, Entity);
        *Selected = State->Thread;
        Valid = TRUE;
    }
    return Valid;
}

BOOLEAN
NTAPI
KiEevdfCreateThreadState(
    _In_ PKTHREAD Thread)
{
    PKI_EEVDF_THREAD_STATE State;
    ULONG Bucket;
    KIRQL OldIrql;

    ASSERT(Thread != NULL);
    ASSERT(KeGetCurrentIrql() <= DISPATCH_LEVEL);

    State = ExAllocatePoolWithTag(NonPagedPoolNx, sizeof(*State), KI_EEVDF_STATE_TAG);
    if (State == NULL)
        return FALSE;

    RtlZeroMemory(State, sizeof(*State));
    State->Thread = Thread;
    State->Entity.Clock.Weight = 1;
    Bucket = KiEevdfThreadBucket(Thread);

    OldIrql = KiEevdfAcquireStateLock();
    if (KiEevdfFindThreadStateLocked(Thread) != NULL)
    {
        KiEevdfReleaseStateLock(OldIrql);
        ExFreePoolWithTag(State, KI_EEVDF_STATE_TAG);
        return FALSE;
    }
    State->Foreground = KiEevdfInitialForegroundLocked(Thread);
    State->Next = KiEevdfStateBuckets[Bucket];
    KiEevdfStateBuckets[Bucket] = State;
    KiEevdfReleaseStateLock(OldIrql);
    return TRUE;
}

VOID
NTAPI
KiEevdfDeleteThreadState(
    _In_ PKTHREAD Thread)
{
    PKI_EEVDF_THREAD_STATE *Link, State;
    KIRQL OldIrql;

    ASSERT(Thread != NULL);
    ASSERT(KeGetCurrentIrql() <= DISPATCH_LEVEL);

    OldIrql = KiEevdfAcquireStateLock();
    Link = &KiEevdfStateBuckets[KiEevdfThreadBucket(Thread)];
    while (*Link != NULL && (*Link)->Thread != Thread)
        Link = &(*Link)->Next;
    State = *Link;
    if (State != NULL)
    {
        KiEevdfDiscardEntity(&State->Entity);
        *Link = State->Next;
    }
    KiEevdfReleaseStateLock(OldIrql);

    if (State != NULL)
        ExFreePoolWithTag(State, KI_EEVDF_STATE_TAG);
}
