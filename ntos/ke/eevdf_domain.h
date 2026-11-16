/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     EEVDF runnable virtual-time domains
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#pragma once

#include "eevdf_math.h"

typedef struct _KI_EEVDF_DOMAIN KI_EEVDF_DOMAIN;

typedef struct _KI_EEVDF_ENTITY
{
    struct _KI_EEVDF_ENTITY *Next;
    KI_EEVDF_DOMAIN *Domain;
    KI_EEVDF_CLOCK Clock;
    uint64_t Deadline;
    uint64_t Slice;
    int64_t Lag;
} KI_EEVDF_ENTITY;

struct _KI_EEVDF_DOMAIN
{
    KI_EEVDF_ENTITY *First;
    uint64_t Baseline;
    uint64_t Progress;
    uint32_t ProgressWeight;
    uint32_t ProgressRemainder;
};

void KiEevdfInitializeDomain(KI_EEVDF_DOMAIN *Domain);
KI_EEVDF_RESULT KiEevdfJoinDomain(KI_EEVDF_DOMAIN *Domain, KI_EEVDF_ENTITY *Entity);
KI_EEVDF_RESULT KiEevdfLeaveDomain(KI_EEVDF_ENTITY *Entity);
void KiEevdfDiscardEntity(KI_EEVDF_ENTITY *Entity);
KI_EEVDF_RESULT KiEevdfMoveDomain(KI_EEVDF_DOMAIN *Destination, KI_EEVDF_ENTITY *Entity);
KI_EEVDF_RESULT KiEevdfReweightEntity(KI_EEVDF_ENTITY *Entity, uint32_t Weight, uint64_t Slice);
KI_EEVDF_RESULT KiEevdfRebaseDomain(KI_EEVDF_DOMAIN *Domain);
KI_EEVDF_RESULT KiEevdfChargeEntity(KI_EEVDF_ENTITY *Entity, uint64_t Execution);
KI_EEVDF_RESULT KiEevdfRenewRequest(KI_EEVDF_ENTITY *Entity);
KI_EEVDF_RESULT KiEevdfChooseDomain(KI_EEVDF_DOMAIN *Domain, KI_EEVDF_ENTITY **Selected);
