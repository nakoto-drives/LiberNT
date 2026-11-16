/*
 * PROJECT:     LiberNT host-native tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Check EEVDF eligibility and deadline selection
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#include <stdio.h>
#include <stdlib.h>
#include "eevdf_math.h"

#define CHECK(Condition) do { if (!(Condition)) { \
    fprintf(stderr, "line %d: %s\n", __LINE__, #Condition); exit(1); \
} } while (0)

static uint32_t NextRandom(uint32_t *State)
{
    *State = *State * UINT32_C(1664525) + UINT32_C(1013904223);
    return *State;
}

int main(void)
{
    KI_EEVDF_CANDIDATE Candidates[] = {
        {10, 100, 1}, {20, 10, 1}, {30, 1, 1}
    };
    KI_EEVDF_CANDIDATE Weighted[] = {
        {0, 10, 10}, {10, 5, 1}, {30, 1, 1}
    };
    KI_EEVDF_CANDIDATE Boundary[] = {
        {UINT64_MAX - 20, UINT64_MAX - 1, 1},
        {UINT64_MAX - 10, UINT64_MAX - 2, 1}
    };
    KI_EEVDF_CANDIDATE Overflow[] = {
        {0, 1, 1}, {UINT64_MAX, 2, 2}
    };
    KI_EEVDF_CANDIDATE SumOverflow[] = {
        {0, 1, 1}, {UINT64_MAX, 2, 1}, {1, 3, 1}
    };
    KI_EEVDF_CANDIDATE Tie[] = {
        {100, 5, 1}, {100, 5, 1}
    };
    KI_EEVDF_CANDIDATE Generated[8];
    KI_EEVDF_STATS Stats = {0, 0, 0};
    KI_EEVDF_STATS Domain = {1000, 4, 80};
    KI_EEVDF_CLOCK Clock = {0, 3, 0};
    KI_EEVDF_CLOCK OneCharge = {0, 3, 0};
    uint32_t Random = 329;
    uint64_t Sum, Total, Deadline = 77;
    uint64_t Placed, Average;
    int64_t Lag = 99;
    int64_t SavedLag, ActualLag;
    size_t Selected = 42, Iteration, Index, Expected;

    CHECK(KiEevdfChoose(NULL, 0, &Selected) == KiEevdfEmpty);
    CHECK(Selected == 42);
    CHECK(KiEevdfChoose(NULL, 1, &Selected) == KiEevdfInvalid);
    CHECK(KiEevdfChoose(Candidates, 3, NULL) == KiEevdfInvalid);
    CHECK(KiEevdfChoose(Candidates, 3, &Selected) == KiEevdfOk);
    CHECK(Selected == 1);
    CHECK(KiEevdfChoose(Candidates, 1, &Selected) == KiEevdfOk);
    CHECK(Selected == 0);
    CHECK(KiEevdfChoose(Tie, 2, &Selected) == KiEevdfOk);
    CHECK(Selected == 0);
    CHECK(KiEevdfChoose(Weighted, 3, &Selected) == KiEevdfOk);
    CHECK(Selected == 0);
    Weighted[0].Weight = 1;
    CHECK(KiEevdfChoose(Weighted, 3, &Selected) == KiEevdfOk);
    CHECK(Selected == 1);
    CHECK(KiEevdfChoose(Boundary, 2, &Selected) == KiEevdfOk);
    CHECK(Selected == 0);
    CHECK(KiEevdfChoose(Overflow, 2, &Selected) == KiEevdfOverflow);
    CHECK(Selected == 0);
    CHECK(KiEevdfChoose(SumOverflow, 3, &Selected) == KiEevdfOverflow);
    CHECK(Selected == 0);
    Candidates[0].Weight = 0;
    CHECK(KiEevdfChoose(Candidates, 3, &Selected) == KiEevdfInvalid);
    CHECK(KiEevdfAccumulate(NULL, &Candidates[1]) == KiEevdfInvalid);
    CHECK(KiEevdfAccumulate(&Stats, NULL) == KiEevdfInvalid);
    CHECK(!KiEevdfIsEligible(&Stats, &Candidates[1]));
    CHECK(KiEevdfAccumulate(&Stats, &Overflow[1]) == KiEevdfOverflow);
    CHECK(Stats.TotalWeight == 0 && Stats.WeightedDistance == 0);

    CHECK(KiEevdfCaptureLag(&Domain, 1010, 100, &Lag) == KiEevdfOk && Lag == 10);
    CHECK(KiEevdfPlaceLag(&Domain, 2, Lag, &Deadline) == KiEevdfOk && Deadline == 1005);
    CHECK(KiEevdfCaptureLag(&Domain, 1050, 20, &Lag) == KiEevdfOk && Lag == -20);
    CHECK(KiEevdfPlaceLag(&Domain, 2, Lag, &Deadline) == KiEevdfOk && Deadline == 1050);
    CHECK(KiEevdfCaptureLag(&Domain, 0, 5, &Lag) == KiEevdfOk && Lag == 5);
    CHECK(KiEevdfCaptureLag(&Stats, 0, 5, &Lag) == KiEevdfEmpty && Lag == 5);
    CHECK(KiEevdfPlaceLag(&Stats, 1, 0, &Deadline) == KiEevdfEmpty && Deadline == 1050);
    CHECK(KiEevdfPlaceLag(&Domain, 0, 0, &Deadline) == KiEevdfInvalid);
    CHECK(KiEevdfPlaceLag(&Domain, 1, INT64_MIN, &Deadline) == KiEevdfInvalid);
    CHECK(KiEevdfPlaceLag(&Domain, 1, INT64_MAX, &Deadline) == KiEevdfOverflow);
    CHECK(Deadline == 1050);
    CHECK(KiEevdfScaleLag(-10, 1, 2, &Lag) == KiEevdfOk && Lag == -5);
    CHECK(KiEevdfScaleLag(5, 2, 1, &Lag) == KiEevdfOk && Lag == 10);
    CHECK(KiEevdfScaleLag(INT64_MAX, UINT32_MAX, 1, &Lag) == KiEevdfOverflow && Lag == 10);
    CHECK(KiEevdfScaleLag(INT64_MIN, 1, 1, &Lag) == KiEevdfInvalid && Lag == 10);
    CHECK(KiEevdfScaleDuration(5, 1, 2, &Deadline) == KiEevdfOk && Deadline == 3);
    CHECK(KiEevdfScaleDuration(5, 2, 1, &Deadline) == KiEevdfOk && Deadline == 10);
    CHECK(KiEevdfScaleDuration(UINT64_MAX, 2, 1, &Deadline) == KiEevdfOverflow && Deadline == 10);
    Deadline = 77;

    CHECK(KiEevdfDeadline(&Clock, 1, &Deadline) == KiEevdfOk);
    CHECK(Deadline == 342);
    CHECK(KiEevdfCharge(&Clock, 1) == KiEevdfOk);
    CHECK(Clock.VirtualRuntime == 341 && Clock.Remainder == 1);
    CHECK(KiEevdfDeadline(&Clock, 3, &Deadline) == KiEevdfOk);
    CHECK(Deadline == 1366);
    CHECK(KiEevdfCharge(&Clock, 1) == KiEevdfOk);
    CHECK(KiEevdfCharge(&Clock, 1) == KiEevdfOk);
    CHECK(KiEevdfCharge(&OneCharge, 3) == KiEevdfOk);
    CHECK(Clock.VirtualRuntime == OneCharge.VirtualRuntime &&
          Clock.Remainder == OneCharge.Remainder && Clock.VirtualRuntime == 1024);
    Clock.Remainder = 2;
    CHECK(KiEevdfReweight(&Clock, 6) == KiEevdfOk);
    CHECK(Clock.Weight == 6 && Clock.Remainder == 4);
    CHECK(KiEevdfReweight(&Clock, 0) == KiEevdfInvalid);
    CHECK(Clock.Weight == 6 && Clock.Remainder == 4);
    Clock.VirtualRuntime = UINT64_MAX;
    CHECK(KiEevdfCharge(&Clock, 1) == KiEevdfOverflow);
    CHECK(Clock.VirtualRuntime == UINT64_MAX && Clock.Remainder == 4);
    CHECK(KiEevdfDeadline(&Clock, 1, &Deadline) == KiEevdfOverflow);
    CHECK(Deadline == 1366);
    CHECK(KiEevdfDeadline(&Clock, 0, &Deadline) == KiEevdfInvalid);

    for (Iteration = 0; Iteration < 10000; Iteration++)
    {
        Sum = 0;
        Total = 0;
        for (Index = 0; Index < 8; Index++)
        {
            Generated[Index].VirtualRuntime = NextRandom(&Random) % 10000;
            Generated[Index].Deadline = NextRandom(&Random) % 10000;
            Generated[Index].Weight = NextRandom(&Random) % 20 + 1;
            Sum += Generated[Index].VirtualRuntime * Generated[Index].Weight;
            Total += Generated[Index].Weight;
        }
        Expected = 8;
        for (Index = 0; Index < 8; Index++)
        {
            if (Generated[Index].VirtualRuntime * Total > Sum)
                continue;
            if (Expected == 8 || Generated[Index].Deadline < Generated[Expected].Deadline)
                Expected = Index;
        }
        CHECK(Expected != 8);
        CHECK(KiEevdfChoose(Generated, 8, &Selected) == KiEevdfOk);
        CHECK(Selected == Expected);
    }

    for (Iteration = 0; Iteration < 1000; Iteration++)
    {
        Clock.VirtualRuntime = 0;
        Clock.Remainder = 0;
        Clock.Weight = NextRandom(&Random) % 100 + 1;
        OneCharge = Clock;
        Sum = 0;
        for (Index = 0; Index < 8; Index++)
        {
            uint64_t Execution = NextRandom(&Random) % 10000;
            Sum += Execution;
            CHECK(KiEevdfCharge(&Clock, Execution) == KiEevdfOk);
        }
        CHECK(KiEevdfCharge(&OneCharge, Sum) == KiEevdfOk);
        CHECK(Clock.VirtualRuntime == OneCharge.VirtualRuntime &&
              Clock.Remainder == OneCharge.Remainder);
    }
    for (Iteration = 0; Iteration < 10000; Iteration++)
    {
        Domain.Minimum = UINT64_C(1) << 40;
        Domain.TotalWeight = NextRandom(&Random) % 100 + 1;
        Domain.WeightedDistance = (NextRandom(&Random) % 10000) * Domain.TotalWeight;
        Clock.Weight = NextRandom(&Random) % 100 + 1;
        SavedLag = (int64_t)(NextRandom(&Random) % 20001) - 10000;
        CHECK(KiEevdfPlaceLag(&Domain, Clock.Weight, SavedLag, &Placed) == KiEevdfOk);
        Average = ((Domain.Minimum + Domain.WeightedDistance / Domain.TotalWeight) *
                   Domain.TotalWeight + Placed * Clock.Weight) /
                  (Domain.TotalWeight + Clock.Weight);
        ActualLag = (int64_t)Average - (int64_t)Placed;
        CHECK(ActualLag >= SavedLag - 1 && ActualLag <= SavedLag + 1);
    }
    return 0;
}
