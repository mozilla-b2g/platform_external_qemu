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
    /* call forward conditions */
    ACallForwardRec*  call_forward[CALL_FORWARDING_MAX_REASON + 1]
                                  [CALL_FORWARDING_MAX_CLASSX_OFFSET + 1];
} ASupplementaryServiceRec;

static ASupplementaryServiceRec  _s_supplementary[MAX_GSM_DEVICES];

static int
asupplementary_callforward_init(ASupplementaryService supplementary)
{
    // Init call forward record.
    memset(supplementary->call_forward, 0, sizeof(supplementary->call_forward));
}

static void
asupplementary_callforward_destroy(ASupplementaryService supplementary)
{
    int i = 0;
    int j = 0;

    for (i = 0; i <= CALL_FORWARDING_MAX_REASON; i++) {
        for (j = 0; j <= CALL_FORWARDING_MAX_CLASSX_OFFSET; j++) {
            ACallForward record = supplementary->call_forward[i][j];
            supplementary->call_forward[i][j] = NULL;
            if (record) free(record);
        }
    }
}

ASupplementaryService
asupplementary_create(int base_port, int instance_id)
{
    ASupplementaryService supplementary = &_s_supplementary[instance_id];

    asupplementary_callforward_init(supplementary);

    return supplementary;
}

void
asupplementary_destroy(ASupplementaryService supplementary)
{
    asupplementary_callforward_destroy(supplementary);
}

int
asupplementary_set_call_forward(ASupplementaryService supplementary,
                                int reason, int classx_offset, bool enabled,
                                char* number, int toa, int time)
{
    ACallForward* record = NULL;

    if (reason < 0 || reason > CALL_FORWARDING_MAX_REASON ||
        classx_offset < 0 || classx_offset > CALL_FORWARDING_MAX_CLASSX_OFFSET) {
        return -1;
    }

    record = &supplementary->call_forward[reason][classx_offset];
    if (!*record) {
        *record = (ACallForward) malloc(sizeof(ACallForwardRec));
        memset(*record, 0, sizeof(ACallForwardRec));
    }

    (*record)->enabled = enabled;
    (*record)->toa     = toa;
    (*record)->time    = time;
    strncpy((*record)->number, number, CALL_FORWARDING_MAX_NUMBERS);

    return 0;
}

int
asupplementary_remove_call_forward(ASupplementaryService supplementary,
                                   int reason, int classx_offset)
{
    ACallForward* record = NULL;

    if (reason < 0 || reason > CALL_FORWARDING_MAX_REASON ||
        classx_offset < 0 || classx_offset > CALL_FORWARDING_MAX_CLASSX_OFFSET) {
        return -1;
    }

    record = &supplementary->call_forward[reason][classx_offset];

    if (*record) {
        free(*record);
        *record = NULL;
    }

    return 0;
}

ACallForward
asupplementary_get_call_foward(ASupplementaryService supplementary,
                               int reason, int classx_offset)
{
    if (reason < 0 || reason > CALL_FORWARDING_MAX_REASON ||
        classx_offset < 0 || classx_offset > CALL_FORWARDING_MAX_CLASSX_OFFSET) {
        return NULL;
    }

    return supplementary->call_forward[reason][classx_offset];
}
