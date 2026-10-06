/* NTSTATUS conversion adapted from Wine 10.0 dlls/ntdll/error.c.
 * Copyright 2000 Alexandre Julliard
 * Copyright 2002 Andriy Palamarchuk
 * Copyright 2010 André Hentschel
 * SPDX-License-Identifier: LGPL-2.1-or-later
 * See ../licenses/Wine-LGPL-2.1.txt and config/runtime/ntstatus-sources.json.
 * Changes: fixed-width types, native sparse TEB, no Wine logging dependency.
 */
#include "guest_ntstatus.h"
#include "guest_teb.h"
#include "ntstatus_table.inc"

uint32_t tribes_ntstatus_to_dos_error(uint32_t status) {
    tribes_teb_set_last_status(status);
    if (!status || (status & 0x20000000u)) return status;
    if ((status & 0xf0000000u) == 0xd0000000u) status &= ~0x10000000u;
    uint32_t facility = status >> 16;
    if (facility == 0xc001 || facility == 0x8007 || facility == 0xc007)
        return status & 0xffff;
    unsigned low = 0, high = sizeof ntstatus_ranges / sizeof ntstatus_ranges[0];
    while (low < high) {
        unsigned middle = low + (high - low) / 2;
        if (status < ntstatus_ranges[middle].first) high = middle;
        else if (status > ntstatus_ranges[middle].last) low = middle + 1;
        else return ntstatus_errors[ntstatus_ranges[middle].offset + status - ntstatus_ranges[middle].first];
    }
    return 317; /* ERROR_MR_MID_NOT_FOUND */
}
