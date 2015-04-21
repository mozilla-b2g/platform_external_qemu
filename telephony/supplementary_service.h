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

/* Supplementary service classx, see +CCFC commands */
typedef enum {
    A_SERVICE_CLASSX_OFFSET_VOICE = 0,
    A_SERVICE_CLASSX_OFFSET_DATA,
    A_SERVICE_CLASSX_OFFSET_FAX,
    A_SERVICE_CLASSX_OFFSET_SMS,
    A_SERVICE_CLASSX_OFFSET_DATA_SYNC,
    A_SERVICE_CLASSX_OFFSET_DATA_ASYNC,
    A_SERVICE_CLASSX_OFFSET_PACKET,
    A_SERVICE_CLASSX_OFFSET_PAD
} AServiceClassxOffset;

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
#define CALL_FORWARDING_MAX_CLASSX_OFFSET A_SERVICE_CLASSX_OFFSET_PAD

typedef struct ASupplementaryServiceRec_*    ASupplementaryService;

/* Supplementary service types */
typedef enum {
    A_SERVICE_TYPE_CALL_BARRING = 0
} AServiceType;

/* Passwords for supplementary services */
#define SERVICE_PASSWD_DEFAULT_PASSWD       "0000"
#define SERVICE_PASSWD_MAX_PASSWD_LENGTH    32
#define SERVICE_PASSWD_MAX_TYPE             A_SERVICE_TYPE_CALL_BARRING

extern bool  asupplementary_check_passwd( ASupplementaryService supplementary,
                                          AServiceType type, char *passwd);
extern bool  asupplementary_set_passwd( ASupplementaryService supplementary,
                                        AServiceType type, char *passwd);

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

/* call barring group, see TS 22.088 */
typedef enum {
    A_CALL_BARRING_PROGRAM_AO = 0,
    A_CALL_BARRING_PROGRAM_OI,
    A_CALL_BARRING_PROGRAM_OX,
    A_CALL_BARRING_PROGRAM_AI,
    A_CALL_BARRING_PROGRAM_IR
} ACallBarringProgram;

#define CALL_BARRING_MAX_PROGRAM        A_CALL_BARRING_PROGRAM_IR
#define CALL_BARRING_MAX_CLASSX_OFFSET  A_SERVICE_CLASSX_OFFSET_PAD

extern bool  asupplementary_set_call_barring( ASupplementaryService supplementary,
                                              ACallBarringProgram program,
                                              AServiceClassxOffset classx_offset, int enable);
extern bool  asupplementary_is_call_barring_enabled(ASupplementaryService supplementary,
                                                    ACallBarringProgram program,
                                                    AServiceClassxOffset classx_offset);
#endif /* _android_supplementary_service_h */
