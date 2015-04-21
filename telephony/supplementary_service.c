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

typedef char AServicePasswdRec [SERVICE_PASSWD_MAX_PASSWD_LENGTH + 1];

typedef struct ACallBarringRec_ {
    bool enabled;
} ACallBarringRec, *ACallBarring;

typedef struct ASupplementaryServiceRec_ {
    /* passwords */
    AServicePasswdRec  passwd[SERVICE_PASSWD_MAX_TYPE + 1];

    /* call forward conditions */
    ACallForwardRec*  call_forward[CALL_FORWARDING_MAX_REASON + 1]
                                  [CALL_FORWARDING_MAX_CLASSX_OFFSET + 1];
    /* call barring programs */
    ACallBarringRec  call_barring[CALL_BARRING_MAX_PROGRAM + 1]
                                 [CALL_BARRING_MAX_CLASSX_OFFSET + 1];
} ASupplementaryServiceRec;

static ASupplementaryServiceRec  _s_supplementary[MAX_GSM_DEVICES];

static void
asupplementary_passwd_init(ASupplementaryService supplementary)
{
    int i = 0;
    for (i = 0; i <= SERVICE_PASSWD_MAX_TYPE; i++) {
        strncpy(supplementary->passwd[i],
                SERVICE_PASSWD_DEFAULT_PASSWD,
                SERVICE_PASSWD_MAX_PASSWD_LENGTH);
    }
}

static void
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

static void
asupplementary_callbarring_init(ASupplementaryService supplementary)
{
    int i;
    int j;

    for (i = 0; i <= CALL_BARRING_MAX_PROGRAM; i++) {
        for (j = 0; j <= CALL_BARRING_MAX_CLASSX_OFFSET; j++) {
            supplementary->call_barring[i][j].enabled = false;
        }
    }
}

ASupplementaryService
asupplementary_create(int base_port, int instance_id)
{
    ASupplementaryService supplementary = &_s_supplementary[instance_id];

    asupplementary_passwd_init(supplementary);
    asupplementary_callforward_init(supplementary);
    asupplementary_callbarring_init(supplementary);

    return supplementary;
}

void
asupplementary_destroy(ASupplementaryService supplementary)
{
    asupplementary_callforward_destroy(supplementary);
}

bool
asupplementary_check_passwd(ASupplementaryService supplementary,
                            AServiceType type, char *passwd)
{
    char *target_passwd = NULL;
    switch (type) {
        case A_SERVICE_TYPE_CALL_BARRING:
            target_passwd = supplementary->passwd[A_SERVICE_TYPE_CALL_BARRING];
            break;
    }

    if (!target_passwd || !passwd ||
        strncmp(target_passwd, passwd, SERVICE_PASSWD_MAX_PASSWD_LENGTH)) {
        return false;
    }

    return true;
}

bool
asupplementary_set_passwd(ASupplementaryService supplementary,
                          AServiceType type, char *passwd)
{
    // According to TS 22.004 clause 5.2, the password of a supplementary
    // service should consist 4 digits in the range 0000 to 9999.
    if (!passwd || strlen(passwd) != 4) {
        return false;
    }

    int passwdValue = atoi(passwd);
    if (passwdValue < 0 || passwdValue > 9999) {
        return false;
    }

    char *target_passwd = NULL;
    switch (type) {
        case A_SERVICE_TYPE_CALL_BARRING:
            target_passwd = supplementary->passwd[A_SERVICE_TYPE_CALL_BARRING];
            break;
        default:
            return false;
    }

    strncpy(target_passwd, passwd, SERVICE_PASSWD_MAX_PASSWD_LENGTH);
    return true;
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

bool
asupplementary_set_call_barring(ASupplementaryService supplementary,
                                ACallBarringProgram program,
                                AServiceClassxOffset classx_offset,
                                int enable)
{
    if (program < 0 ||
        program > CALL_BARRING_MAX_PROGRAM ||
        classx_offset < 0 ||
        classx_offset > CALL_BARRING_MAX_CLASSX_OFFSET) {
        return false;
    }

    if (enable) {
        if (program == A_CALL_BARRING_PROGRAM_AO ||
            program == A_CALL_BARRING_PROGRAM_OI ||
            program == A_CALL_BARRING_PROGRAM_OX ) {
            // According to TS 22.088 clause 1.3.5, there can be only one active
            // outgoing call barring program.
            supplementary->call_barring[A_CALL_BARRING_PROGRAM_AO][classx_offset].enabled = false;
            supplementary->call_barring[A_CALL_BARRING_PROGRAM_OI][classx_offset].enabled = false;
            supplementary->call_barring[A_CALL_BARRING_PROGRAM_OX][classx_offset].enabled = false;

        } else if (program == A_CALL_BARRING_PROGRAM_AI ||
                   program == A_CALL_BARRING_PROGRAM_IR ) {
            // According to TS 22.088 clause 2.3.5, there can be only one active
            // incoming call barring program.
            supplementary->call_barring[A_CALL_BARRING_PROGRAM_AI][classx_offset].enabled = false;
            supplementary->call_barring[A_CALL_BARRING_PROGRAM_IR][classx_offset].enabled = false;
        }
    }

    supplementary->call_barring[program][classx_offset].enabled = enable;
    return true;
}

bool
asupplementary_is_call_barring_enabled(ASupplementaryService supplementary,
                                       ACallBarringProgram program,
                                       AServiceClassxOffset classx_offset)
{
    if (program < 0 ||
        program > CALL_BARRING_MAX_PROGRAM ||
        classx_offset < 0 ||
        classx_offset > CALL_BARRING_MAX_CLASSX_OFFSET) {
        return false;
    }

    return supplementary->call_barring[program][classx_offset].enabled;
}
