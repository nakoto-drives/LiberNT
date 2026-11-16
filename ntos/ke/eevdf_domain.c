/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     EEVDF runnable virtual-time domains
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#include "eevdf_domain.h"

#define KI_EEVDF_ORIGIN (UINT64_C(1) << 60)

static KI_EEVDF_RESULT
KiEevdfDomainStatsExcept(const KI_EEVDF_DOMAIN *Domain,
                         const KI_EEVDF_ENTITY *Excluded,
                         KI_EEVDF_STATS *Stats)
{
    const KI_EEVDF_ENTITY *Entity;
    KI_EEVDF_CANDIDATE Candidate;
    KI_EEVDF_RESULT Result;

    Stats->Minimum = 0;
    Stats->TotalWeight = 0;
    Stats->WeightedDistance = 0;
    Stats->Minimum = UINT64_MAX;
    for (Entity = Domain->First; Entity != NULL; Entity = Entity->Next)
    {
        if (Entity->Domain != Domain || Entity->Clock.Weight == 0)
            return KiEevdfInvalid;
        if (Entity == Excluded)
            continue;
        if (Entity->Clock.VirtualRuntime < Stats->Minimum)
            Stats->Minimum = Entity->Clock.VirtualRuntime;
    }
    for (Entity = Domain->First; Entity != NULL; Entity = Entity->Next)
    {
        if (Entity == Excluded)
            continue;
        Candidate.VirtualRuntime = Entity->Clock.VirtualRuntime;
        Candidate.Deadline = Entity->Deadline;
        Candidate.Weight = Entity->Clock.Weight;
        Result = KiEevdfAccumulate(Stats, &Candidate);
        if (Result != KiEevdfOk)
            return Result;
    }
    return Stats->TotalWeight == 0 ? KiEevdfEmpty : KiEevdfOk;
}

static KI_EEVDF_RESULT
KiEevdfDomainStats(const KI_EEVDF_DOMAIN *Domain, KI_EEVDF_STATS *Stats)
{
    return KiEevdfDomainStatsExcept(Domain, NULL, Stats);
}

static KI_EEVDF_RESULT
KiEevdfDomainLagLimit(const KI_EEVDF_ENTITY *Entity, uint64_t *Limit)
{
    KI_EEVDF_CLOCK Clock = Entity->Clock;
    KI_EEVDF_RESULT Result;

    Clock.VirtualRuntime = 0;
    Result = KiEevdfDeadline(&Clock, Entity->Slice, Limit);
    if (Result != KiEevdfOk)
        return Result;
    if (*Limit > INT64_MAX)
        *Limit = INT64_MAX;
    return KiEevdfOk;
}

static KI_EEVDF_RESULT
KiEevdfDomainLag(const KI_EEVDF_ENTITY *Entity,
                 const KI_EEVDF_STATS *Stats,
                 int64_t *Lag)
{
    uint64_t Limit;
    KI_EEVDF_RESULT Result = KiEevdfDomainLagLimit(Entity, &Limit);

    if (Result != KiEevdfOk)
        return Result;
    return KiEevdfCaptureLag(Stats, Entity->Clock.VirtualRuntime, Limit, Lag);
}

static KI_EEVDF_RESULT
KiEevdfDomainPlacement(const KI_EEVDF_DOMAIN *Domain,
                       const KI_EEVDF_ENTITY *Entity,
                       const KI_EEVDF_ENTITY *Excluded,
                       int64_t Lag,
                       uint64_t *Runtime,
                       uint64_t *Deadline)
{
    KI_EEVDF_STATS Stats;
    KI_EEVDF_CLOCK Clock = Entity->Clock;
    KI_EEVDF_RESULT Result;
    uint64_t Limit;

    Result = KiEevdfDomainLagLimit(Entity, &Limit);
    if (Result != KiEevdfOk)
        return Result;
    if (Lag > (int64_t)Limit)
        Lag = (int64_t)Limit;
    else if (Lag < -(int64_t)Limit)
        Lag = -(int64_t)Limit;

    Result = KiEevdfDomainStatsExcept(Domain, Excluded, &Stats);
    if (Result == KiEevdfEmpty)
        Clock.VirtualRuntime = Excluded != NULL ? Excluded->Clock.VirtualRuntime : Domain->Baseline;
    else if (Result == KiEevdfOk)
    {
        Result = KiEevdfPlaceLag(&Stats, Clock.Weight, Lag, &Clock.VirtualRuntime);
        if (Result != KiEevdfOk)
            return Result;
    }
    else
        return Result;
    Result = KiEevdfDeadline(&Clock, Entity->Slice, Deadline);
    if (Result != KiEevdfOk)
        return Result;
    *Runtime = Clock.VirtualRuntime;
    return KiEevdfOk;
}

static KI_EEVDF_RESULT
KiEevdfDomainMovePlacement(const KI_EEVDF_DOMAIN *Domain,
                           const KI_EEVDF_ENTITY *Entity,
                           int64_t Lag,
                           uint64_t *Runtime,
                           uint64_t *Deadline)
{
    uint64_t Remaining;
    KI_EEVDF_RESULT Result;

    Result = KiEevdfDomainPlacement(Domain, Entity, NULL, Lag, Runtime, Deadline);
    if (Result != KiEevdfOk || Entity->Deadline <= Entity->Clock.VirtualRuntime)
        return Result;
    Remaining = Entity->Deadline - Entity->Clock.VirtualRuntime;
    if (*Runtime > UINT64_MAX - Remaining)
        return KiEevdfOverflow;
    *Deadline = *Runtime + Remaining;
    return KiEevdfOk;
}

static void
KiEevdfDomainUnlink(KI_EEVDF_DOMAIN *Domain, KI_EEVDF_ENTITY *Entity)
{
    KI_EEVDF_ENTITY **Link = &Domain->First;

    while (*Link != Entity)
        Link = &(*Link)->Next;
    *Link = Entity->Next;
    Entity->Next = NULL;
    Entity->Domain = NULL;
}

static void
KiEevdfDomainLink(KI_EEVDF_DOMAIN *Domain, KI_EEVDF_ENTITY *Entity,
                  uint64_t Runtime, uint64_t Deadline)
{
    KI_EEVDF_ENTITY **Link = &Domain->First;

    Entity->Clock.VirtualRuntime = Runtime;
    Entity->Deadline = Deadline;
    Entity->Domain = Domain;
    while (*Link != NULL)
        Link = &(*Link)->Next;
    *Link = Entity;
}

void
KiEevdfInitializeDomain(KI_EEVDF_DOMAIN *Domain)
{
    Domain->First = NULL;
    Domain->Baseline = KI_EEVDF_ORIGIN;
    Domain->Progress = 0;
    Domain->ProgressWeight = 1;
    Domain->ProgressRemainder = 0;
}

KI_EEVDF_RESULT
KiEevdfJoinDomain(KI_EEVDF_DOMAIN *Domain, KI_EEVDF_ENTITY *Entity)
{
    uint64_t Runtime, Deadline;
    KI_EEVDF_RESULT Result;

    if (Domain == NULL || Entity == NULL || Entity->Domain != NULL || Entity->Next != NULL)
        return KiEevdfInvalid;
    Result = KiEevdfDomainPlacement(Domain, Entity, NULL, Entity->Lag, &Runtime, &Deadline);
    if (Result == KiEevdfOverflow && KiEevdfRebaseDomain(Domain) == KiEevdfOk)
        Result = KiEevdfDomainPlacement(Domain, Entity, NULL, Entity->Lag, &Runtime, &Deadline);
    if (Result == KiEevdfOk)
        KiEevdfDomainLink(Domain, Entity, Runtime, Deadline);
    return Result;
}

KI_EEVDF_RESULT
KiEevdfLeaveDomain(KI_EEVDF_ENTITY *Entity)
{
    KI_EEVDF_DOMAIN *Domain;
    KI_EEVDF_STATS Stats;
    KI_EEVDF_RESULT Result;
    int64_t Lag;

    if (Entity == NULL || Entity->Domain == NULL)
        return KiEevdfInvalid;
    Domain = Entity->Domain;
    Result = KiEevdfDomainStats(Domain, &Stats);
    if (Result != KiEevdfOk)
        return Result;
    Result = KiEevdfDomainLag(Entity, &Stats, &Lag);
    if (Result != KiEevdfOk)
        return Result;
    Domain->Baseline = Stats.Minimum + Stats.WeightedDistance / Stats.TotalWeight;
    KiEevdfDomainUnlink(Domain, Entity);
    Entity->Lag = Lag;
    return KiEevdfOk;
}

void
KiEevdfDiscardEntity(KI_EEVDF_ENTITY *Entity)
{
    KI_EEVDF_DOMAIN *Domain;
    KI_EEVDF_STATS Stats;

    if (Entity == NULL || Entity->Domain == NULL)
        return;
    Domain = Entity->Domain;
    if (KiEevdfDomainStats(Domain, &Stats) == KiEevdfOk)
        Domain->Baseline = Stats.Minimum + Stats.WeightedDistance / Stats.TotalWeight;
    else if (Domain->First == Entity && Entity->Next == NULL)
        Domain->Baseline = Entity->Clock.VirtualRuntime;
    KiEevdfDomainUnlink(Domain, Entity);
    Entity->Lag = 0;
}

KI_EEVDF_RESULT
KiEevdfMoveDomain(KI_EEVDF_DOMAIN *Destination, KI_EEVDF_ENTITY *Entity)
{
    KI_EEVDF_DOMAIN *Source;
    KI_EEVDF_STATS Stats;
    KI_EEVDF_RESULT Result;
    uint64_t Runtime, Deadline;
    int64_t Lag;

    if (Destination == NULL || Entity == NULL || Entity->Domain == NULL)
        return KiEevdfInvalid;
    Source = Entity->Domain;
    if (Source == Destination)
        return KiEevdfOk;
    Result = KiEevdfDomainStats(Source, &Stats);
    if (Result != KiEevdfOk)
        return Result;
    Result = KiEevdfDomainLag(Entity, &Stats, &Lag);
    if (Result != KiEevdfOk)
        return Result;
    Result = KiEevdfDomainMovePlacement(Destination, Entity, Lag, &Runtime, &Deadline);
    if (Result == KiEevdfOverflow && KiEevdfRebaseDomain(Destination) == KiEevdfOk)
        Result = KiEevdfDomainMovePlacement(Destination, Entity, Lag, &Runtime, &Deadline);
    if (Result != KiEevdfOk)
        return Result;
    Source->Baseline = Stats.Minimum + Stats.WeightedDistance / Stats.TotalWeight;
    KiEevdfDomainUnlink(Source, Entity);
    Entity->Lag = Lag;
    KiEevdfDomainLink(Destination, Entity, Runtime, Deadline);
    return KiEevdfOk;
}

static KI_EEVDF_RESULT
KiEevdfReweightEntityOnce(KI_EEVDF_ENTITY *Entity, uint32_t Weight, uint64_t Slice)
{
    KI_EEVDF_ENTITY Changed;
    KI_EEVDF_STATS Stats;
    KI_EEVDF_RESULT Result;
    uint64_t Runtime, Deadline, Remaining;
    int64_t Lag;

    if (Entity == NULL || Entity->Domain == NULL || Weight == 0 || Slice == 0)
        return KiEevdfInvalid;
    Result = KiEevdfDomainStats(Entity->Domain, &Stats);
    if (Result != KiEevdfOk)
        return Result;
    Result = KiEevdfDomainLag(Entity, &Stats, &Lag);
    if (Result != KiEevdfOk)
        return Result;
    Result = KiEevdfScaleLag(Lag, Entity->Clock.Weight, Weight, &Lag);
    if (Result != KiEevdfOk)
        return Result;
    Changed = *Entity;
    Result = KiEevdfReweight(&Changed.Clock, Weight);
    if (Result != KiEevdfOk)
        return Result;
    Changed.Slice = Slice;
    Result = KiEevdfDomainPlacement(Entity->Domain, &Changed, Entity, Lag, &Runtime, &Deadline);
    if (Result != KiEevdfOk)
        return Result;
    if (Entity->Deadline > Entity->Clock.VirtualRuntime)
    {
        Result = KiEevdfScaleDuration(Entity->Deadline - Entity->Clock.VirtualRuntime,
                                     Entity->Clock.Weight, Weight, &Remaining);
        if (Result != KiEevdfOk)
            return Result;
        if (Runtime > UINT64_MAX - Remaining)
            return KiEevdfOverflow;
        Deadline = Runtime + Remaining;
    }
    Entity->Clock = Changed.Clock;
    Entity->Clock.VirtualRuntime = Runtime;
    Entity->Deadline = Deadline;
    Entity->Slice = Slice;
    return KiEevdfOk;
}

KI_EEVDF_RESULT
KiEevdfReweightEntity(KI_EEVDF_ENTITY *Entity, uint32_t Weight, uint64_t Slice)
{
    KI_EEVDF_RESULT Result;

    Result = KiEevdfReweightEntityOnce(Entity, Weight, Slice);
    if (Result == KiEevdfOverflow && Entity != NULL && Entity->Domain != NULL &&
        KiEevdfRebaseDomain(Entity->Domain) == KiEevdfOk)
        Result = KiEevdfReweightEntityOnce(Entity, Weight, Slice);
    return Result;
}

KI_EEVDF_RESULT
KiEevdfRebaseDomain(KI_EEVDF_DOMAIN *Domain)
{
    KI_EEVDF_ENTITY *Entity;
    uint64_t Minimum, Maximum;

    if (Domain == NULL)
        return KiEevdfInvalid;
    if (Domain->First == NULL)
    {
        Domain->Baseline = KI_EEVDF_ORIGIN;
        return KiEevdfOk;
    }
    Minimum = Maximum = Domain->First->Clock.VirtualRuntime;
    for (Entity = Domain->First; Entity != NULL; Entity = Entity->Next)
    {
        if (Entity->Domain != Domain || Entity->Clock.Weight == 0)
            return KiEevdfInvalid;
        if (Entity->Clock.VirtualRuntime < Minimum)
            Minimum = Entity->Clock.VirtualRuntime;
        if (Entity->Deadline < Minimum)
            Minimum = Entity->Deadline;
        if (Entity->Clock.VirtualRuntime > Maximum)
            Maximum = Entity->Clock.VirtualRuntime;
        if (Entity->Deadline > Maximum)
            Maximum = Entity->Deadline;
    }
    if (Maximum - Minimum > UINT64_MAX - KI_EEVDF_ORIGIN)
        return KiEevdfOverflow;
    for (Entity = Domain->First; Entity != NULL; Entity = Entity->Next)
    {
        Entity->Clock.VirtualRuntime = KI_EEVDF_ORIGIN + (Entity->Clock.VirtualRuntime - Minimum);
        Entity->Deadline = KI_EEVDF_ORIGIN + (Entity->Deadline - Minimum);
    }
    Domain->Baseline = KI_EEVDF_ORIGIN;
    return KiEevdfOk;
}

KI_EEVDF_RESULT
KiEevdfChargeEntity(KI_EEVDF_ENTITY *Entity, uint64_t Execution)
{
    KI_EEVDF_CLOCK Clock, ProgressClock;
    KI_EEVDF_DOMAIN *Domain;
    KI_EEVDF_STATS Stats;
    KI_EEVDF_RESULT Result;

    if (Entity == NULL || Entity->Domain == NULL)
        return KiEevdfInvalid;
    Domain = Entity->Domain;
    Result = KiEevdfDomainStats(Domain, &Stats);
    if (Result != KiEevdfOk)
        return Result;
    if (Stats.TotalWeight > UINT32_MAX)
        return KiEevdfOverflow;
    ProgressClock.VirtualRuntime = 0;
    ProgressClock.Weight = Domain->ProgressWeight;
    ProgressClock.Remainder = Domain->ProgressRemainder;
    Result = KiEevdfReweight(&ProgressClock, (uint32_t)Stats.TotalWeight);
    if (Result != KiEevdfOk)
        return Result;
    Result = KiEevdfCharge(&ProgressClock, Execution);
    if (Result != KiEevdfOk)
        return Result;
    Clock = Entity->Clock;
    Result = KiEevdfCharge(&Clock, Execution);
    if (Result == KiEevdfOverflow)
    {
        Result = KiEevdfRebaseDomain(Entity->Domain);
        if (Result != KiEevdfOk)
            return Result;
        Clock = Entity->Clock;
        Result = KiEevdfCharge(&Clock, Execution);
    }
    if (Result == KiEevdfOk)
    {
        Entity->Clock = Clock;
        Domain->Progress += ProgressClock.VirtualRuntime;
        Domain->ProgressWeight = ProgressClock.Weight;
        Domain->ProgressRemainder = ProgressClock.Remainder;
    }
    return Result;
}

KI_EEVDF_RESULT
KiEevdfRenewRequest(KI_EEVDF_ENTITY *Entity)
{
    uint64_t Deadline;
    KI_EEVDF_RESULT Result;

    if (Entity == NULL || Entity->Domain == NULL)
        return KiEevdfInvalid;
    Result = KiEevdfDeadline(&Entity->Clock, Entity->Slice, &Deadline);
    if (Result == KiEevdfOverflow && KiEevdfRebaseDomain(Entity->Domain) == KiEevdfOk)
        Result = KiEevdfDeadline(&Entity->Clock, Entity->Slice, &Deadline);
    if (Result == KiEevdfOk)
        Entity->Deadline = Deadline;
    return Result;
}

KI_EEVDF_RESULT
KiEevdfChooseDomain(KI_EEVDF_DOMAIN *Domain, KI_EEVDF_ENTITY **Selected)
{
    KI_EEVDF_ENTITY *Entity, *Best = NULL;
    KI_EEVDF_STATS Stats;
    KI_EEVDF_CANDIDATE Candidate;
    KI_EEVDF_RESULT Result;

    if (Domain == NULL || Selected == NULL)
        return KiEevdfInvalid;
    Result = KiEevdfDomainStats(Domain, &Stats);
    if (Result != KiEevdfOk)
        return Result;
    for (Entity = Domain->First; Entity != NULL; Entity = Entity->Next)
    {
        Candidate.VirtualRuntime = Entity->Clock.VirtualRuntime;
        Candidate.Deadline = Entity->Deadline;
        Candidate.Weight = Entity->Clock.Weight;
        if (KiEevdfIsEligible(&Stats, &Candidate) &&
            (Best == NULL || Entity->Deadline < Best->Deadline))
            Best = Entity;
    }
    if (Best == NULL)
        return KiEevdfInvalid;
    *Selected = Best;
    return KiEevdfOk;
}
