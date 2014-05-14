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
#ifndef _android_supplementary_service_h
#define _android_supplementary_service_h

/* call forwarding service classx, see +CCFC commands */
typedef enum {
    A_CALL_FORWARDING_CLASSX_OFFSET_VOICE = 0,
    A_CALL_FORWARDING_CLASSX_OFFSET_DATA,
    A_CALL_FORWARDING_CLASSX_OFFSET_FAX,
    A_CALL_FORWARDING_CLASSX_OFFSET_SMS,
    A_CALL_FORWARDING_CLASSX_OFFSET_DATA_SYNC,
    A_CALL_FORWARDING_CLASSX_OFFSET_DATA_ASYNC,
    A_CALL_FORWARDING_CLASSX_OFFSET_PACKET,
    A_CALL_FORWARDING_CLASSX_OFFSET_PAD
} ACallForwardingClassxOffset;

/* call forwarding condition reason, see +CCFC commands */
typedef enum {
    A_CALL_FORWARDING_REASON_UNCONDITIONAL = 0,
    A_CALL_FORWARDING_REASON_BUSY,
    A_CALL_FORWARDING_REASON_NO_REPLY,
    A_CALL_FORWARDING_REASON_NOT_REACHABLE,
    A_CALL_FORWARDING_REASON_ALL,
    A_CALL_FORWARDING_REASON_ALL_CONDITIONAL
} ACallForwardingReason;

/* call forwarding mode, see +CCFC commands */
typedef enum {
    A_CALL_FORWARDING_MODE_DISABLE = 0,
    A_CALL_FORWARDING_MODE_ENABLE,
    A_CALL_FORWARDING_MODE_QUERY,
    A_CALL_FORWARDING_MODE_REGISTRATION,
    A_CALL_FORWARDING_MODE_ERASURE
} ACallForwardingMode;

#define CALL_FORWARDING_MAX_NUMBERS       32
#define CALL_FORWARDING_MAX_REASON        A_CALL_FORWARDING_REASON_NOT_REACHABLE
#define CALL_FORWARDING_MAX_CLASSX_OFFSET A_CALL_FORWARDING_CLASSX_OFFSET_PAD

typedef struct ASupplementaryServiceRec_*    ASupplementaryService;

typedef struct ACallForwardRec_ {
    bool  enabled;
    char  number[CALL_FORWARDING_MAX_NUMBERS + 1];
    int   toa;
    int   time;
} ACallForwardRec, *ACallForward;

extern ASupplementaryService  asupplementary_create( int base_port, int instance_id );
extern void                   asupplementary_destroy( ASupplementaryService sim );

extern int  asupplementary_set_call_forward( ASupplementaryService supplementary,
                                             int reason, int classx_offset,
                                             bool enabled, char* number, int toa,
                                             int time);
extern int  asupplementary_remove_call_forward( ASupplementaryService supplementary,
                                                int reason, int classx_offset );
extern ACallForward  asupplementary_get_call_foward( ASupplementaryService supplementary,
                                                     int reason, int classx_offset );

#endif /* _android_supplementary_service_h */
