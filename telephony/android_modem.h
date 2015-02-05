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
#ifndef _android_modem_h_
#define _android_modem_h_

#include "sim_card.h"
#include "sms.h"

/** MODEM OBJECT
 **/
typedef struct AModemRec_*    AModem;
extern int         amodem_num_devices;

/* a function used by the modem to send unsolicited messages to the channel controller */
typedef void (*AModemUnsolFunc)( void*  opaque, const char*  message );

extern AModem      amodem_create( int  base_port, int  instance_id, AModemUnsolFunc  unsol_func, void*  unsol_opaque );
extern void        amodem_set_legacy( AModem  modem );
extern void        amodem_destroy( AModem  modem );
extern AModem      amodem_get_instance( int  instance_id );

/* send a command to the modem, returns non-zero if in sms mode */
extern int          amodem_send( AModem  modem, const char*  cmd );

/* simulate the receipt on an incoming SMS message */
extern void         amodem_receive_sms( AModem  modem, SmsPDU  pdu );

/* simulate the receipt on an incoming Cell Broadcast message */
extern void         amodem_receive_cbs( AModem  modem, SmsPDU  pdu );

/** RADIO STATE
 **/
typedef enum {
    A_RADIO_STATE_OFF = 0,          /* Radio explictly powered off (eg CFUN=0) */
    A_RADIO_STATE_ON,               /* Radio on */
} ARadioState;

extern ARadioState  amodem_get_radio_state( AModem modem );
extern void         amodem_set_radio_state( AModem modem, ARadioState state );

/* Get the received signal strength indicator and bit error rate */
extern void         amodem_get_signal_strength( AModem modem, int* rssi, int* ber );
/* Set the received signal strength indicator and bit error rate */
extern void         amodem_set_signal_strength( AModem modem, int rssi, int ber );
/* Get the received LTE rxlev, rsrp and rssnr */
extern void         amodem_get_lte_signal_strength( AModem modem, int* rxlev, int* rsrp, int* rssnr );
/* Set the received LTE rxlev, rsrp and rssnr */
extern void         amodem_set_lte_signal_strength( AModem modem, int rxlev, int rsrp, int rssnr );

/** SIM CARD STATUS
 **/
extern ASimCard    amodem_get_sim( AModem  modem );

/** VOICE AND DATA NETWORK REGISTRATION
 **/

/* 'mode' for +CREG/+CGREG commands */
typedef enum {
    A_REGISTRATION_UNSOL_DISABLED     = 0,
    A_REGISTRATION_UNSOL_ENABLED      = 1,
    A_REGISTRATION_UNSOL_ENABLED_FULL = 2
} ARegistrationUnsolMode;

/* 'stat' for +CREG/+CGREG commands */
typedef enum {
    A_REGISTRATION_UNREGISTERED = 0,
    A_REGISTRATION_HOME = 1,
    A_REGISTRATION_SEARCHING,
    A_REGISTRATION_DENIED,
    A_REGISTRATION_UNKNOWN,
    A_REGISTRATION_ROAMING
} ARegistrationState;

typedef enum {
    A_DATA_NETWORK_UNKNOWN = 0,
    A_DATA_NETWORK_GPRS,
    A_DATA_NETWORK_EDGE,
    A_DATA_NETWORK_UMTS,
    A_DATA_NETWORK_LTE,
    A_DATA_NETWORK_CDMA1X,
    A_DATA_NETWORK_EVDO, // TODO: Should REV0, REVA and REVB be added?
} ADataNetworkType;
// TODO: Merge the usage of these two structs and rename ADataNetworkType
typedef enum {
    A_TECH_GSM = 0,
    A_TECH_WCDMA,
    A_TECH_CDMA,
    A_TECH_EVDO,
    A_TECH_LTE,
    A_TECH_UNKNOWN,
    A_TECH_PREFERRED = 8 // Be used in A_PREFERRED_MASK* as the preferred bit.
} AModemTech;

typedef enum {
    A_PREFERRED_MASK_GSM_WCDMA_PREF = 0,
    A_PREFERRED_MASK_GSM,
    A_PREFERRED_MASK_WCDMA,
    A_PREFERRED_MASK_GSM_WCDMA,
    A_PREFERRED_MASK_CDMA_EVDO,
    A_PREFERRED_MASK_CDMA,
    A_PREFERRED_MASK_EVDO,
    A_PREFERRED_MASK_GSM_WCDMA_CDMA_EVDO,
    // TODO: Add Lte related preferred mask
    A_PREFERRED_MASK_UNKNOWN // This must always be the last value in the enum
} AModemPreferredMask;

typedef enum {
    A_SUBSCRIPTION_NVRAM = 0,
    A_SUBSCRIPTION_RUIM,
    A_SUBSCRIPTION_UNKNOWN // This must always be the last value in the enum
} ACdmaSubscriptionSource;

typedef enum {
    A_ROAMING_PREF_HOME = 0,
    A_ROAMING_PREF_AFFILIATED,
    A_ROAMING_PREF_ANY,
    A_ROAMING_PREF_UNKNOWN // This must always be the last value in the enum
} ACdmaRoamingPref;

extern ARegistrationUnsolMode  amodem_get_voice_unsol_mode( AModem  modem );

extern ARegistrationState  amodem_get_voice_registration( AModem  modem );
extern void                amodem_set_voice_registration( AModem  modem, ARegistrationState    state );

extern ARegistrationState  amodem_get_data_registration( AModem  modem );
extern void                amodem_set_data_registration( AModem  modem, ARegistrationState    state );
extern void                amodem_set_data_network_type( AModem  modem, ADataNetworkType   type );

extern ADataNetworkType    android_parse_network_type( const char*  speed );
extern AModemTech          android_parse_modem_tech( const char*  tech );
extern const char*         android_get_modem_tech_name( AModemTech tech );
extern AModemPreferredMask android_parse_modem_preferred_mask( const char* mask );
extern const char*         android_get_modem_preferred_mask_name( AModemPreferredMask mask );
extern void                amodem_set_cdma_subscription_source( AModem modem, ACdmaSubscriptionSource ssource );
extern void                amodem_set_cdma_prl_version( AModem modem, int prlVersion);

extern SmsAddress          amodem_get_smsc_address( AModem  modem );
extern int                 amodem_set_smsc_address( AModem  modem, const char *smsc, unsigned char toa );

extern AModemTech          amodem_get_technology( AModem modem );
extern AModemPreferredMask amodem_get_preferred_mask( AModem modem );
extern int                 amodem_set_technology( AModem modem, AModemTech technology, AModemPreferredMask preferredMask );

/** OPERATOR TYPES
 **/
typedef enum {
    A_OPERATOR_HOME = 0,
    A_OPERATOR_ROAMING,
    A_OPERATOR_MAX  /* don't remove */
} AOperatorIndex;

/** OPERATOR NAMES
 **/
typedef enum {
    A_NAME_LONG = 0,
    A_NAME_SHORT,
    A_NAME_NUMERIC,
    A_NAME_MAX  /* don't remove */
} ANameIndex;

/* retrieve current operator name into user-provided buffer. returns number of writes written, including terminating zero */
extern int   amodem_get_operator_name ( AModem  modem, ANameIndex  index, char*  buffer, int  buffer_size );
/* retrieve specified operator name into user-provided buffer. returns number of writes written, including terminating zero */
extern int   amodem_get_operator_name_ex ( AModem  modem, AOperatorIndex, ANameIndex  index, char*  buffer, int  buffer_size );

/* reset one current operator name from a user-provided buffer, set buffer_size to -1 for zero-terminated strings */
extern void  amodem_set_operator_name( AModem  modem, ANameIndex  index, const char*  buffer, int  buffer_size );
/* reset one specified operator name from a user-provided buffer, set buffer_size to -1 for zero-terminated strings */
extern void  amodem_set_operator_name_ex( AModem  modem, AOperatorIndex, ANameIndex  index, const char*  buffer, int  buffer_size );

/** CALL STATES
 **/

typedef enum {
    A_CALL_OUTBOUND = 0,
    A_CALL_INBOUND  = 1,
} ACallDir;

typedef enum {
    A_CALL_ACTIVE = 0,
    A_CALL_HELD,
    A_CALL_DIALING,
    A_CALL_ALERTING,
    A_CALL_INCOMING,
    A_CALL_WAITING
} ACallState;

typedef enum {
    A_CALL_VOICE = 0,
    A_CALL_DATA,
    A_CALL_FAX,
    A_CALL_UNKNOWN = 9
} ACallMode;

#define  A_CALL_NUMBER_MAX_SIZE  16
/* TS 24.096 clause 4.1 */
#define  A_CALL_NAME_MAX_SIZE  80

#define CALL_FAIL_UNOBTAINABLE_NUMBER 1
#define CALL_FAIL_NORMAL 16
#define CALL_FAIL_BUSY 17

typedef struct {
    int         id;
    ACallDir    dir;
    ACallState  state;
    ACallMode   mode;
    int         multi;
    char        number[ A_CALL_NUMBER_MAX_SIZE+1 ];
    int         numberPresentation;
} ACallRec, *ACall;

extern int    amodem_get_call_count( AModem  modem );
extern ACall  amodem_get_call( AModem  modem,  int  index );
extern ACall  amodem_find_call_by_number( AModem  modem, const char*  number );
extern int    amodem_add_inbound_call( AModem  modem, const char*  number, const int  numPresentation, const char*  name, const int  namePresentation );
extern int    amodem_update_call( AModem  modem, const char*  number, ACallState  state );
extern int    amodem_disconnect_call( AModem  modem, const char*  number );
extern int    amodem_remote_call_busy( AModem  modem, const char*  number );
extern void   amodem_send_stk_unsol_proactive_command( AModem  modem, const char*  stkCmdPdu );

/** Cell Location
 **/

extern void amodem_get_gsm_location( AModem modem, int* lac, int* ci );
extern void amodem_set_gsm_location( AModem modem, int lac, int ci );

/** Base Port & Instance ID & Phone Prefix
 **/

extern int amodem_get_base_port( AModem  modem );
extern int amodem_get_instance_id( AModem  modem );

/**/

#endif /* _android_modem_h_ */
