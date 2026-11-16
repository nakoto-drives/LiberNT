/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     EEVDF eligibility and deadline selection arithmetic
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

typedef struct _KI_EEVDF_CANDIDATE
{
    uint64_t VirtualRuntime;
    uint64_t Deadline;
    uint32_t Weight;
} KI_EEVDF_CANDIDATE;

typedef enum _KI_EEVDF_RESULT
{
    KiEevdfOk,
    KiEevdfEmpty,
    KiEevdfInvalid,
    KiEevdfOverflow
} KI_EEVDF_RESULT;

typedef struct _KI_EEVDF_STATS
{
    uint64_t Minimum;
    uint64_t TotalWeight;
    uint64_t WeightedDistance;
} KI_EEVDF_STATS;

typedef struct _KI_EEVDF_CLOCK
{
    uint64_t VirtualRuntime;
    uint32_t Weight;
    uint32_t Remainder;
} KI_EEVDF_CLOCK;

KI_EEVDF_RESULT
KiEevdfCharge(
    KI_EEVDF_CLOCK *Clock,
    uint64_t Execution);

KI_EEVDF_RESULT
KiEevdfDeadline(
    const KI_EEVDF_CLOCK *Clock,
    uint64_t Slice,
    uint64_t *Deadline);

KI_EEVDF_RESULT
KiEevdfReweight(
    KI_EEVDF_CLOCK *Clock,
    uint32_t Weight);

KI_EEVDF_RESULT
KiEevdfAccumulate(
    KI_EEVDF_STATS *Stats,
    const KI_EEVDF_CANDIDATE *Candidate);

KI_EEVDF_RESULT
KiEevdfCaptureLag(
    const KI_EEVDF_STATS *Stats,
    uint64_t VirtualRuntime,
    uint64_t Limit,
    int64_t *Lag);

KI_EEVDF_RESULT
KiEevdfPlaceLag(
    const KI_EEVDF_STATS *Stats,
    uint32_t Weight,
    int64_t Lag,
    uint64_t *VirtualRuntime);

KI_EEVDF_RESULT
KiEevdfScaleLag(
    int64_t Lag,
    uint32_t OldWeight,
    uint32_t NewWeight,
    int64_t *ScaledLag);

KI_EEVDF_RESULT
KiEevdfScaleDuration(
    uint64_t Duration,
    uint32_t OldWeight,
    uint32_t NewWeight,
    uint64_t *ScaledDuration);

int
KiEevdfIsEligible(
    const KI_EEVDF_STATS *Stats,
    const KI_EEVDF_CANDIDATE *Candidate);

KI_EEVDF_RESULT
KiEevdfChoose(
    const KI_EEVDF_CANDIDATE *Candidates,
    size_t Count,
    size_t *Selected);
