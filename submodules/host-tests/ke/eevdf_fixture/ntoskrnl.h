/*
 * PROJECT:     LiberNT host-native tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Ready queue types for the EEVDF dispatcher test
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#pragma once

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define _In_
#define _In_opt_
#define _Inout_
#define _Out_
#define FASTCALL
#define NTAPI
#define VOID void
#define ASSERT assert
#define DISPATCH_LEVEL 2
#define SYNCH_LEVEL 2
#define HIGH_PRIORITY 31
#define MAXULONG UINT32_MAX
#define MAXIMUM_PROCESSORS 64
#define LOW_PRIORITY 0
#define LOW_REALTIME_PRIORITY 16
#define KI_EEVDF_QUANTUM_BASE 6
#define PROCESS_PRIORITY_NORMAL_FOREGROUND 9
#define PROCESS_PRIORITY_CLASS_NORMAL 2
#define FALSE 0
#define TRUE 1
#define Ready 1
#define DeferredReady 2
#define Terminated 3
enum { Standby = 4, Running = 5 };
#define PRIORITY_MASK(Priority) (UINT32_C(1) << (Priority))
#define AFFINITY_MASK(Processor) ((ULONG_PTR)1 << (Processor))
#define KiThreadAffinityMask(Thread) ((Thread)->Affinity)
#define NonPagedPoolNx 0
#define RtlZeroMemory(Address, Size) memset((Address), 0, (Size))
#define min(Left, Right) ((Left) < (Right) ? (Left) : (Right))
#define UNREFERENCED_PARAMETER(Parameter) ((void)(Parameter))
#define CONTAINING_RECORD(Address, Type, Field) \
    ((Type *)((char *)(Address) - offsetof(Type, Field)))

typedef int BOOLEAN;
typedef unsigned char UCHAR;
typedef signed char SCHAR;
typedef int KPRIORITY;
typedef void *PVOID;
typedef uint64_t ULONGLONG;
typedef int64_t LONGLONG;
typedef int64_t LONG64, *PLONG64;
typedef uintptr_t ULONG_PTR;
typedef uint32_t ULONG;
typedef int KIRQL;
typedef int KSPIN_LOCK;
typedef int32_t *PLONG;
typedef struct _KPROCESS { int BasePriority; } KPROCESS, *PKPROCESS;
typedef struct _EPROCESS { KPROCESS Pcb; UCHAR PriorityClass; } EPROCESS, *PEPROCESS;
typedef struct _LIST_ENTRY
{
    struct _LIST_ENTRY *Flink;
    struct _LIST_ENTRY *Blink;
} LIST_ENTRY, *PLIST_ENTRY;
typedef struct _KTHREAD
{
    LIST_ENTRY WaitListEntry;
    int State;
    int Priority;
    int BasePriority;
    int QuantumReset;
    PKPROCESS Process;
    BOOLEAN Preempted;
    BOOLEAN DeferPreemption;
    ULONG WaitTime;
    int ProcessReadyQueue;
    unsigned NextProcessor;
    ULONG_PTR Affinity;
    int Leaving;
    BOOLEAN Running;
    BOOLEAN ReadyTransition;
    ULONGLONG CycleTime;
    uint64_t VirtualRuntime;
    uint64_t Deadline;
    uint32_t Weight;
} KTHREAD, *PKTHREAD;
typedef struct _KPRCB
{
    int PrcbLock;
    BOOLEAN QuantumEnd;
    uint32_t ReadySummary;
    unsigned Number;
    ULONG_PTR SetMember;
    PKTHREAD IdleThread;
    PKTHREAD CurrentThread;
    PKTHREAD NextThread;
    LIST_ENTRY DispatcherReadyListHead[32];
} KPRCB, *PKPRCB;

static inline int KeGetCurrentIrql(void) { return DISPATCH_LEVEL; }
static inline LONG64 InterlockedCompareExchange64(PLONG64 Target, LONG64 Exchange, LONG64 Comparand)
{
    LONG64 Value = *Target;
    if (Value == Comparand) *Target = Exchange;
    return Value;
}
#ifndef EEVDF_CUSTOM_LEAVING
static inline int KiThreadLeavingProcessor(PKTHREAD Thread) { return Thread->Leaving; }
#endif
KIRQL KeRaiseIrqlToSynchLevel(void);
void KeLowerIrql(KIRQL OldIrql);
void KeAcquireSpinLockAtDpcLevel(KSPIN_LOCK *Lock);
void KeReleaseSpinLockFromDpcLevel(KSPIN_LOCK *Lock);
void *ExAllocatePoolWithTag(int Pool, size_t Size, ULONG Tag);
void ExFreePoolWithTag(void *Pointer, ULONG Tag);
