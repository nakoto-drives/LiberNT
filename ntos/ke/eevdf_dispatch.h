/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     EEVDF ready queue selection interface
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#pragma once

#include "eevdf_math.h"

typedef BOOLEAN (FASTCALL *PKI_EEVDF_QUERY_READY)(
    _In_ PKTHREAD Thread,
    _In_opt_ PVOID Context,
    _Out_ KI_EEVDF_CANDIDATE *Candidate);

BOOLEAN
FASTCALL
KiEevdfTrySelectReady(
    _In_ PKPRCB Prcb,
    _In_ KPRIORITY Priority,
    _Out_ PKTHREAD *Selected);

BOOLEAN
FASTCALL
KiEevdfTrySelectQuantum(
    _In_ PKPRCB Prcb,
    _In_ PKTHREAD Current,
    _Out_ PKTHREAD *Selected);

BOOLEAN
FASTCALL
KiEevdfSelectReady(
    _In_ PKPRCB Prcb,
    _In_ KPRIORITY Priority,
    _In_ PKI_EEVDF_QUERY_READY Query,
    _In_opt_ PVOID Context,
    _Out_ PKTHREAD *Selected);
