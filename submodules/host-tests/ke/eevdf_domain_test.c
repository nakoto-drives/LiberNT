/*
 * PROJECT:     LiberNT host-native tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Exercise EEVDF virtual-time domains and migration
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#include <stdio.h>
#include <stdlib.h>
#include "eevdf_domain.h"

#define CHECK(Condition) do { if (!(Condition)) { \
    fprintf(stderr, "line %d: %s\n", __LINE__, #Condition); exit(1); \
} } while (0)

int main(void)
{
    KI_EEVDF_DOMAIN Source, Destination;
    KI_EEVDF_ENTITY A = {0}, B = {0}, C = {0}, *Selected = NULL;
    uint64_t Origin, Runtime, Deadline;
    unsigned Index, ARequests, BRequests;

    KiEevdfInitializeDomain(&Source);
    KiEevdfInitializeDomain(&Destination);
    Origin = Source.Baseline;
    A.Clock.Weight = B.Clock.Weight = C.Clock.Weight = 1;
    A.Slice = B.Slice = C.Slice = 100;
    CHECK(KiEevdfChooseDomain(&Source, &Selected) == KiEevdfEmpty);
    CHECK(KiEevdfJoinDomain(&Source, &A) == KiEevdfOk);
    CHECK(KiEevdfJoinDomain(&Source, &B) == KiEevdfOk);
    CHECK(A.Clock.VirtualRuntime == Origin && B.Clock.VirtualRuntime == Origin);
    CHECK(KiEevdfChooseDomain(&Source, &Selected) == KiEevdfOk && Selected == &A);
    CHECK(KiEevdfChargeEntity(&A, 20) == KiEevdfOk);
    CHECK(KiEevdfLeaveDomain(&A) == KiEevdfOk);
    CHECK(A.Lag == -10240 && A.Domain == NULL);
    CHECK(KiEevdfJoinDomain(&Source, &A) == KiEevdfOk);
    CHECK(A.Clock.VirtualRuntime == Origin + 20480);
    CHECK(KiEevdfChooseDomain(&Source, &Selected) == KiEevdfOk && Selected == &B);
    CHECK(KiEevdfJoinDomain(&Destination, &C) == KiEevdfOk);
    CHECK(KiEevdfMoveDomain(&Destination, &A) == KiEevdfOk);
    CHECK(A.Domain == &Destination && A.Clock.VirtualRuntime == Origin + 20480);
    CHECK(A.Deadline == Origin + 122880);
    CHECK(KiEevdfChooseDomain(&Destination, &Selected) == KiEevdfOk && Selected == &C);
    CHECK(KiEevdfLeaveDomain(&A) == KiEevdfOk && A.Lag == -10240);
    CHECK(KiEevdfLeaveDomain(&C) == KiEevdfOk);
    CHECK(KiEevdfJoinDomain(&Destination, &A) == KiEevdfOk);
    CHECK(A.Clock.VirtualRuntime == Destination.Baseline);
    Runtime = A.Clock.VirtualRuntime;
    Deadline = A.Deadline;
    A.Slice = 0;
    CHECK(KiEevdfMoveDomain(&Source, &A) == KiEevdfInvalid);
    CHECK(A.Domain == &Destination && A.Clock.VirtualRuntime == Runtime && A.Deadline == Deadline);
    A.Slice = 100;
    CHECK(KiEevdfRenewRequest(&A) == KiEevdfOk);
    A.Clock.VirtualRuntime = UINT64_MAX - 1000;
    A.Deadline = UINT64_MAX - 500;
    CHECK(KiEevdfChargeEntity(&A, 2) == KiEevdfOk);
    CHECK(A.Clock.VirtualRuntime == Origin + 2048 && A.Deadline == Origin + 500);
    CHECK(KiEevdfRenewRequest(&A) == KiEevdfOk);
    CHECK(KiEevdfLeaveDomain(&A) == KiEevdfOk);
    CHECK(KiEevdfLeaveDomain(&B) == KiEevdfOk);

    KiEevdfInitializeDomain(&Source);
    A = (KI_EEVDF_ENTITY){0};
    B = (KI_EEVDF_ENTITY){0};
    A.Clock.Weight = 1;
    B.Clock.Weight = 3;
    A.Slice = B.Slice = 10;
    CHECK(KiEevdfJoinDomain(&Source, &A) == KiEevdfOk);
    CHECK(KiEevdfJoinDomain(&Source, &B) == KiEevdfOk);
    ARequests = BRequests = 0;
    for (Index = 0; Index < 20000; Index++)
    {
        CHECK(KiEevdfChooseDomain(&Source, &Selected) == KiEevdfOk);
        if (Selected == &A) ARequests++;
        else if (Selected == &B) BRequests++;
        else CHECK(0);
        CHECK(KiEevdfChargeEntity(Selected, 10) == KiEevdfOk);
        CHECK(KiEevdfRenewRequest(Selected) == KiEevdfOk);
    }
    CHECK(ARequests >= 4999 && ARequests <= 5001);
    CHECK(BRequests == 20000 - ARequests);
    CHECK(KiEevdfLeaveDomain(&A) == KiEevdfOk);
    CHECK(KiEevdfLeaveDomain(&B) == KiEevdfOk);

    KiEevdfInitializeDomain(&Source);
    A = (KI_EEVDF_ENTITY){0};
    B = (KI_EEVDF_ENTITY){0};
    A.Clock.Weight = 1;
    A.Slice = 10;
    B.Clock.Weight = 3;
    B.Clock.Remainder = 2;
    B.Slice = 1;
    B.Lag = -100000;
    CHECK(KiEevdfJoinDomain(&Source, &A) == KiEevdfOk);
    CHECK(KiEevdfJoinDomain(&Source, &B) == KiEevdfOk);
    CHECK(B.Clock.VirtualRuntime == Source.Baseline + 1368);
    CHECK(B.Clock.Remainder == 2);
    CHECK(KiEevdfLeaveDomain(&B) == KiEevdfOk);
    CHECK(KiEevdfLeaveDomain(&A) == KiEevdfOk);

    KiEevdfInitializeDomain(&Source);
    A = (KI_EEVDF_ENTITY){0};
    B = (KI_EEVDF_ENTITY){0};
    A.Clock.Weight = B.Clock.Weight = 1;
    A.Slice = B.Slice = 100;
    CHECK(KiEevdfJoinDomain(&Source, &A) == KiEevdfOk);
    CHECK(KiEevdfJoinDomain(&Source, &B) == KiEevdfOk);
    CHECK(KiEevdfChargeEntity(&A, 10) == KiEevdfOk);
    CHECK(KiEevdfReweightEntity(&A, 2, 100) == KiEevdfOk);
    CHECK(A.Clock.Weight == 2 && A.Clock.VirtualRuntime == Origin + 7680);
    CHECK(A.Deadline == Origin + 53760);
    Runtime = A.Clock.VirtualRuntime;
    Deadline = A.Deadline;
    CHECK(KiEevdfReweightEntity(&A, 0, 100) == KiEevdfInvalid);
    CHECK(A.Clock.Weight == 2 && A.Clock.VirtualRuntime == Runtime && A.Deadline == Deadline);
    CHECK(KiEevdfLeaveDomain(&A) == KiEevdfOk && A.Lag == -2560);
    CHECK(KiEevdfReweightEntity(&B, 3, 100) == KiEevdfOk);
    CHECK(B.Clock.VirtualRuntime == Origin);
    CHECK(KiEevdfLeaveDomain(&B) == KiEevdfOk);

    KiEevdfInitializeDomain(&Source);
    KiEevdfInitializeDomain(&Destination);
    A = (KI_EEVDF_ENTITY){0};
    B = (KI_EEVDF_ENTITY){0};
    C = (KI_EEVDF_ENTITY){0};
    A.Clock.Weight = B.Clock.Weight = C.Clock.Weight = 1;
    A.Slice = B.Slice = C.Slice = 100;
    CHECK(KiEevdfJoinDomain(&Source, &A) == KiEevdfOk);
    CHECK(KiEevdfJoinDomain(&Source, &B) == KiEevdfOk);
    CHECK(KiEevdfJoinDomain(&Destination, &C) == KiEevdfOk);
    CHECK(KiEevdfChargeEntity(&B, 20) == KiEevdfOk);
    CHECK(KiEevdfMoveDomain(&Destination, &A) == KiEevdfOk);
    CHECK(A.Lag == 10240 && A.Clock.VirtualRuntime == Origin - 20480);
    CHECK(KiEevdfMoveDomain(&Source, &A) == KiEevdfOk);
    CHECK(A.Lag == 10240 && A.Clock.VirtualRuntime == Origin);
    CHECK(KiEevdfLeaveDomain(&A) == KiEevdfOk);
    CHECK(KiEevdfLeaveDomain(&B) == KiEevdfOk);
    CHECK(KiEevdfLeaveDomain(&C) == KiEevdfOk);

    KiEevdfInitializeDomain(&Source);
    KiEevdfInitializeDomain(&Destination);
    A = (KI_EEVDF_ENTITY){0};
    B = (KI_EEVDF_ENTITY){0};
    C = (KI_EEVDF_ENTITY){0};
    A.Clock.Weight = B.Clock.Weight = C.Clock.Weight = 1;
    A.Slice = B.Slice = C.Slice = 100;
    CHECK(KiEevdfJoinDomain(&Source, &A) == KiEevdfOk);
    CHECK(KiEevdfJoinDomain(&Source, &B) == KiEevdfOk);
    CHECK(KiEevdfJoinDomain(&Destination, &C) == KiEevdfOk);
    CHECK(KiEevdfChargeEntity(&A, 20) == KiEevdfOk);
    CHECK(KiEevdfMoveDomain(&Destination, &A) == KiEevdfOk);
    CHECK(A.Clock.VirtualRuntime == Origin + 20480 && A.Deadline == Origin + 102400);
    CHECK(KiEevdfLeaveDomain(&A) == KiEevdfOk);
    CHECK(KiEevdfLeaveDomain(&B) == KiEevdfOk);
    CHECK(KiEevdfLeaveDomain(&C) == KiEevdfOk);
    return 0;
}
