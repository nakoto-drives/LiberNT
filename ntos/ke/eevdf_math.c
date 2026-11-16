/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     EEVDF eligibility and deadline selection arithmetic
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#include "eevdf_math.h"

#define KI_EEVDF_SCALE UINT64_C(1024)

KI_EEVDF_RESULT
KiEevdfCharge(
    KI_EEVDF_CLOCK *Clock,
    uint64_t Execution)
{
    uint64_t Quotient, Fraction, Delta;

    if (Clock == NULL || Clock->Weight == 0 || Clock->Remainder >= Clock->Weight)
        return KiEevdfInvalid;
    Quotient = Execution / Clock->Weight;
    if (Quotient > UINT64_MAX / KI_EEVDF_SCALE)
        return KiEevdfOverflow;
    Fraction = (Execution % Clock->Weight) * KI_EEVDF_SCALE + Clock->Remainder;
    Delta = Quotient * KI_EEVDF_SCALE;
    if (Delta > UINT64_MAX - Fraction / Clock->Weight)
        return KiEevdfOverflow;
    Delta += Fraction / Clock->Weight;
    if (Clock->VirtualRuntime > UINT64_MAX - Delta)
        return KiEevdfOverflow;
    Clock->VirtualRuntime += Delta;
    Clock->Remainder = (uint32_t)(Fraction % Clock->Weight);
    return KiEevdfOk;
}

KI_EEVDF_RESULT
KiEevdfDeadline(
    const KI_EEVDF_CLOCK *Clock,
    uint64_t Slice,
    uint64_t *Deadline)
{
    uint64_t Quotient, Fraction, Delta;

    if (Clock == NULL || Deadline == NULL || Clock->Weight == 0 ||
        Clock->Remainder >= Clock->Weight || Slice == 0)
        return KiEevdfInvalid;
    Quotient = Slice / Clock->Weight;
    if (Quotient > UINT64_MAX / KI_EEVDF_SCALE)
        return KiEevdfOverflow;
    Fraction = (Slice % Clock->Weight) * KI_EEVDF_SCALE + Clock->Remainder;
    Delta = Quotient * KI_EEVDF_SCALE;
    Fraction = Fraction / Clock->Weight + ((Fraction % Clock->Weight) != 0);
    if (Delta > UINT64_MAX - Fraction)
        return KiEevdfOverflow;
    Delta += Fraction;
    if (Clock->VirtualRuntime > UINT64_MAX - Delta)
        return KiEevdfOverflow;
    *Deadline = Clock->VirtualRuntime + Delta;
    return KiEevdfOk;
}

KI_EEVDF_RESULT
KiEevdfReweight(
    KI_EEVDF_CLOCK *Clock,
    uint32_t Weight)
{
    uint64_t Remainder;

    if (Clock == NULL || Clock->Weight == 0 || Clock->Remainder >= Clock->Weight || Weight == 0)
        return KiEevdfInvalid;
    Remainder = (uint64_t)Clock->Remainder * Weight / Clock->Weight;
    Clock->Weight = Weight;
    Clock->Remainder = (uint32_t)Remainder;
    return KiEevdfOk;
}

KI_EEVDF_RESULT
KiEevdfAccumulate(
    KI_EEVDF_STATS *Stats,
    const KI_EEVDF_CANDIDATE *Candidate)
{
    uint64_t Distance, Term;

    if (Stats == NULL || Candidate == NULL || Candidate->Weight == 0 ||
        Candidate->VirtualRuntime < Stats->Minimum)
        return KiEevdfInvalid;

    Distance = Candidate->VirtualRuntime - Stats->Minimum;
    if (Distance > UINT64_MAX / Candidate->Weight)
        return KiEevdfOverflow;
    Term = Distance * Candidate->Weight;
    if (Stats->WeightedDistance > UINT64_MAX - Term ||
        Stats->TotalWeight > UINT64_MAX - Candidate->Weight)
        return KiEevdfOverflow;
    Stats->WeightedDistance += Term;
    Stats->TotalWeight += Candidate->Weight;
    return KiEevdfOk;
}

KI_EEVDF_RESULT
KiEevdfCaptureLag(
    const KI_EEVDF_STATS *Stats,
    uint64_t VirtualRuntime,
    uint64_t Limit,
    int64_t *Lag)
{
    uint64_t Time, Distance, Magnitude;

    if (Stats == NULL || Lag == NULL || Limit > INT64_MAX)
        return KiEevdfInvalid;
    if (Stats->TotalWeight == 0)
        return KiEevdfEmpty;
    Distance = Stats->WeightedDistance / Stats->TotalWeight;
    if (Stats->Minimum > UINT64_MAX - Distance)
        return KiEevdfOverflow;
    Time = Stats->Minimum + Distance;
    if (Time >= VirtualRuntime)
    {
        Magnitude = Time - VirtualRuntime;
        *Lag = (int64_t)(Magnitude < Limit ? Magnitude : Limit);
    }
    else
    {
        Magnitude = VirtualRuntime - Time;
        *Lag = -(int64_t)(Magnitude < Limit ? Magnitude : Limit);
    }
    return KiEevdfOk;
}

KI_EEVDF_RESULT
KiEevdfPlaceLag(
    const KI_EEVDF_STATS *Stats,
    uint32_t Weight,
    int64_t Lag,
    uint64_t *VirtualRuntime)
{
    uint64_t Time, Distance, Magnitude, Quotient, Remainder, Extra;

    if (Stats == NULL || VirtualRuntime == NULL || Weight == 0 || Lag == INT64_MIN)
        return KiEevdfInvalid;
    if (Stats->TotalWeight == 0)
        return KiEevdfEmpty;
    Distance = Stats->WeightedDistance / Stats->TotalWeight;
    if (Stats->Minimum > UINT64_MAX - Distance)
        return KiEevdfOverflow;
    Time = Stats->Minimum + Distance;
    Magnitude = (uint64_t)(Lag < 0 ? -Lag : Lag);
    Quotient = Magnitude / Stats->TotalWeight;
    Remainder = Magnitude % Stats->TotalWeight;
    if (Quotient > UINT64_MAX / Weight || Remainder > UINT64_MAX / Weight)
        return KiEevdfOverflow;
    Extra = Quotient * Weight;
    Remainder = Remainder * Weight / Stats->TotalWeight;
    if (Extra > UINT64_MAX - Remainder || Magnitude > UINT64_MAX - Extra - Remainder)
        return KiEevdfOverflow;
    Distance = Magnitude + Extra + Remainder;
    if (Lag >= 0)
    {
        if (Time < Distance)
            return KiEevdfOverflow;
        *VirtualRuntime = Time - Distance;
    }
    else
    {
        if (Time > UINT64_MAX - Distance)
            return KiEevdfOverflow;
        *VirtualRuntime = Time + Distance;
    }
    return KiEevdfOk;
}

KI_EEVDF_RESULT
KiEevdfScaleLag(
    int64_t Lag,
    uint32_t OldWeight,
    uint32_t NewWeight,
    int64_t *ScaledLag)
{
    uint64_t Magnitude, Quotient, Remainder, Result;

    if (Lag == INT64_MIN || OldWeight == 0 || NewWeight == 0 || ScaledLag == NULL)
        return KiEevdfInvalid;
    Magnitude = (uint64_t)(Lag < 0 ? -Lag : Lag);
    Quotient = Magnitude / NewWeight;
    Remainder = Magnitude % NewWeight;
    if (Quotient > (uint64_t)INT64_MAX / OldWeight)
        return KiEevdfOverflow;
    Result = Quotient * OldWeight;
    Remainder = Remainder * OldWeight / NewWeight;
    if (Result > (uint64_t)INT64_MAX - Remainder)
        return KiEevdfOverflow;
    Result += Remainder;
    *ScaledLag = Lag < 0 ? -(int64_t)Result : (int64_t)Result;
    return KiEevdfOk;
}

KI_EEVDF_RESULT
KiEevdfScaleDuration(
    uint64_t Duration,
    uint32_t OldWeight,
    uint32_t NewWeight,
    uint64_t *ScaledDuration)
{
    uint64_t Quotient, Remainder, Result;

    if (OldWeight == 0 || NewWeight == 0 || ScaledDuration == NULL)
        return KiEevdfInvalid;
    Quotient = Duration / NewWeight;
    Remainder = Duration % NewWeight;
    if (Quotient > UINT64_MAX / OldWeight)
        return KiEevdfOverflow;
    Result = Quotient * OldWeight;
    Remainder *= OldWeight;
    Quotient = Remainder / NewWeight + ((Remainder % NewWeight) != 0);
    if (Result > UINT64_MAX - Quotient)
        return KiEevdfOverflow;
    *ScaledDuration = Result + Quotient;
    return KiEevdfOk;
}

int
KiEevdfIsEligible(
    const KI_EEVDF_STATS *Stats,
    const KI_EEVDF_CANDIDATE *Candidate)
{
    if (Stats == NULL || Candidate == NULL || Stats->TotalWeight == 0 ||
        Candidate->Weight == 0 || Candidate->VirtualRuntime < Stats->Minimum)
        return 0;
    return Candidate->VirtualRuntime - Stats->Minimum <=
           Stats->WeightedDistance / Stats->TotalWeight;
}

KI_EEVDF_RESULT
KiEevdfChoose(
    const KI_EEVDF_CANDIDATE *Candidates,
    size_t Count,
    size_t *Selected)
{
    KI_EEVDF_STATS Stats;
    KI_EEVDF_RESULT Result;
    size_t Index, Best;

    if (Selected == NULL || (Count != 0 && Candidates == NULL))
        return KiEevdfInvalid;
    if (Count == 0)
        return KiEevdfEmpty;

    Stats.Minimum = Candidates[0].VirtualRuntime;
    Stats.TotalWeight = 0;
    Stats.WeightedDistance = 0;
    for (Index = 0; Index < Count; Index++)
    {
        if (Candidates[Index].Weight == 0)
            return KiEevdfInvalid;
        if (Candidates[Index].VirtualRuntime < Stats.Minimum)
            Stats.Minimum = Candidates[Index].VirtualRuntime;
    }

    for (Index = 0; Index < Count; Index++)
    {
        Result = KiEevdfAccumulate(&Stats, &Candidates[Index]);
        if (Result != KiEevdfOk)
            return Result;
    }

    Best = Count;
    for (Index = 0; Index < Count; Index++)
    {
        if (!KiEevdfIsEligible(&Stats, &Candidates[Index]))
            continue;
        if (Best == Count || Candidates[Index].Deadline < Candidates[Best].Deadline)
            Best = Index;
    }

    if (Best == Count)
        return KiEevdfInvalid;
    *Selected = Best;
    return KiEevdfOk;
}
