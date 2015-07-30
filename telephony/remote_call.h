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
#ifndef _REMOTE_CALL_H
#define _REMOTE_CALL_H

#include "sms.h"
#include "android_modem.h"

/* convert a AModem into a remote phone number, -1 on error */
extern int         remote_number_from_modem( AModem  modem );

/* convert a remote phone number into a remote console port & instance id,
 * returns > 0 if success, -1 otherwise. */
extern int         remote_number_to_port( int   number,
                                          int*  port,
                                          int*  instance_id);

extern int         remote_number_string_to_port( const char*  number,
                                                 AModem       modem,
                                                 int*         port,
                                                 int*         instance_id);

typedef void   (*RemoteResultFunc)( void*  opaque, int  success );

typedef enum {
    REMOTE_CALL_DIAL = 0,
    REMOTE_CALL_BUSY,
    REMOTE_CALL_HANGUP,
    REMOTE_CALL_HOLD,
    REMOTE_CALL_ACCEPT,
    REMOTE_CALL_SMS
} RemoteCallType;

/* call this function when you need to dial a remote voice call.
 * this will try to connect to a remote emulator. the result function
 * is called to indicate success or failure after some time.
 *
 * returns 0 if the number is to a remote phone, or -1 otherwise
 */
extern  int     remote_call_dial( const char*       to_number,
                                  AModem            from_modem,
                                  RemoteResultFunc  result_func,
                                  void*             result_opaque );

/* call this function to send a SMS to a remote emulator */
extern int      remote_call_sms( const char*   number, AModem  from_modem, SmsPDU  pdu );

/* call this function to indicate that you're busy to a remote caller */
extern void     remote_call_other( const char*  to_number, AModem  from_modem, RemoteCallType  type );

extern void     remote_call_cancel( const char*  to_number, AModem  from_modem );

#endif /* _REMOTE_CALL_H */
