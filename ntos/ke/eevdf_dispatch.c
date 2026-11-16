/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Select an eligible thread from a priority ready queue
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#include <ntoskrnl.h>
#include "eevdf_dispatch.h"
#include "eevdf_state.h"

#if DBG
static LONG KiEevdfReportedSelection;
#endif

static
BOOLEAN
FASTCALL
KiEevdfQueryThreadState(
    _In_ PKTHREAD Thread,
    _In_opt_ PVOID Context,
    _Out_ KI_EEVDF_CANDIDATE *Candidate)
{
    PKI_EEVDF_THREAD_STATE State;
    PKPRCB Prcb = Context;

    State = KiEevdfFindThreadStateLocked(Thread);
    if (State == NULL || !State->Initialized || State->Failed ||
        State->Priority != Thread->Priority || State->Processor != Prcb->Number ||
        Thread->NextProcessor != Prcb->Number)
        return FALSE;
    Candidate->VirtualRuntime = State->Entity.Clock.VirtualRuntime;
    Candidate->Deadline = State->Entity.Deadline;
    Candidate->Weight = State->Entity.Clock.Weight;
    return TRUE;
}

BOOLEAN
FASTCALL
KiEevdfTrySelectReady(
    _In_ PKPRCB Prcb,
    _In_ KPRIORITY Priority,
    _Out_ PKTHREAD *Selected)
{
    KIRQL OldIrql;
    BOOLEAN Result;

    if (!KiEevdfEnabled || Priority <= LOW_PRIORITY || Priority >= LOW_REALTIME_PRIORITY)
        return FALSE;
    OldIrql = KiEevdfAcquireStateLock();
    Result = KiEevdfSelectReady(Prcb, Priority, KiEevdfQueryThreadState, Prcb, Selected);
    KiEevdfReleaseStateLock(OldIrql);
    return Result;
}

BOOLEAN
FASTCALL
KiEevdfTrySelectQuantum(
    _In_ PKPRCB Prcb,
    _In_ PKTHREAD Current,
    _Out_ PKTHREAD *Selected)
{
    PKI_EEVDF_THREAD_STATE CurrentState, State;
    KI_EEVDF_ENTITY *Entity;
    PKTHREAD Thread, Choice;
    PLIST_ENTRY Head, Entry;
    KPRIORITY Priority;
    KIRQL OldIrql;
    ULONG QueueCount = 0, DomainCount = 0;
    BOOLEAN CurrentFound = FALSE, Result = FALSE;

    ASSERT(KeGetCurrentIrql() >= DISPATCH_LEVEL);
    ASSERT(Prcb != NULL && Prcb->PrcbLock != 0);
    if (!KiEevdfEnabled || Prcb == NULL || Current == NULL || Selected == NULL ||
        Prcb->CurrentThread != Current || Current->State != Running)
        return FALSE;
    Priority = Current->Priority;
    if (Priority <= LOW_PRIORITY || Priority >= LOW_REALTIME_PRIORITY ||
        !(Prcb->ReadySummary & PRIORITY_MASK(Priority)) ||
        (Prcb->ReadySummary >> (Priority + 1)) != 0)
        return FALSE;

    KiEevdfAccountThread(Current, (ULONGLONG)InterlockedCompareExchange64((PLONG64)&Current->CycleTime, 0, 0));
    KiEevdfReadyThread(Current, TRUE);
    OldIrql = KiEevdfAcquireStateLock();
    CurrentState = KiEevdfFindThreadStateLocked(Current);
    if (CurrentState == NULL || !CurrentState->Initialized || CurrentState->Failed ||
        CurrentState->Entity.Domain == NULL || CurrentState->Priority != Priority ||
        CurrentState->Processor != Prcb->Number)
        goto Exit;
    Head = &Prcb->DispatcherReadyListHead[Priority];
    for (Entry = Head->Flink; Entry != Head; Entry = Entry->Flink)
    {
        Thread = CONTAINING_RECORD(Entry, KTHREAD, WaitListEntry);
        State = KiEevdfFindThreadStateLocked(Thread);
        if (State == NULL || !State->Initialized || State->Failed ||
            State->Entity.Domain != CurrentState->Entity.Domain ||
            Thread->State != Ready || Thread->Priority != Priority ||
            Thread->NextProcessor != Prcb->Number || KiThreadLeavingProcessor(Thread) ||
            !(KiThreadAffinityMask(Thread) & AFFINITY_MASK(Prcb->Number)))
            goto Exit;
        if (QueueCount == MAXULONG)
            goto Exit;
        QueueCount++;
    }
    for (Entity = CurrentState->Entity.Domain->First; Entity != NULL; Entity = Entity->Next)
    {
        State = CONTAINING_RECORD(Entity, KI_EEVDF_THREAD_STATE, Entity);
        if (State == CurrentState)
        {
            CurrentFound = TRUE;
            continue;
        }
        if (State->Thread->State != Ready || DomainCount == MAXULONG)
            goto Exit;
        DomainCount++;
    }
    if (!CurrentFound || QueueCount != DomainCount)
        goto Exit;
    if (KiEevdfSelectDomainThreadLocked(Prcb->Number, Priority, &Choice))
    {
        *Selected = Choice;
        Result = TRUE;
    }
Exit:
    KiEevdfReleaseStateLock(OldIrql);
#if DBG
    if (Result && InterlockedCompareExchange(&KiEevdfReportedSelection, 1, 0) == 0)
        DbgPrint("EEVDF: virtual-deadline selection active\n");
#endif
    return Result;
}

BOOLEAN
FASTCALL
KiEevdfSelectReady(
    _In_ PKPRCB Prcb,
    _In_ KPRIORITY Priority,
    _In_ PKI_EEVDF_QUERY_READY Query,
    _In_opt_ PVOID Context,
    _Out_ PKTHREAD *Selected)
{
    KI_EEVDF_CANDIDATE Candidate;
    KI_EEVDF_STATS Stats;
    PLIST_ENTRY Head, Entry;
    PKTHREAD Thread, Best;
    ULONGLONG BestDeadline;
    BOOLEAN First;

    ASSERT(KeGetCurrentIrql() >= DISPATCH_LEVEL);
    ASSERT(Prcb != NULL && Prcb->PrcbLock != 0);

    if (Prcb == NULL || Query == NULL || Selected == NULL ||
        Priority <= LOW_PRIORITY || Priority >= LOW_REALTIME_PRIORITY)
        return FALSE;

    Head = &Prcb->DispatcherReadyListHead[Priority];
    Stats.Minimum = 0;
    Stats.TotalWeight = 0;
    Stats.WeightedDistance = 0;
    First = TRUE;
    for (Entry = Head->Flink; Entry != Head; Entry = Entry->Flink)
    {
        Thread = CONTAINING_RECORD(Entry, KTHREAD, WaitListEntry);
        ASSERT(Thread->State == Ready && Thread->Priority == Priority);
        if (KiThreadLeavingProcessor(Thread) || !Query(Thread, Context, &Candidate) ||
            Candidate.Weight == 0)
            return FALSE;
        if (First || Candidate.VirtualRuntime < Stats.Minimum)
            Stats.Minimum = Candidate.VirtualRuntime;
        First = FALSE;
    }
    if (First)
        return FALSE;

    for (Entry = Head->Flink; Entry != Head; Entry = Entry->Flink)
    {
        Thread = CONTAINING_RECORD(Entry, KTHREAD, WaitListEntry);
        if (!Query(Thread, Context, &Candidate) ||
            KiEevdfAccumulate(&Stats, &Candidate) != KiEevdfOk)
            return FALSE;
    }

    Best = NULL;
    BestDeadline = 0;
    for (Entry = Head->Flink; Entry != Head; Entry = Entry->Flink)
    {
        Thread = CONTAINING_RECORD(Entry, KTHREAD, WaitListEntry);
        if (!Query(Thread, Context, &Candidate))
            return FALSE;
        if (KiEevdfIsEligible(&Stats, &Candidate) &&
            (Best == NULL || Candidate.Deadline < BestDeadline))
        {
            Best = Thread;
            BestDeadline = Candidate.Deadline;
        }
    }
    if (Best == NULL)
        return FALSE;

    *Selected = Best;
    return TRUE;
}
