/* Copyright (C) 2007-2008 The Android Open Source Project
**
** This software is licensed under the terms of the GNU General Public
** License version 2, as published by the Free Software Foundation, and
** may be copied, distributed, and modified under those terms.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
*/
#include "qemu-common.h"
#include "supplementary_service.h"

#if DEBUG
#  define  D_ACTIVE  VERBOSE_CHECK(modem)
#  define  D(...)   do { if (D_ACTIVE) fprintf( stderr, __VA_ARGS__ ); } while (0)
#else
#  define  D(...)   ((void)0)
#endif

typedef struct ASupplementaryServiceRec_ {
} ASupplementaryServiceRec;

static ASupplementaryServiceRec  _s_supplementary[MAX_GSM_DEVICES];

ASupplementaryService
asupplementary_create(int base_port, int instance_id)
{
    return &_s_supplementary[instance_id];
}

void
asupplementary_destroy(ASupplementaryService supplementary)
{
    /* nothing really */
}
