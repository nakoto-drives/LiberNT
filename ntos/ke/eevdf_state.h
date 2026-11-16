/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Private per-thread state for the EEVDF scheduler
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#pragma once

#include "eevdf_domain.h"

extern BOOLEAN KiEevdfEnabled;

typedef struct _KI_EEVDF_THREAD_STATE
{
    struct _KI_EEVDF_THREAD_STATE *Next;
    PKTHREAD Thread;
    KI_EEVDF_ENTITY Entity;
    LONGLONG PriorityLag[LOW_REALTIME_PRIORITY];
    ULONGLONG PriorityProgress[LOW_REALTIME_PRIORITY];
    KI_EEVDF_DOMAIN *PriorityDomain[LOW_REALTIME_PRIORITY];
    ULONG PriorityWeight[LOW_REALTIME_PRIORITY];
    ULONGLONG AccountedCycles;
    ULONG PriorityLagValid;
    ULONG Processor;
    KPRIORITY Priority;
    BOOLEAN Initialized;
    BOOLEAN Failed;
    BOOLEAN Foreground;
} KI_EEVDF_THREAD_STATE, *PKI_EEVDF_THREAD_STATE;

BOOLEAN
NTAPI
KiEevdfCreateThreadState(
    _In_ PKTHREAD Thread);

VOID
NTAPI
KiEevdfDeleteThreadState(
    _In_ PKTHREAD Thread);

VOID
FASTCALL
KiEevdfAccountThread(
    _In_ PKTHREAD Thread,
    _In_ ULONGLONG Cycles);

VOID
FASTCALL
KiEevdfCompleteThreadSwitch(
    _In_ PKTHREAD Thread,
    _In_ ULONGLONG Cycles);

VOID
FASTCALL
KiEevdfReadyThread(
    _In_ PKTHREAD Thread,
    _In_ BOOLEAN SwitchComplete);

VOID
FASTCALL
KiEevdfSetProcessForeground(
    _In_ PKPROCESS Process,
    _In_ BOOLEAN Foreground);

BOOLEAN
FASTCALL
KiEevdfPlaceThread(
    _In_ PKTHREAD Thread,
    _In_ ULONG Processor,
    _In_ KPRIORITY Priority,
    _In_ ULONG Weight,
    _In_ ULONGLONG Slice);

BOOLEAN
FASTCALL
KiEevdfSelectDomainThreadLocked(
    _In_ ULONG Processor,
    _In_ KPRIORITY Priority,
    _Out_ PKTHREAD *Selected);

KIRQL
FASTCALL
KiEevdfAcquireStateLock(VOID);

VOID
FASTCALL
KiEevdfReleaseStateLock(
    _In_ KIRQL OldIrql);

PKI_EEVDF_THREAD_STATE
FASTCALL
KiEevdfFindThreadStateLocked(
    _In_ PKTHREAD Thread);
