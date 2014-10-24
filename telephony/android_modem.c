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
#include "android/android.h"
#include "android_modem.h"
#include "android/config.h"
#include "android/config/config.h"
#include "android/snapshot.h"
#include "android/utils/debug.h"
#include "android/utils/timezone.h"
#include "android/utils/system.h"
#include "android/utils/bufprint.h"
#include "android/utils/path.h"
#include "hw/hw.h"
#include "qemu-common.h"
#include "sim_card.h"
#include "supplementary_service.h"
#include "sysdeps.h"
#include <memory.h>
#include <stdarg.h>
#include <time.h>
#include <assert.h>
#include <stdio.h>
#include <netinet/in.h>
#include "sms.h"
#include "net.h"
#include "remote_call.h"
#include "slirp.h"

#define  DEBUG  1

#if  1
#  define  D_ACTIVE  VERBOSE_CHECK(modem)
#else
#  define  D_ACTIVE  DEBUG
#endif

#if 1
#  define  R_ACTIVE  VERBOSE_CHECK(radio)
#else
#  define  R_ACTIVE  DEBUG
#endif

#if DEBUG
#  define  D(...)   do { if (D_ACTIVE) fprintf( stderr, __VA_ARGS__ ); } while (0)
#  define  R(...)   do { if (R_ACTIVE) fprintf( stderr, __VA_ARGS__ ); } while (0)
#else
#  define  D(...)   ((void)0)
#  define  R(...)   ((void)0)
#endif

#define  CALL_DELAY_DIAL   1000
#define  CALL_DELAY_ALERT  1000

/* the Android GSM stack checks that the operator's name has changed
 * when roaming is on. If not, it will not update the Roaming status icon
 *
 * this means that we need to emulate two distinct operators:
 * - the first one for the 'home' registration state, must also correspond
 *   to the emulated user's IMEI
 *
 * - the second one for the 'roaming' registration state, must have a
 *   different name and MCC/MNC
 */

#define  OPERATOR_HOME_INDEX 0
#define  OPERATOR_HOME_MCC   310
#define  OPERATOR_HOME_MNC   260
#define  OPERATOR_HOME_NAME  "Android"
#define  OPERATOR_HOME_MCCMNC  STRINGIFY(OPERATOR_HOME_MCC) \
                               STRINGIFY(OPERATOR_HOME_MNC)

#define  OPERATOR_ROAMING_INDEX 1
#define  OPERATOR_ROAMING_MCC   310
#define  OPERATOR_ROAMING_MNC   295
#define  OPERATOR_ROAMING_NAME  "TelKila"
#define  OPERATOR_ROAMING_MCCMNC  STRINGIFY(OPERATOR_ROAMING_MCC) \
                                  STRINGIFY(OPERATOR_ROAMING_MNC)

#define  SMSC_ADDRESS           "+123456789"

static const struct {
    const char* name;
    AModemTech  tech;
} techs[] = {
    { "gsm",   A_TECH_GSM },
    { "wcdma", A_TECH_WCDMA },
    { "cdma",  A_TECH_CDMA },
    { "evdo",  A_TECH_EVDO },
    { "lte",   A_TECH_LTE },
    { NULL,    A_TECH_UNKNOWN }
};

static const struct {
    const char*         name;
    AModemPreferredMask mask;
    int                 value;
} preferred_masks[] = {
    { "gsm/wcdma",
      A_PREFERRED_MASK_GSM_WCDMA_PREF,      (1 << A_TECH_GSM) | (1 << A_TECH_WCDMA + A_TECH_PREFERRED) },
    { "gsm",
      A_PREFERRED_MASK_GSM,                 (1 << A_TECH_GSM) },
    { "wcdma",
      A_PREFERRED_MASK_WCDMA,               (1 << A_TECH_WCDMA) },
    { "gsm/wcdma-auto",
      A_PREFERRED_MASK_GSM_WCDMA,           (1 << A_TECH_GSM) | (1 << A_TECH_WCDMA) },
    { "cdma/evdo",
      A_PREFERRED_MASK_CDMA_EVDO,           (1 << A_TECH_CDMA) | (1 << A_TECH_EVDO) },
    { "cdma",
      A_PREFERRED_MASK_CDMA,                (1 << A_TECH_CDMA) },
    { "evdo",
      A_PREFERRED_MASK_EVDO,                (1 << A_TECH_EVDO) },
    { "gsm/wcdma/cdma/evdo",
      A_PREFERRED_MASK_GSM_WCDMA_CDMA_EVDO, (1 << A_TECH_GSM) | (1 << A_TECH_WCDMA) |
                                            (1 << A_TECH_CDMA) | (1 << A_TECH_EVDO) },
    { NULL,
      A_PREFERRED_MASK_UNKNOWN,             -1 }
};

int amodem_num_devices = 0;

static int _amodem_switch_technology(AModem modem, AModemTech newtech, int32_t newpreferred);
static int _amodem_set_cdma_subscription_source( AModem modem, ACdmaSubscriptionSource ss);
static int _amodem_set_cdma_prl_version( AModem modem, int prlVersion);
static const char* handleSignalStrength( const char*  cmd, AModem  modem);

#if DEBUG
static const char*  quote( const char*  line )
{
    static char  temp[1024];
    const char*  hexdigits = "0123456789abcdef";
    char*        p = temp;
    int          c;

    while ((c = *line++) != 0) {
        c &= 255;
        if (c >= 32 && c < 127) {
            *p++ = c;
        }
        else if (c == '\r') {
            memcpy( p, "<CR>", 4 );
            p += 4;
        }
        else if (c == '\n') {
            memcpy( p, "<LF>", 4 );strcat( p, "<LF>" );
            p += 4;
        }
        else {
            p[0] = '\\';
            p[1] = 'x';
            p[2] = hexdigits[ (c) >> 4 ];
            p[3] = hexdigits[ (c) & 15 ];
            p += 4;
        }
    }
    *p = 0;
    return temp;
}
#endif

extern AModemTech
android_parse_modem_tech( const char * tech )
{
    int  nn;

    for (nn = 0; techs[nn].name; nn++) {
        if (!strcmp(tech, techs[nn].name))
            return techs[nn].tech;
    }
    /* not found */
    return A_TECH_UNKNOWN;
}

extern const char*
android_get_modem_tech_name( AModemTech tech )
{
    int  nn;

    for (nn = 0; techs[nn].name; nn++) {
        if (techs[nn].tech == tech)
            return techs[nn].name;
    }
    /* not found */
    return NULL;
}

extern AModemPreferredMask
android_parse_modem_preferred_mask( const char* maskName )
{
    int nn;

    for (nn = 0; techs[nn].name; nn++) {
        if (!strcmp(maskName, preferred_masks[nn].name)) {
            return preferred_masks[nn].mask;
        }
    }
    /* not found */
    return A_PREFERRED_MASK_UNKNOWN;
}

extern const char*
android_get_modem_preferred_mask_name( AModemPreferredMask mask )
{
    int nn;

    for (nn = 0; preferred_masks[nn].name; nn++) {
        if (preferred_masks[nn].mask == mask) {
            return preferred_masks[nn].name;
        }
    }
    /* not found */
    return NULL;
}

static AModemPreferredMask
android_get_modem_preferred_mask(int32_t maskValue)
{
    int nn;

    for (nn = 0; preferred_masks[nn].name; nn++) {
        if (preferred_masks[nn].value == maskValue) {
            return preferred_masks[nn].mask;
        }
    }
    /* not found */
    return A_PREFERRED_MASK_UNKNOWN;
}

extern ADataNetworkType
android_parse_network_type( const char*  speed )
{
    const struct { const char* name; ADataNetworkType  type; }  types[] = {
        { "gprs",  A_DATA_NETWORK_GPRS },
        { "edge",  A_DATA_NETWORK_EDGE },
        { "umts",  A_DATA_NETWORK_UMTS },
        { "hsdpa", A_DATA_NETWORK_UMTS },  /* not handled yet by Android GSM framework */
        { "full",  A_DATA_NETWORK_UMTS },
        { "lte",   A_DATA_NETWORK_LTE },
        { "cdma",  A_DATA_NETWORK_CDMA1X },
        { "evdo",  A_DATA_NETWORK_EVDO },
        { NULL, 0 }
    };
    int  nn;

    for (nn = 0; types[nn].name; nn++) {
        if (!strcmp(speed, types[nn].name))
            return types[nn].type;
    }
    /* not found, be conservative */
    return A_DATA_NETWORK_GPRS;
}

/* Operator selection mode, see +COPS commands */
typedef enum {
    A_SELECTION_AUTOMATIC,
    A_SELECTION_MANUAL,
    A_SELECTION_DEREGISTRATION,
    A_SELECTION_SET_FORMAT,
    A_SELECTION_MANUAL_AUTOMATIC
} AOperatorSelection;

/* Operator status, see +COPS commands */
typedef enum {
    A_STATUS_UNKNOWN = 0,
    A_STATUS_AVAILABLE,
    A_STATUS_CURRENT,
    A_STATUS_DENIED
} AOperatorStatus;

typedef struct {
    AOperatorStatus  status;
    char             name[3][16];
} AOperatorRec, *AOperator;

typedef struct AVoiceCallRec {
    ACallRec    call;
    SysTimer    timer;
    AModem      modem;
    char        is_remote;
} AVoiceCallRec, *AVoiceCall;

#define  MAX_OPERATORS  4

typedef enum {
    A_DATA_IP = 0,
    A_DATA_PPP
} ADataType;

typedef struct {
    struct in_addr  in;
} AInetAddrRec, *AInetAddr;

#define  A_DATA_APN_SIZE  32

struct _ADataNetRec;

typedef struct {
    int        id;
    int        active;
    ADataType  type;
    char       apn[ A_DATA_APN_SIZE ];
    AInetAddrRec  addr;

    struct _ADataNetRec* net;
} ADataContextRec, *ADataContext;

/* AT+CGCONTRDP can only report two DNS server addresses -- primary and
 * secondary.  See 3GPP TS 27.007 subclause 10.1.23 "PDP context read dynamic
 * parameters +CGCONTRDP".
 */
#define NUM_DNS_PER_RMNET 2

typedef struct _ADataNetRec {
    struct NICInfo*  nd;
    ADataContext     context;
    AInetAddrRec     addr, gw, dns[ NUM_DNS_PER_RMNET ];
} ADataNetRec, *ADataNet;

/* the spec says that there can only be a max of 4 contexts */
#define  MAX_DATA_CONTEXTS  4

static const char* amodem_teardown_pdp( ADataContext context );

/* According to 3GPP 22.083 clause 2.2.1, 3GPP 22.084 clause 1.2.1 and 3GPP
 * 22.030 clause 6.5.5.6, the case of the maximum number is reached "when
 * there comes an incoming call while we have already one active(held)
 * conference call (with 5 remote parties) and one held(active) single call."
 * The maximum number of voice calls is therefore 7.
 */
#define  MAX_CALLS          7
#define  MAX_EMERGENCY_NUMBERS 16


#define  A_MODEM_SELF_SIZE   3


typedef struct AModemRec_
{
    /* Legacy support */
    char          supportsNetworkDataType;

    /* Radio state */
    ARadioState   radio_state;
    int           area_code;
    int           cell_id;
    int           base_port;
    int           instance_id;

    int           rssi;
    int           ber;

    /* LTE signal strength */
    int           rxlev;
    int           rsrp;
    int           rssnr;

    /* SMS */
    int           wait_sms;

    /* SIM card */
    ASimCard      sim;

    /* Supplementary Service */
    ASupplementaryService    supplementary;

    /* voice and data network registration */
    ARegistrationUnsolMode   voice_mode;
    ARegistrationState       voice_state;
    ARegistrationUnsolMode   data_mode;
    ARegistrationState       data_state;
    ADataNetworkType         data_network;

    /* operator names */
    AOperatorSelection  oper_selection_mode;
    ANameIndex          oper_name_index;
    int                 oper_index;
    int                 oper_count;
    AOperatorRec        operators[ MAX_OPERATORS ];

    /* data connection contexts */
    ADataContextRec     data_contexts[ MAX_DATA_CONTEXTS ];

    /* active calls */
    AVoiceCallRec       calls[ MAX_CALLS ];
    int                 call_count;

    /* multiparty calls count */
    int                 multi_count;

    /* last call fail cause */
    int                 last_call_fail_cause;

    /* unsolicited callback */  /* XXX: TODO: use this */
    AModemUnsolFunc     unsol_func;
    void*               unsol_opaque;

    SmsReceiver         sms_receiver;

    int                 out_size;
    char                out_buff[1024];

    /*
     * Hold non-volatile ram configuration for modem
     */
    AConfig *nvram_config;
    char *nvram_config_filename;

    AModemTech technology;
    /*
     * This is are really 4 byte-sized prioritized masks.
     * Byte order gives the priority for the specific bitmask.
     * Each bit position in each of the masks is indexed by the different
     * A_TECH_XXXX values.
     * e.g. 0x01 means only GSM is set (bit index 0), whereas 0x0f
     * means that GSM,WCDMA,CDMA and EVDO are set
     */
    int32_t preferred_mask;
    ACdmaSubscriptionSource subscription_source;
    ACdmaRoamingPref roaming_pref;
    int in_emergency_mode;
    int prl_version;

    const char *emergency_numbers[MAX_EMERGENCY_NUMBERS];

    // SMSC address
    SmsAddressRec   smsc_address;
} AModemRec;


static void
amodem_unsol_buffered( AModem  modem, const char* message )
{
    if (modem->unsol_func) {
        modem->unsol_func( modem->unsol_opaque, message );
    }
}

static void
amodem_unsol( AModem  modem, const char* format, ... )
{
    va_list  args;
    va_start(args, format);
    vsnprintf( modem->out_buff, sizeof(modem->out_buff), format, args );
    va_end(args);

    amodem_unsol_buffered( modem, modem->out_buff );
}

void
amodem_receive_sms( AModem  modem, SmsPDU  sms )
{
#define  SMS_UNSOL_HEADER  "+CMT: 0\r\n"

    if (modem->unsol_func) {
        int    len, max;
        char*  p;

        strcpy( modem->out_buff, SMS_UNSOL_HEADER );
        p   = modem->out_buff + (sizeof(SMS_UNSOL_HEADER)-1);
        max = sizeof(modem->out_buff) - 3 - (sizeof(SMS_UNSOL_HEADER)-1);
        len = smspdu_to_hex( sms, p, max );
        if (len > max) /* too long */
            return;
        p[len]   = '\r';
        p[len+1] = '\n';
        p[len+2] = 0;

        R( "SMS>> %s\n", p );

        modem->unsol_func( modem->unsol_opaque, modem->out_buff );
    }
}

void
amodem_receive_cbs( AModem  modem, SmsPDU  cbs )
{
#define  CBS_UNSOL_HEADER  "+CBM: 0\r\n"

    if (!modem->unsol_func) {
        return;
    }

    int    len, max;
    char*  p;

    strcpy( modem->out_buff, CBS_UNSOL_HEADER );
    p   = modem->out_buff + (sizeof(CBS_UNSOL_HEADER)-1);
    max = sizeof(modem->out_buff) - 3 - (sizeof(CBS_UNSOL_HEADER)-1);
    len = smspdu_to_hex( cbs, p, max );
    if (len > max) /* too long */
        return;
    p[len]   = '\r';
    p[len+1] = '\n';
    p[len+2] = 0;

    R( "CBS>> %s\n", p );

    modem->unsol_func( modem->unsol_opaque, modem->out_buff );
}

static const char*
amodem_printf( AModem  modem, const char*  format, ... )
{
    va_list  args;
    va_start(args, format);
    vsnprintf( modem->out_buff, sizeof(modem->out_buff), format, args );
    va_end(args);

    return modem->out_buff;
}

static void
amodem_begin_line( AModem  modem )
{
    modem->out_size = 0;
}

static void
amodem_add_line( AModem  modem, const char*  format, ... )
{
    va_list  args;
    va_start(args, format);
    modem->out_size += vsnprintf( modem->out_buff + modem->out_size,
                                  sizeof(modem->out_buff) - modem->out_size,
                                  format, args );
    va_end(args);
}

static const char*
amodem_end_line( AModem  modem )
{
    modem->out_buff[ modem->out_size ] = 0;
    return modem->out_buff;
}

#define NV_OPER_NAME_INDEX                     "oper_name_index"
#define NV_OPER_INDEX                          "oper_index"
#define NV_SELECTION_MODE                      "selection_mode"
#define NV_OPER_COUNT                          "oper_count"
#define NV_MODEM_TECHNOLOGY                    "modem_technology"
#define NV_PREFERRED_MODE                      "preferred_mode"
#define NV_CDMA_SUBSCRIPTION_SOURCE            "cdma_subscription_source"
#define NV_CDMA_ROAMING_PREF                   "cdma_roaming_pref"
#define NV_IN_ECBM                             "in_ecbm"
#define NV_EMERGENCY_NUMBER_FMT                    "emergency_number_%d"
#define NV_PRL_VERSION                         "prl_version"
#define NV_SREGISTER                           "sregister"
#define NV_MODEM_SMSC_ADDRESS                  "smsc_address"

#define MAX_KEY_NAME 40

static AConfig *
amodem_load_nvram( AModem modem )
{
    AConfig* root = aconfig_node(NULL, NULL);
    D("Using config file: %s\n", modem->nvram_config_filename);
    if (aconfig_load_file(root, modem->nvram_config_filename)) {
        D("Unable to load config\n");
        aconfig_set(root, NV_MODEM_TECHNOLOGY, "gsm");
        aconfig_save_file(root, modem->nvram_config_filename);
    }
    return root;
}

static int
amodem_nvram_get_int( AModem modem, const char *nvname, int defval)
{
    int value;
    char strval[MAX_KEY_NAME + 1];
    char *newvalue;

    value = aconfig_int(modem->nvram_config, nvname, defval);
    snprintf(strval, MAX_KEY_NAME, "%d", value);
    D("Setting value of %s to %d (%s)\n",nvname, value, strval);
    newvalue = strdup(strval);
    if (!newvalue) {
        newvalue = "";
    }
    aconfig_set(modem->nvram_config, nvname, newvalue);

    return value;
}

const char *
amodem_nvram_get_str( AModem modem, const char *nvname, const char *defval)
{
    const char *value;

    value = aconfig_str(modem->nvram_config, nvname, defval);
    D("Setting value of %s to %s\n",nvname, value);

    if (!value) {
        if (!defval)
            return NULL;
        value = defval;
    }

    aconfig_set(modem->nvram_config, nvname, value);

    return value;
}

static ACdmaSubscriptionSource _amodem_get_cdma_subscription_source( AModem modem )
{
   int iss = -1;
   iss = amodem_nvram_get_int( modem, NV_CDMA_SUBSCRIPTION_SOURCE, A_SUBSCRIPTION_RUIM );
   if (iss >= A_SUBSCRIPTION_UNKNOWN || iss < 0) {
       iss = A_SUBSCRIPTION_RUIM;
   }

   return iss;
}

static ACdmaRoamingPref _amodem_get_cdma_roaming_preference( AModem modem )
{
   int rp = -1;
   rp = amodem_nvram_get_int( modem, NV_CDMA_ROAMING_PREF, A_ROAMING_PREF_ANY );
   if (rp >= A_ROAMING_PREF_UNKNOWN || rp < 0) {
       rp = A_ROAMING_PREF_ANY;
   }

   return rp;
}

static void
amodem_reset( AModem  modem )
{
    const char *tmp;
    int i;
    modem->nvram_config = amodem_load_nvram(modem);
    modem->radio_state = A_RADIO_STATE_OFF;
    modem->wait_sms    = 0;

    modem->rssi= 7;    // Two signal strength bars
    modem->ber = 99;   // Means 'unknown'

    modem->rxlev = 99;    // Not known or not detectable
    modem->rsrp  = 65535; // Denotes invalid value
    modem->rssnr = 65535; // Denotes invalid value

    modem->oper_name_index     = amodem_nvram_get_int(modem, NV_OPER_NAME_INDEX, 2);
    modem->oper_selection_mode = amodem_nvram_get_int(modem, NV_SELECTION_MODE, A_SELECTION_AUTOMATIC);
    modem->oper_index          = amodem_nvram_get_int(modem, NV_OPER_INDEX, 0);
    modem->oper_count          = amodem_nvram_get_int(modem, NV_OPER_COUNT, 2);
    modem->in_emergency_mode   = amodem_nvram_get_int(modem, NV_IN_ECBM, 0);
    modem->prl_version         = amodem_nvram_get_int(modem, NV_PRL_VERSION, 0);

    modem->emergency_numbers[0] = "911";
    char key_name[MAX_KEY_NAME + 1];
    for (i = 1; i < MAX_EMERGENCY_NUMBERS; i++) {
        snprintf(key_name,MAX_KEY_NAME, NV_EMERGENCY_NUMBER_FMT, i);
        modem->emergency_numbers[i] = amodem_nvram_get_str(modem,key_name, NULL);
    }

    modem->area_code = 0;
    modem->cell_id   = 0;

    strcpy( modem->operators[0].name[0], OPERATOR_HOME_NAME );
    strcpy( modem->operators[0].name[1], OPERATOR_HOME_NAME );
    strcpy( modem->operators[0].name[2], OPERATOR_HOME_MCCMNC );

    modem->operators[0].status        = A_STATUS_AVAILABLE;

    strcpy( modem->operators[1].name[0], OPERATOR_ROAMING_NAME );
    strcpy( modem->operators[1].name[1], OPERATOR_ROAMING_NAME );
    strcpy( modem->operators[1].name[2], OPERATOR_ROAMING_MCCMNC );

    modem->operators[1].status        = A_STATUS_AVAILABLE;

    modem->voice_mode   = A_REGISTRATION_UNSOL_ENABLED_FULL;
    modem->voice_state  = A_REGISTRATION_HOME;
    modem->data_mode    = A_REGISTRATION_UNSOL_ENABLED_FULL;
    modem->data_state   = A_REGISTRATION_HOME;
    modem->data_network = A_DATA_NETWORK_UMTS;

    tmp = amodem_nvram_get_str( modem, NV_MODEM_TECHNOLOGY, "gsm" );
    modem->technology = android_parse_modem_tech( tmp );
    if (modem->technology == A_TECH_UNKNOWN) {
        modem->technology = aconfig_int( modem->nvram_config, NV_MODEM_TECHNOLOGY, A_TECH_GSM );
    }
    // Support GSM, WCDMA, CDMA, EvDo
    modem->preferred_mask = amodem_nvram_get_int( modem, NV_PREFERRED_MODE, 0x0f );

    modem->subscription_source = _amodem_get_cdma_subscription_source( modem );
    modem->roaming_pref = _amodem_get_cdma_roaming_preference( modem );

    tmp = amodem_nvram_get_str( modem, NV_MODEM_SMSC_ADDRESS, SMSC_ADDRESS);
    sms_address_from_str( &modem->smsc_address, tmp, strlen(tmp));
}

static AVoiceCall amodem_alloc_call( AModem   modem );
static void amodem_free_call( AModem  modem, AVoiceCall  call, int cause );

#define MODEM_DEV_STATE_SAVE_VERSION 1

static void  android_modem_state_save(QEMUFile *f, void  *opaque)
{
    AModem modem = opaque;

    // TODO: save more than just calls and call_count - rssi, power, etc.

    qemu_put_byte(f, modem->call_count);

    int nn;
    for (nn = modem->call_count - 1; nn >= 0; nn--) {
      AVoiceCall  vcall = modem->calls + nn;
      // Note: not saving timers or remote calls.
      ACall       call  = &vcall->call;
      qemu_put_byte( f, call->dir );
      qemu_put_byte( f, call->state );
      qemu_put_byte( f, call->mode );
      qemu_put_be32( f, call->multi );
      qemu_put_buffer( f, (uint8_t *)call->number, A_CALL_NUMBER_MAX_SIZE+1 );
    }
}

static int  android_modem_state_load(QEMUFile *f, void  *opaque, int version_id)
{
    if (version_id != MODEM_DEV_STATE_SAVE_VERSION)
      return -1;

    AModem modem = opaque;

    // In case there are timers or remote calls.
    int nn;
    for (nn = modem->call_count - 1; nn >= 0; nn--) {
      amodem_free_call( modem, modem->calls + nn, CALL_FAIL_NORMAL );
    }

    int call_count = qemu_get_byte(f);
    for (nn = call_count; nn > 0; nn--) {
      AVoiceCall vcall = amodem_alloc_call( modem );
      ACall      call  = &vcall->call;
      call->dir   = qemu_get_byte( f );
      call->state = qemu_get_byte( f );
      call->mode  = qemu_get_byte( f );
      call->multi = qemu_get_be32( f );
      qemu_get_buffer( f, (uint8_t *)call->number, A_CALL_NUMBER_MAX_SIZE+1 );
    }

    return 0; // >=0 Happy
}

static int _amodem_num_rmnets = 0;
static ADataNetRec _amodem_rmnets[MAX_DATA_CONTEXTS];

static void
amodem_init_rmnets()
{
    static int inited = 0;
    int i, j, k;

    if ( inited ) {
        return;
    }
    inited = 1;

    memset( _amodem_rmnets, 0, sizeof _amodem_rmnets );

    for ( i = 0, j = 0; i < MAX_NICS && j < MAX_DATA_CONTEXTS; i++ ) {
        struct NICInfo* nd = &nd_table[i];
        if ( !nd->used ||
             !nd->name ||
             strncmp( nd->name, "rmnet.", 6 ) ) {
            continue;
        }

        ADataNet net = &_amodem_rmnets[j];

        net->nd = nd;

        int ip = special_addr_ip + 100 + (net - _amodem_rmnets);
        net->addr.in.s_addr = htonl(ip);
        net->gw.in.s_addr = htonl(alias_addr_ip);
        for ( k = 0; k < NUM_DNS_PER_RMNET && k < dns_addr_count; k++ ) {
            ip = dns_addr[k];
            net->dns[k].in.s_addr = htonl(ip);
        }

        /* Data connections are down by default. */
        do_set_link( NULL, nd->name, "down" );

        j++;
    }

    _amodem_num_rmnets = j;
}

static AModemRec   _android_modem[MAX_GSM_DEVICES];

AModem
amodem_create( int  base_port, int instance_id, AModemUnsolFunc  unsol_func, void*  unsol_opaque )
{
    AModem  modem = &_android_modem[instance_id];
    char nvfname[MAX_PATH];
    char *start = nvfname;
    char *end = start + sizeof(nvfname);

    amodem_init_rmnets();

    modem->base_port    = base_port;
    modem->instance_id  = instance_id;
    start = bufprint_config_file( start, end, "modem-nv-ram-" );
    start = bufprint( start, end, "%d-%d", modem->base_port, modem->instance_id );
    modem->nvram_config_filename = strdup( nvfname );

    amodem_reset( modem );
    modem->supportsNetworkDataType = 1;
    modem->unsol_func   = unsol_func;
    modem->unsol_opaque = unsol_opaque;

    modem->sim = asimcard_create(base_port, instance_id);
    modem->supplementary = asupplementary_create(base_port, instance_id);

    // We don't know the exact number of instances to create here, it's
    // controlled by modem_driver_init(). Putting -1 here and register_savevm()
    // will assign a correct SaveStateEntry instance_id for us.
    register_savevm( "android_modem", -1, MODEM_DEV_STATE_SAVE_VERSION,
                      android_modem_state_save,
                      android_modem_state_load, modem);

    aconfig_save_file( modem->nvram_config, modem->nvram_config_filename );
    return  modem;
}

int
amodem_get_base_port( AModem  modem )
{
    return modem->base_port;
}

int
amodem_get_instance_id( AModem  modem )
{
    return modem->instance_id;
}

void
amodem_set_legacy( AModem  modem )
{
    modem->supportsNetworkDataType = 0;
}

void
amodem_destroy( AModem  modem )
{
    asimcard_destroy( modem->sim );
    modem->sim = NULL;

    asupplementary_destroy( modem->supplementary );
    modem->supplementary = NULL;
}


static int
amodem_has_network( AModem  modem )
{
    return !(modem->radio_state == A_RADIO_STATE_OFF   ||
             modem->oper_index < 0                  ||
             modem->oper_index >= modem->oper_count ||
             modem->oper_selection_mode == A_SELECTION_DEREGISTRATION );
}


ARadioState
amodem_get_radio_state( AModem modem )
{
    return modem->radio_state;
}

ASimCard
amodem_get_sim( AModem  modem )
{
    return  modem->sim;
}

ARegistrationState
amodem_get_voice_registration( AModem  modem )
{
    return modem->voice_state;
}

ARegistrationUnsolMode
amodem_get_voice_unsol_mode( AModem  modem )
{
    return modem->voice_mode;
}

void
amodem_set_voice_registration( AModem  modem, ARegistrationState  state )
{
    modem->voice_state = state;

    if (state == A_REGISTRATION_HOME)
        modem->oper_index = OPERATOR_HOME_INDEX;
    else if (state == A_REGISTRATION_ROAMING)
        modem->oper_index = OPERATOR_ROAMING_INDEX;
    else
        modem->oper_index = -1;

    switch (modem->voice_mode) {
        case A_REGISTRATION_UNSOL_ENABLED:
            amodem_unsol( modem, "+CREG: %d,%d\r",
                          modem->voice_mode, modem->voice_state );
            break;

        case A_REGISTRATION_UNSOL_ENABLED_FULL:
            amodem_unsol( modem, "+CREG: %d,%d,\"%04x\",\"%07x\"\r",
                          modem->voice_mode, modem->voice_state,
                          modem->area_code & 0xffff, modem->cell_id & 0xfffffff);
            break;
        default:
            ;
    }
}

ARegistrationState
amodem_get_data_registration( AModem  modem )
{
    return modem->data_state;
}

void
amodem_set_data_registration( AModem  modem, ARegistrationState  state )
{
    modem->data_state = state;

    /* Any active PDP contexts will be automatically deactivated when the
       attachment state changes to detached. */
    if (modem->data_state != A_REGISTRATION_HOME &&
        modem->data_state != A_REGISTRATION_ROAMING) {
        int nn;
        for (nn = 0; nn < MAX_DATA_CONTEXTS; nn++) {
            ADataContext  data = modem->data_contexts + nn;
            amodem_teardown_pdp( data );
        }
        // Trigger an unsol data call list.
        amodem_unsol(modem, "+CGEV: ME DETACH\r");
    }

    switch (modem->data_mode) {
        case A_REGISTRATION_UNSOL_ENABLED:
            amodem_unsol( modem, "+CGREG: %d,%d\r",
                          modem->data_mode, modem->data_state );
            break;

        case A_REGISTRATION_UNSOL_ENABLED_FULL:
            if (modem->supportsNetworkDataType)
                amodem_unsol( modem, "+CGREG: %d,%d,\"%04x\",\"%07x\",\"%08x\"\r",
                            modem->data_mode, modem->data_state,
                            modem->area_code & 0xffff, modem->cell_id & 0xfffffff,
                            modem->data_network );
            else
                amodem_unsol( modem, "+CGREG: %d,%d,\"%04x\",\"%07x\"\r",
                            modem->data_mode, modem->data_state,
                            modem->area_code & 0xffff, modem->cell_id & 0xfffffff );
            break;

        default:
            ;
    }
}

static int
amodem_nvram_set( AModem modem, const char *name, const char *value )
{
    aconfig_set(modem->nvram_config, name, value);
    aconfig_save_file(modem->nvram_config, modem->nvram_config_filename);
    return 0;
}

static AModemTech
tech_from_network_type( ADataNetworkType type )
{
    switch (type) {
        case A_DATA_NETWORK_GPRS:
        case A_DATA_NETWORK_EDGE:
        case A_DATA_NETWORK_UMTS:
            // TODO: Add A_TECH_WCDMA
            return A_TECH_GSM;
        case A_DATA_NETWORK_LTE:
            return A_TECH_LTE;
        case A_DATA_NETWORK_CDMA1X:
        case A_DATA_NETWORK_EVDO:
            return A_TECH_CDMA;
        case A_DATA_NETWORK_UNKNOWN:
            return A_TECH_UNKNOWN;
    }
    return A_TECH_UNKNOWN;
}

void
amodem_set_data_network_type( AModem  modem, ADataNetworkType   type )
{
    AModemTech modemTech;
    modem->data_network = type;
    amodem_set_data_registration( modem, modem->data_state );
    modemTech = tech_from_network_type(type);
    if (modemTech != A_TECH_UNKNOWN) {
        amodem_set_technology( modem, modemTech, 0 );
    }
}

int
amodem_get_operator_name_ex ( AModem  modem, AOperatorIndex  oper_index, ANameIndex  name_index, char*  buffer, int  buffer_size )
{
    AOperator  oper;
    int        len;

    if ( (unsigned)oper_index >= A_OPERATOR_MAX ||
         (unsigned)name_index >= A_NAME_MAX )
        return 0;

    oper = modem->operators + oper_index;
    len  = strlen(oper->name[name_index]) + 1;

    if (buffer_size > len)
        buffer_size = len;

    if (buffer_size > 0) {
        memcpy( buffer, oper->name[name_index], buffer_size-1 );
        buffer[buffer_size] = 0;
    }
    return len;
}

int
amodem_get_operator_name ( AModem  modem, ANameIndex  index, char*  buffer, int  buffer_size )
{
    if ( (unsigned)modem->oper_index >= (unsigned)modem->oper_count )
        return 0;

    return amodem_get_operator_name_ex(modem, modem->oper_index, index, buffer, buffer_size);
}

void
amodem_set_operator_name_ex( AModem  modem, AOperatorIndex  oper_index, ANameIndex  name_index, const char*  buffer, int  buffer_size )
{
    AOperator  oper;
    int        avail;

    if ( (unsigned)oper_index >= A_OPERATOR_MAX ||
         (unsigned)name_index >= A_NAME_MAX )
        return;

    oper = modem->operators + oper_index;

    avail = sizeof(oper->name[0]);
    if (buffer_size < 0)
        buffer_size = strlen(buffer);
    if (buffer_size > avail-1)
        buffer_size = avail-1;
    memcpy( oper->name[name_index], buffer, buffer_size );
    oper->name[name_index][buffer_size] = 0;
}

void
amodem_set_operator_name( AModem  modem, ANameIndex  index, const char*  buffer, int  buffer_size )
{
    if ( (unsigned)modem->oper_index >= (unsigned)modem->oper_count )
        return;

    amodem_set_operator_name_ex(modem, modem->oper_index, index, buffer, buffer_size);
}

/** CALLS
 **/
int
amodem_get_call_count( AModem  modem )
{
    return modem->call_count;
}

ACall
amodem_get_call( AModem  modem, int  index )
{
    if ((unsigned)index >= (unsigned)modem->call_count)
        return NULL;

    return &modem->calls[index].call;
}

static AVoiceCall
amodem_alloc_call( AModem   modem )
{
    AVoiceCall  call  = NULL;
    int         count = modem->call_count;

    if (count < MAX_CALLS) {
        int  id;

        /* find a valid id for this call */
        for (id = 0; id < modem->call_count; id++) {
            int  found = 0;
            int  nn;
            for (nn = 0; nn < count; nn++) {
                if ( modem->calls[nn].call.id == (id+1) ) {
                    found = 1;
                    break;
                }
            }
            if (!found)
                break;
        }
        call          = modem->calls + count;
        call->call.id = id + 1;
        call->modem   = modem;

        modem->call_count += 1;
    }
    return call;
}


static void
acall_set_multi( AVoiceCall  vcall )
{
    ACall call = &vcall->call;
    if (call->multi)
        return;

    call->multi = 1;
    vcall->modem->multi_count++;
}


static void
acall_unset_multi( AVoiceCall  vcall )
{
    ACall call = &vcall->call;
    AModem modem = vcall->modem;
    int nn;

    if (!call->multi)
        return;

    call->multi = 0;
    modem->multi_count--;

    // Remove the dangling multiparty call.
    if (modem->multi_count == 1) {
        for (nn = 0; nn < modem->call_count; nn++) {
            AVoiceCall  vcall = modem->calls + nn;
            ACall       call  = &vcall->call;
            if (call->mode != A_CALL_VOICE)
                continue;
            if (call->multi) {
                call->multi = 0;
                modem->multi_count--;
                break;
            }
        }
    }
}


static void
amodem_free_call( AModem  modem, AVoiceCall  call, int  cause )
{
    int  nn;

    if (call->timer) {
        sys_timer_destroy( call->timer );
        call->timer = NULL;
    }

    if (call->is_remote) {
        remote_call_cancel( call->call.number, modem );
        call->is_remote = 0;
    }

    acall_unset_multi( call );

    for (nn = 0; nn < modem->call_count; nn++) {
        if ( modem->calls + nn == call )
            break;
    }
    assert( nn < modem->call_count );

    memmove( modem->calls + nn,
             modem->calls + nn + 1,
             (modem->call_count - 1 - nn)*sizeof(*call) );

    modem->call_count -= 1;
    modem->last_call_fail_cause = cause;
}

static AVoiceCall
amodem_find_call( AModem  modem, int  id )
{
    int  nn;

    for (nn = 0; nn < modem->call_count; nn++) {
        AVoiceCall call = modem->calls + nn;
        if (call->call.id == id)
            return call;
    }
    return NULL;
}

void
amodem_send_stk_unsol_proactive_command( AModem  modem, const char* stkCmdPdu )
{
   amodem_unsol( modem, "+CUSATP: %s\r",
                          stkCmdPdu); //string type in hexadecimal character format
}

static void
amodem_send_calls_update( AModem  modem )
{
    amodem_unsol( modem, "CALL STATE CHANGED\r" );
}


int
amodem_add_inbound_call( AModem  modem, const char*  number, const int  numPresentation, const char*  name, const int  namePresentation )
{
    AVoiceCall  vcall = amodem_alloc_call( modem );
    ACall       call  = &vcall->call;
    int         len;
    char        cnapName[ A_CALL_NAME_MAX_SIZE+1 ];
    int         voice_call_count;
    int         nn;

    if (call == NULL)
        return -1;

    call->dir   = A_CALL_INBOUND;
    call->mode  = A_CALL_VOICE;
    call->multi = 0;

    voice_call_count = 0;
    for (nn = 0; nn < modem->call_count; nn++) {
      AVoiceCall  vcall = modem->calls + nn;
      ACall       call  = &vcall->call;
      if (call->mode == A_CALL_VOICE) {
        voice_call_count++;
      }
    }

    call->state = (voice_call_count == 1) ? A_CALL_INCOMING : A_CALL_WAITING;

    vcall->is_remote = (remote_number_string_to_port(number, modem, NULL, NULL) > 0);

    vcall->timer = NULL;

    len  = strlen(number);
    if (len >= sizeof(call->number))
        len = sizeof(call->number)-1;

    memcpy( call->number, number, len );
    call->number[len] = 0;

    call->numberPresentation = numPresentation;

    len = 0;
    if (namePresentation == 0) {
      len = strlen(name);
      if (len >= sizeof(cnapName))
          len = sizeof(cnapName)-1;
      memcpy( cnapName, name, len );
    }
    cnapName[len] = 0;

    amodem_unsol( modem, "RING\r");
    // Send unsolicited +CNAP with valid information.
    if (strlen(cnapName) > 0
        || (namePresentation > 0 && namePresentation <= 2)) {
        amodem_unsol( modem, "+CNAP: \"%s\",%d\r", cnapName, namePresentation);
    }
    return 0;
}

ACall
amodem_find_call_by_number( AModem  modem, const char*  number )
{
    AVoiceCall  vcall = modem->calls;
    AVoiceCall  vend  = vcall + modem->call_count;

    if (!number)
        return NULL;

    for ( ; vcall < vend; vcall++ )
        if ( !strcmp(vcall->call.number, number) )
            return &vcall->call;

    return  NULL;
}

void
amodem_get_signal_strength( AModem modem, int* rssi, int* ber )
{
    *rssi = modem->rssi;
    *ber = modem->ber;
}

void
amodem_set_signal_strength( AModem modem, int rssi, int ber )
{
    modem->rssi = rssi;
    modem->ber = ber;

    /* Reset LTE signal strength */
    modem->rxlev = 99;
    modem->rsrp  = 65535;
    modem->rssnr = 65535;

    amodem_unsol_buffered( modem, handleSignalStrength(NULL, modem) );
}

void
amodem_get_lte_signal_strength( AModem modem, int* rxlev, int* rsrp, int* rssnr )
{
    *rxlev = modem->rxlev;
    *rsrp = modem->rsrp;
    *rssnr = modem->rssnr;
}

void
amodem_set_lte_signal_strength( AModem modem, int rxlev, int rsrp, int rssnr )
{
    /* Reset GSM/UMTS signal strength */
    modem->rssi = 99;
    modem->ber = 99;

    modem->rxlev = rxlev;
    modem->rsrp = rsrp;
    modem->rssnr = rssnr;

    amodem_unsol_buffered( modem, handleSignalStrength(NULL, modem) );
}

static void
acall_set_state( AVoiceCall    call, ACallState  state )
{
    if (state != call->call.state)
    {
        if (call->is_remote)
        {
            const char*  number = call->call.number;

            switch (state) {
                case A_CALL_HELD:
                    remote_call_other( number, call->modem, REMOTE_CALL_HOLD );
                    break;

                case A_CALL_ACTIVE:
                    remote_call_other( number, call->modem, REMOTE_CALL_ACCEPT );
                    break;

                default: ;
            }
        }
        call->call.state = state;
    }
}


int
amodem_update_call( AModem  modem, const char*  fromNumber, ACallState  state )
{
    AVoiceCall  vcall = (AVoiceCall) amodem_find_call_by_number(modem, fromNumber);

    if (vcall == NULL)
        return -1;

    acall_set_state( vcall, state );
    amodem_send_calls_update(modem);
    return 0;
}


int amodem_remote_call_busy( AModem  modem, const char*  number )
{
    AVoiceCall vcall = (AVoiceCall) amodem_find_call_by_number(modem, number);

    if (!vcall)
        return -1;

    amodem_free_call(modem, vcall, CALL_FAIL_BUSY);
    amodem_unsol( modem, "NO CARRIER\r");
    return 0;
}


int
amodem_disconnect_call( AModem  modem, const char*  number )
{
    AVoiceCall  vcall = (AVoiceCall) amodem_find_call_by_number(modem, number);

    if (!vcall)
        return -1;

    amodem_free_call( modem, vcall, CALL_FAIL_NORMAL );
    amodem_unsol( modem, "NO CARRIER\r");
    return 0;
}

int
amodem_clear_call( AModem modem )
{
    if (!modem->call_count)
        return 0;

    int nn;
    for (nn = modem->call_count - 1; nn >= 0; --nn) {
        amodem_free_call( modem, modem->calls + nn, CALL_FAIL_NORMAL );
    }
    amodem_unsol( modem, "NO CARRIER\r");

    return 0;
}

/** Cell Location
 **/

void
amodem_get_gsm_location( AModem modem, int* lac, int* ci )
{
    *lac = modem->area_code;
    *ci = modem->cell_id;
}

void
amodem_set_gsm_location( AModem modem, int lac, int ci )
{
    if ((modem->area_code == lac) && (modem->cell_id == ci)) {
        return;
    }

    modem->area_code = lac;
    modem->cell_id = ci;

    // Notify device through amodem_unsol(...)
    amodem_set_voice_registration( modem, modem->voice_state );
}

/** Data
 **/

static ADataNet
amodem_acquire_data_conn( ADataContext context )
{
    int i;

    for ( i = 0; i < _amodem_num_rmnets; ++i ) {
        ADataNet net = &_amodem_rmnets[i];
        if ( net->context ) {
            continue;
        }

        context->net = net;
        net->context = context;
        return net;
    }

    return NULL;
}

static void
amodem_release_data_conn( ADataNet net )
{
    net->context->net = NULL;
    net->context = NULL;
}

static const char*
amodem_setup_pdp( ADataContext context )
{
    if ( context->active ) {
        return "OK";
    }

    ADataNet net = amodem_acquire_data_conn( context );
    if ( !net || !do_set_link( NULL, net->nd->name, "up" ) ) {
        goto err;
    }

    context->active = true;
    return "OK";

err:
    if ( net ) {
        amodem_release_data_conn(net);
    }

    // service option temporarily out of order
    return "+CME ERROR: 134";
}

static const char*
amodem_teardown_pdp( ADataContext context )
{
    if ( !context->active ) {
        return "OK";
    }

    do_set_link( NULL, context->net->nd->name, "down" );
    amodem_release_data_conn( context->net );

    context->active = false;
    return "OK";
}

static const char*
amodem_activate_data_call( AModem  modem, int cid, int enable)
{
    ADataContext     data;
    int              id;

    assert( enable ==  0 || enable == 1 );

    id = cid - 1;
    if (id < 0 || id >= MAX_DATA_CONTEXTS) {
        // unknown PDP context
        return "+CME ERROR: 143";
    }

    data = modem->data_contexts + id;
    if (data->id <= 0) {
        // activation rejected, unspecified
        return "+CME ERROR: 131";
    }

    if (modem->data_state != A_REGISTRATION_HOME &&
        modem->data_state != A_REGISTRATION_ROAMING) {
        // service option temporarily out of order
        return "+CME ERROR: 134";
    }

    return enable ? amodem_setup_pdp( data )
                  : amodem_teardown_pdp( data );
}

/** COMMAND HANDLERS
 **/

static void
amodem_reply(AModem  modem, const char*  answer)
{
    if ( !memcmp( answer, "> ", 2 ) ||
         !memcmp( answer, "OK", 2 ) ||
         !memcmp( answer, "ERROR", 5 ) ||
         !memcmp( answer, "+CME ERROR", 6 ) ) {
        // Don't append "OK".
    } else if (answer != modem->out_buff) {
        amodem_printf( modem, "%s\rOK", answer );
        answer = modem->out_buff;
    } else
        strcat( modem->out_buff, "\rOK" );

    R(">> %s\n", quote(answer));
    if (modem->unsol_func) {
        modem->unsol_func( modem->unsol_opaque, answer );
        modem->unsol_func( modem->unsol_opaque, "\r" );
    }
}

static const char*
unknownCommand( const char*  cmd, AModem  modem )
{
    modem=modem;
    fprintf(stderr, ">>> unknown command '%s'\n", cmd );
    return "ERROR: unknown command\r";
}

/*
 * Tell whether the specified tech is valid for the preferred mask.
 * @pmask: The preferred mask
 * @tech: The AModemTech we try to validate
 * return: If the specified technology is not set in any of the 4
 *         bitmasks, return 0.
 *         Otherwise, return a non-zero value.
 */
static int matchPreferredMask( int32_t pmask, AModemTech tech )
{
    int ret = 0;
    int i;
    for ( i=3; i >= 0 ; i-- ) {
        if (pmask & (1 << (tech + i*8 ))) {
            ret = 1;
            break;
        }
    }
    return ret;
}

static AModemTech
chooseTechFromMask( AModem modem, int32_t preferred )
{
    int i, j;

    /* TODO: Current implementation will only return the highest priority,
     * lowest numbered technology that is set in the mask.
     * However the implementation could be changed to consider currently
     * available networks set from the console (or somewhere else)
     */
    for ( i=3 ; i >= 0; i-- ) {
        for ( j=0 ; j < A_TECH_UNKNOWN ; j++ ) {
            if (preferred & (1 << (j + 8 * i)))
                return (AModemTech) j;
        }
    }
    assert("This should never happen" == 0);
    // This should never happen. Just to please the compiler.
    return A_TECH_UNKNOWN;
}

static int
_amodem_switch_technology( AModem modem, AModemTech newtech, int32_t newpreferred )
{
    D("_amodem_switch_technology: oldtech: %d, newtech %d, preferred: %d. newpreferred: %d\n",
                      modem->technology, newtech, modem->preferred_mask,newpreferred);
    assert( modem );

    if (!newpreferred) {
        D("ERROR: At least one technology must be enabled");
        return -1;
    }
    if (modem->preferred_mask != newpreferred) {
        char value[MAX_KEY_NAME + 1];
        modem->preferred_mask = newpreferred;
        snprintf(value, MAX_KEY_NAME, "%d", newpreferred);
        amodem_nvram_set(modem, NV_PREFERRED_MODE, value);
        if (!matchPreferredMask(modem->preferred_mask, newtech)) {
            newtech = chooseTechFromMask(modem, newpreferred);
        }
    }

    if (modem->technology != newtech) {
        if (!matchPreferredMask(modem->preferred_mask, newtech)) {
            D("ERROR: Select an unsupported technology\n");
            return -1;
        }
        modem->technology = newtech;
        amodem_nvram_set(modem, NV_MODEM_TECHNOLOGY,
                         android_get_modem_tech_name(modem->technology));
    }

    return modem->technology;
}

AModemTech
amodem_get_technology( AModem modem )
{
    return modem->technology;
}

AModemPreferredMask
amodem_get_preferred_mask( AModem modem )
{
    return android_get_modem_preferred_mask(modem->preferred_mask);
}

int
amodem_set_technology( AModem modem, AModemTech technology, AModemPreferredMask preferredMask )
{
    int current = modem->technology;
    int ret;

    if (preferredMask >= A_PREFERRED_MASK_UNKNOWN) {
        ret = _amodem_switch_technology(modem, technology, modem->preferred_mask);
    } else {
        int32_t maskValue = preferred_masks[preferredMask].value;
        ret = _amodem_switch_technology(modem, technology, maskValue);
    }

    if (ret < 0) {
        return -1;
    }

    if (ret != current) {
        amodem_unsol(modem, "+CTEC: %d\r", ret);
    }

    return 0;
}

static int
parsePreferred( const char *str, int *preferred )
{
    char *endptr = NULL;
    int result = 0;
    if (!str || !*str) { *preferred = 0; return 0; }
    if (*str == '"') str ++;
    if (!*str) return 0;

    result = strtol(str, &endptr, 16);
    if (*endptr && *endptr != '"') {
        return 0;
    }
    if (preferred)
        *preferred = result;
    return 1;
}

void
amodem_set_cdma_prl_version( AModem modem, int prlVersion)
{
    D("amodem_set_prl_version()\n");
    if (!_amodem_set_cdma_prl_version( modem, prlVersion)) {
        amodem_unsol(modem, "+WPRL: %d", prlVersion);
    }
}

static int
_amodem_set_cdma_prl_version( AModem modem, int prlVersion)
{
    D("_amodem_set_cdma_prl_version");
    if (modem->prl_version != prlVersion) {
        modem->prl_version = prlVersion;
        return 0;
    }
    return -1;
}

void
amodem_set_cdma_subscription_source( AModem modem, ACdmaSubscriptionSource ss)
{
    D("amodem_set_cdma_subscription_source()\n");
    if (!_amodem_set_cdma_subscription_source( modem, ss)) {
        amodem_unsol(modem, "+CCSS: %d", (int)ss);
    }
}

#define MAX_INT_DIGITS 10
static int
_amodem_set_cdma_subscription_source( AModem modem, ACdmaSubscriptionSource ss)
{
    D("_amodem_set_cdma_subscription_source()\n");
    char value[MAX_INT_DIGITS + 1];

    if (ss != modem->subscription_source) {
        snprintf( value, MAX_INT_DIGITS + 1, "%d", ss );
        amodem_nvram_set( modem, NV_CDMA_SUBSCRIPTION_SOURCE, value );
        modem->subscription_source = ss;
        return 0;
    }
    return -1;
}

static const char*
handleSubscriptionSource( const char*  cmd, AModem  modem )
{
    int newsource;
    // TODO: Actually change subscription depending on source
    D("handleSubscriptionSource(%s)\n",cmd);

    assert( !memcmp( "+CCSS", cmd, 5 ) );
    cmd += 5;
    if (cmd[0] == '?') {
        return amodem_printf( modem, "+CCSS: %d", modem->subscription_source );
    } else if (cmd[0] == '=') {
        switch (cmd[1]) {
            case '0':
            case '1':
                newsource = (ACdmaSubscriptionSource)cmd[1] - '0';
                _amodem_set_cdma_subscription_source( modem, newsource );
                return amodem_printf( modem, "+CCSS: %d", modem->subscription_source );
                break;
        }
    }
    return amodem_printf( modem, "ERROR: Invalid subscription source");
}

static const char*
handleRoamPref( const char * cmd, AModem modem )
{
    int roaming_pref = -1;
    char *endptr = NULL;
    D("handleRoamPref(%s)\n", cmd);
    assert( !memcmp( "+WRMP", cmd, 5 ) );
    cmd += 5;
    if (cmd[0] == '?') {
        return amodem_printf( modem, "+WRMP: %d", modem->roaming_pref );
    }

    if (!strcmp( cmd, "=?")) {
        return amodem_printf( modem, "+WRMP: 0,1,2" );
    } else if (cmd[0] == '=') {
        cmd ++;
        roaming_pref = strtol( cmd, &endptr, 10 );
         // Make sure the rest of the command is the number
         // (if *endptr is null, it means strtol processed the whole string as a number)
        if(endptr && !*endptr) {
            modem->roaming_pref = roaming_pref;
            aconfig_set( modem->nvram_config, NV_CDMA_ROAMING_PREF, cmd );
            aconfig_save_file( modem->nvram_config, modem->nvram_config_filename );
            return "OK";
        }
    }
    return amodem_printf( modem, "ERROR");
}
static const char*
handleTech( const char*  cmd, AModem  modem )
{
    AModemTech newtech = modem->technology;
    int pt = modem->preferred_mask;
    int havenewtech = 0;
    D("handleTech. cmd: %s\n", cmd);
    assert( !memcmp( "+CTEC", cmd, 5 ) );
    cmd += 5;
    if (cmd[0] == '?') {
        return amodem_printf( modem, "+CTEC: %d,%x",modem->technology, modem->preferred_mask );
    }
    amodem_begin_line( modem );
    if (!strcmp( cmd, "=?")) {
        return amodem_printf( modem, "+CTEC: 0,1,2,3" );
    }
    else if (cmd[0] == '=') {
        switch (cmd[1]) {
            case '0':
            case '1':
            case '2':
            case '3':
                havenewtech = 1;
                newtech = cmd[1] - '0';
                cmd += 1;
                break;
        }
        cmd += 1;
    }
    if (havenewtech) {
        int current = modem->technology;
        int ret;

        D( "cmd: %s\n", cmd );
        if (cmd[0] == ',' && ! parsePreferred( ++cmd, &pt ))
            return amodem_printf( modem, "ERROR: invalid preferred mode" );

        ret = _amodem_switch_technology( modem, newtech, pt );

        if (ret < 0) {
            return amodem_printf( modem, "ERROR: unable to set preferred mode" );
        }

        if (ret != current) {
            return amodem_printf( modem, "+CTEC: %d", ret );
        }

        return amodem_printf( modem, "+CTEC: DONE" );
    }
    return amodem_printf( modem, "ERROR: %s: Unknown Technology", cmd + 1 );
}

static const char*
handleEmergencyMode( const char* cmd, AModem modem )
{
    long arg;
    char *endptr = NULL;
    assert ( !memcmp( "+WSOS", cmd, 5 ) );
    cmd += 5;
    if (cmd[0] == '?') {
        return amodem_printf( modem, "+WSOS: %d", modem->in_emergency_mode);
    }

    if (cmd[0] == '=') {
        if (cmd[1] == '?') {
            return amodem_printf(modem, "+WSOS: (0)");
        }
        if (cmd[1] == 0) {
            return amodem_printf(modem, "ERROR");
        }
        arg = strtol(cmd+1, &endptr, 10);

        if (!endptr || endptr[0] != 0) {
            return amodem_printf(modem, "ERROR");
        }

        arg = arg? 1 : 0;

        if ((!arg) != (!modem->in_emergency_mode)) {
            modem->in_emergency_mode = arg;
            return amodem_printf(modem, "+WSOS: %d", arg);
        }
    }
    return amodem_printf(modem, "ERROR");
}

static const char*
handlePrlVersion( const char* cmd, AModem modem )
{
    assert ( !memcmp( "+WPRL", cmd, 5 ) );
    cmd += 5;
    if (cmd[0] == '?') {
        return amodem_printf( modem, "+WPRL: %d", modem->prl_version);
    }

    return amodem_printf(modem, "ERROR");
}

static const char*
handleRadioPower( const char*  cmd, AModem  modem )
{
    ARadioState radio_state;

    if ( !strcmp( cmd, "+CFUN=0" ) )
        radio_state = A_RADIO_STATE_OFF;
    else if ( !strcmp( cmd, "+CFUN=1" ) )
        radio_state = A_RADIO_STATE_ON;
    else {
        // 3GPP TS 27.007 subclause 9.2.1 "General errors":
        // 50 Incorrect parameters
        return "+CME ERROR: 50";
    }

    if (radio_state == modem->radio_state) {
        return "OK";
    }

    modem->radio_state = radio_state;
    amodem_reply(modem, "OK");

    switch (radio_state) {
        case A_RADIO_STATE_OFF:
            amodem_set_voice_registration(modem, A_REGISTRATION_UNREGISTERED);
            amodem_set_data_registration(modem, A_REGISTRATION_UNREGISTERED);
            asimcard_reset_status_after_radio_off(modem->sim);
            break;
        case A_RADIO_STATE_ON:
            amodem_set_voice_registration(modem, A_REGISTRATION_HOME);
            amodem_set_data_registration(modem, A_REGISTRATION_HOME);
            break;
    }

    // Return NULL to show we have sent the reply and no further work to do.
    return NULL;
}

static const char*
handleRadioPowerReq( const char*  cmd, AModem  modem )
{
    if (modem->radio_state != A_RADIO_STATE_OFF)
        return "+CFUN: 1";
    else
        return "+CFUN: 0";
}

static const char*
handleSIMStatusReq( const char*  cmd, AModem  modem )
{
    const char*  answer = NULL;

    switch (asimcard_get_status(modem->sim)) {
        case A_SIM_STATUS_ABSENT:    answer = "+CPIN: ABSENT"; break;
        case A_SIM_STATUS_READY:     answer = "+CPIN: READY"; break;
        case A_SIM_STATUS_NOT_READY: answer = "+CMERROR: NOT READY"; break;
        case A_SIM_STATUS_PIN:       answer = "+CPIN: SIM PIN"; break;
        case A_SIM_STATUS_PUK:       answer = "+CPIN: SIM PUK"; break;
        case A_SIM_STATUS_NETWORK_PERSONALIZATION: answer = "+CPIN: PH-NET PIN"; break;
        default:
            answer = "ERROR: internal error";
    }
    return answer;
}

/* TODO: Will we need this?
static const char*
handleSRegister( const char * cmd, AModem modem )
{
    char *end;
    assert( cmd[0] == 'S' || cmd[0] == 's' );

    ++ cmd;

    int l = strtol(cmd, &end, 10);
} */

static const char*
handleNetworkRegistration( const char*  cmd, AModem  modem )
{
    if ( !memcmp( cmd, "+CREG", 5 ) ) {
        cmd += 5;
        if (cmd[0] == '?') {
            if (modem->voice_mode == A_REGISTRATION_UNSOL_ENABLED_FULL)
                return amodem_printf( modem, "+CREG: %d,%d, \"%04x\", \"%07x\"",
                                       modem->voice_mode, modem->voice_state,
                                       modem->area_code & 0xffff, modem->cell_id & 0xfffffff );
            else
                return amodem_printf( modem, "+CREG: %d,%d",
                                       modem->voice_mode, modem->voice_state );
        } else if (cmd[0] == '=') {
            switch (cmd[1]) {
                case '0':
                    modem->voice_mode  = A_REGISTRATION_UNSOL_DISABLED;
                    break;

                case '1':
                    modem->voice_mode  = A_REGISTRATION_UNSOL_ENABLED;
                    break;

                case '2':
                    modem->voice_mode = A_REGISTRATION_UNSOL_ENABLED_FULL;
                    break;

                case '?':
                    return "+CREG: (0-2)";

                default:
                    return "ERROR: BAD COMMAND";
            }
        } else {
            assert( 0 && "unreachable" );
        }
    } else if ( !memcmp( cmd, "+CGREG", 6 ) ) {
        cmd += 6;
        if (cmd[0] == '?') {
            if (modem->supportsNetworkDataType)
                return amodem_printf( modem, "+CGREG: %d,%d,\"%04x\",\"%07x\",\"%04x\"",
                                    modem->data_mode, modem->data_state,
                                    modem->area_code & 0xffff, modem->cell_id & 0xfffffff,
                                    modem->data_network );
            else
                return amodem_printf( modem, "+CGREG: %d,%d,\"%04x\",\"%07x\"",
                                    modem->data_mode, modem->data_state,
                                    modem->area_code & 0xffff, modem->cell_id & 0xfffffff );
        } else if (cmd[0] == '=') {
            switch (cmd[1]) {
                case '0':
                    modem->data_mode  = A_REGISTRATION_UNSOL_DISABLED;
                    break;

                case '1':
                    modem->data_mode  = A_REGISTRATION_UNSOL_ENABLED;
                    break;

                case '2':
                    modem->data_mode = A_REGISTRATION_UNSOL_ENABLED_FULL;
                    break;

                case '?':
                    return "+CGREG: (0-2)";

                default:
                    return "ERROR: BAD COMMAND";
            }
        } else {
            assert( 0 && "unreachable" );
        }
    }
    return "OK";
}

static const char*
handleSetDialTone( const char*  cmd, AModem  modem )
{
    /* XXX: TODO */
    return "OK";
}

static const char*
handleDeleteSMSonSIM( const char*  cmd, AModem  modem )
{
    /* XXX: TODO */
    return "OK";
}

static const char*
handleSIM_IO( const char*  cmd, AModem  modem )
{
    return asimcard_io( modem->sim, cmd );
}


static const char*
handleOperatorSelection( const char*  cmd, AModem  modem )
{
    assert( !memcmp( "+COPS", cmd, 5 ) );
    cmd += 5;
    if (cmd[0] == '?') { /* ask for current operator */
        AOperator  oper = &modem->operators[ modem->oper_index ];

        if ( !amodem_has_network( modem ) )
        {
            /* this error code means "no network" */
            return amodem_printf( modem, "+CME ERROR: 30" );
        }

        oper = &modem->operators[ modem->oper_index ];

        if ( modem->oper_name_index == 2 )
            return amodem_printf( modem, "+COPS: %d,2,%s",
                                  modem->oper_selection_mode,
                                  oper->name[2] );

        return amodem_printf( modem, "+COPS: %d,%d,\"%s\"",
                              modem->oper_selection_mode,
                              modem->oper_name_index,
                              oper->name[ modem->oper_name_index ] );
    }
    else if (cmd[0] == '=' && cmd[1] == '?') {  /* ask for all available operators */
        const char*  comma = "+COPS: ";
        int          nn;
        amodem_begin_line( modem );
        for (nn = 0; nn < modem->oper_count; nn++) {
            AOperator  oper = &modem->operators[nn];
            amodem_add_line( modem, "%s(%d,\"%s\",\"%s\",\"%s\")", comma,
                             oper->status, oper->name[0], oper->name[1], oper->name[2] );
            comma = ", ";
        }
        return amodem_end_line( modem );
    }
    else if (cmd[0] == '=') {
        switch (cmd[1]) {
            case '0':
                modem->oper_selection_mode = A_SELECTION_AUTOMATIC;
                amodem_set_voice_registration(modem, A_REGISTRATION_HOME);
                return "OK";

            case '1':
                {
                    int  format, nn, len, found = -1;

                    if (cmd[2] != ',')
                        goto BadCommand;
                    format = cmd[3] - '0';
                    if ( (unsigned)format > 2 )
                        goto BadCommand;
                    if (cmd[4] != ',')
                        goto BadCommand;
                    cmd += 5;
                    len  = strlen(cmd);
                    if (*cmd == '"') {
                        cmd++;
                        len -= 2;
                    }
                    if (len <= 0)
                        goto BadCommand;

                    for (nn = 0; nn < modem->oper_count; nn++) {
                        AOperator    oper = modem->operators + nn;
                        char*        name = oper->name[ format ];

                        if ( !memcmp( name, cmd, len ) && name[len] == 0 ) {
                            found = nn;
                            break;
                        }
                    }

                    if (found < 0) {
                        /* Selection failed */
                        return "+CME ERROR: 529";
                    } else if (modem->operators[found].status == A_STATUS_DENIED) {
                        /* network not allowed */
                        return "+CME ERROR: 32";
                    }
                    modem->oper_selection_mode = A_SELECTION_MANUAL;
                    modem->oper_index = found;

                    /* set the voice and data registration states to home or roaming
                     * depending on the operator index
                     */
                    if (found == OPERATOR_HOME_INDEX) {
                        modem->data_state = A_REGISTRATION_HOME;
                        amodem_set_voice_registration(modem, A_REGISTRATION_HOME);
                    } else if (found == OPERATOR_ROAMING_INDEX) {
                        modem->data_state = A_REGISTRATION_ROAMING;
                        amodem_set_voice_registration(modem, A_REGISTRATION_ROAMING);
                    }
                    return "OK";
                }

            case '2':
                modem->oper_selection_mode = A_SELECTION_DEREGISTRATION;
                return "OK";

            case '3':
                {
                    int format;

                    if (cmd[2] != ',')
                        goto BadCommand;

                    format = cmd[3] - '0';
                    if ( (unsigned)format > 2 )
                        goto BadCommand;

                    modem->oper_name_index = format;
                    return "OK";
                }
            default:
                ;
        }
    }
BadCommand:
    return unknownCommand(cmd,modem);
}

static const char*
handleRequestOperator( const char*  cmd, AModem  modem )
{
    AOperator  oper;
    cmd=cmd;

    if ( !amodem_has_network(modem) )
        return "+CME ERROR: 30";

    oper = modem->operators + modem->oper_index;
    modem->oper_name_index = 2;
    return amodem_printf( modem, "+COPS: 0,0,\"%s\"\r"
                          "+COPS: 0,1,\"%s\"\r"
                          "+COPS: 0,2,\"%s\"",
                          oper->name[0], oper->name[1], oper->name[2] );
}

static const char*
handleSendSMStoSIM( const char*  cmd, AModem  modem )
{
    /* XXX: TODO */
    return "ERROR: unimplemented";
}

static const char*
handleSendSMS( const char*  cmd, AModem  modem )
{
    modem->wait_sms = 1;
    return "> ";
}

#if 0
static void
sms_address_dump( SmsAddress  address, FILE*  out )
{
    int  nn, len = address->len;

    if (address->toa == 0x91) {
        fprintf( out, "+" );
    }
    for (nn = 0; nn < len; nn += 2)
    {
        static const char  dialdigits[16] = "0123456789*#,N%";
        int  c = address->data[nn/2];

        fprintf( out, "%c", dialdigits[c & 0xf] );
        if (nn+1 >= len)
            break;

        fprintf( out, "%c", dialdigits[(c >> 4) & 0xf] );
    }
}

static void
smspdu_dump( SmsPDU  pdu, FILE*  out )
{
    SmsAddressRec    address;
    unsigned char    temp[256];
    int              len;

    if (pdu == NULL) {
        fprintf( out, "SMS PDU is (null)\n" );
        return;
    }

    fprintf( out, "SMS PDU type:       " );
    switch (smspdu_get_type(pdu)) {
        case SMS_PDU_DELIVER: fprintf(out, "DELIVER"); break;
        case SMS_PDU_SUBMIT:  fprintf(out, "SUBMIT"); break;
        case SMS_PDU_STATUS_REPORT: fprintf(out, "STATUS_REPORT"); break;
        default: fprintf(out, "UNKNOWN");
    }
    fprintf( out, "\n        sender:   " );
    if (smspdu_get_sender_address(pdu, &address) < 0)
        fprintf( out, "(N/A)" );
    else
        sms_address_dump(&address, out);
    fprintf( out, "\n        receiver: " );
    if (smspdu_get_receiver_address(pdu, &address) < 0)
        fprintf(out, "(N/A)");
    else
        sms_address_dump(&address, out);
    fprintf( out, "\n        text:     " );
    len = smspdu_get_text_message( pdu, temp, sizeof(temp)-1 );
    if (len > sizeof(temp)-1 )
        len = sizeof(temp)-1;
    fprintf( out, "'%.*s'\n", len, temp );
}
#endif

static const char*
handleSendSMSText( const char*  cmd, AModem  modem )
{
    SmsAddressRec  address;
    char           temp[16];
    char           number[16];
    int            numlen;
    int            len = strlen(cmd);
    SmsPDU         pdu;

    /* get rid of trailing escape */
    if (len > 0 && cmd[len-1] == 0x1a)
        len -= 1;

    pdu = smspdu_create_from_hex( cmd, len );
    if (pdu == NULL) {
        D("%s: invalid SMS PDU ?: '%s'\n", __FUNCTION__, cmd);
        return "+CMS ERROR: INVALID SMS PDU";
    }
    if (smspdu_get_receiver_address(pdu, &address) < 0) {
        D("%s: could not get SMS receiver address from '%s'\n",
          __FUNCTION__, cmd);
        return "+CMS ERROR: BAD SMS RECEIVER ADDRESS";
    }

    amodem_reply( modem, "+CMGS: 0" );

    do {
        int  index;

        numlen = sms_address_to_str( &address, temp, sizeof(temp) );
        if (numlen > sizeof(temp)-1)
            break;
        temp[numlen] = 0;

        /* Converts 4, 5, 7, and 10 digits number to 11 digits */
        if ((numlen == 10 && (!strncmp(temp, PHONE_PREFIX+1, 5) && ((temp[5] - '1') == modem->instance_id)))
            || (numlen == 7 && (!strncmp(temp, PHONE_PREFIX+4, 2) && ((temp[2] - '1') == modem->instance_id)))
            || (numlen == 5 && ((temp[0] - '1') == modem->instance_id))) {
            memcpy( number, PHONE_PREFIX, 11 - numlen );
            memcpy( number + 11 - numlen, temp, numlen );
            number[11] = 0;
        } else if (numlen == 4) {
            memcpy( number, PHONE_PREFIX, 6 );
            number[6] = '1' + modem->instance_id;
            memcpy( number+7, temp, numlen );
            number[11] = 0;
        } else {
            memcpy( number, temp, numlen );
            number[numlen] = 0;
        }

        int remote_port = -1, remote_instance_id = -1;
        if (remote_number_string_to_port( number, modem, &remote_port,
                                          &remote_instance_id ) < 0) {
            break;
        }

        if (modem->sms_receiver == NULL) {
            modem->sms_receiver = sms_receiver_create();
            if (modem->sms_receiver == NULL) {
                D( "%s: could not create SMS receiver\n", __FUNCTION__ );
                break;
            }
        }

        index = sms_receiver_add_submit_pdu( modem->sms_receiver, pdu );
        if (index < 0) {
            D( "%s: could not add submit PDU\n", __FUNCTION__ );
            break;
        }
        /* the PDU is now owned by the receiver */
        pdu = NULL;

        if (index > 0) {
            SmsAddressRec  from[1];
            char           temp[12];
            SmsPDU*        deliver;
            int            nn;

            snprintf( temp, sizeof(temp), PHONE_PREFIX "%d%d",
                      modem->instance_id + 1, modem->base_port );
            sms_address_from_str( from, temp, strlen(temp) );

            deliver = sms_receiver_create_deliver( modem->sms_receiver, index, from );
            if (deliver == NULL) {
                D( "%s: could not create deliver PDUs for SMS index %d\n",
                   __FUNCTION__, index );
                break;
            }

            for (nn = 0; deliver[nn] != NULL; nn++) {
                if (remote_port == modem->base_port) {
                    AModem remote_modem = amodem_get_instance(remote_instance_id);
                    if (remote_modem) {
                        amodem_receive_sms( remote_modem, deliver[nn] );
                    }
                } else if ( remote_call_sms( number, modem, deliver[nn] ) < 0 ) {
                    D( "%s: could not send SMS PDU to remote emulator\n",
                       __FUNCTION__ );
                    break;
                }
            }

            smspdu_free_list(deliver);
        }

    } while (0);

    if (pdu != NULL)
        smspdu_free(pdu);

    return NULL;
}

static const char*
handleChangeOrEnterPIN( const char*  cmd, AModem  modem )
{
    assert( !memcmp( cmd, "+CPIN=", 6 ) );
    cmd += 6;

    switch (asimcard_get_status(modem->sim)) {
        case A_SIM_STATUS_ABSENT:
            return "+CME ERROR: SIM ABSENT";

        case A_SIM_STATUS_NOT_READY:
            return "+CME ERROR: SIM NOT READY";

        case A_SIM_STATUS_READY:
            /* this may be a request to change the PIN */
            {
                if (strlen(cmd) == 9 && cmd[4] == ',') {
                    char  pin[5];
                    memcpy( pin, cmd, 4 ); pin[4] = 0;

                    if ( !asimcard_check_pin( modem->sim, pin ) )
                        return "+CME ERROR: BAD PIN";

                    memcpy( pin, cmd+5, 4 );
                    asimcard_set_pin( modem->sim, pin );
                    return "+CPIN: READY";
                }
            }
            break;

        case A_SIM_STATUS_PIN:   /* waiting for PIN */
            if ( asimcard_check_pin( modem->sim, cmd ) )
                return "+CPIN: READY";
            else
                return "+CME ERROR: BAD PIN";

        case A_SIM_STATUS_PUK:  /* waiting for PUK */
            if (strlen(cmd) == 13 && cmd[8] == ',') {
                char  puk[9];
                memcpy( puk, cmd, 8 );
                puk[8] = 0;
                if ( asimcard_check_puk( modem->sim, puk, cmd+9 ) )
                    return "+CPIN: READY";
                else
                    return "+CME ERROR: BAD PUK";
            }
            return "+CME ERROR: BAD PUK";

        default:
            return "+CPIN: PH-NET PIN";
    }

    return "+CME ERROR: BAD FORMAT";
}

static const char*
handleGetRemainingRetries( const char* cmd, AModem modem )
{
    assert(!memcmp(cmd, "+CPINR=", 7));
    cmd += 7;

    amodem_begin_line(modem);

    if (!strcmp(cmd, "SIM PIN")) {
      amodem_add_line(modem, "+CPINR: SIM PIN,%d,%d\r\n",
                      asimcard_get_pin_retries(modem->sim),
                      A_SIM_PIN_RETRIES);
    } else if (!strcmp(cmd, "SIM PUK")) {
      amodem_add_line(modem, "+CPINR: SIM PUK,%d,%d\r\n",
                      asimcard_get_puk_retries(modem->sim),
                      A_SIM_PUK_RETRIES);
    } else {
      // Incorrect parameters
      amodem_add_line( modem, "+CME ERROR: 50\r\n");
    }

    return amodem_end_line(modem);
}

static const char*
handleListCurrentCalls( const char*  cmd, AModem  modem )
{
    int  nn;
    amodem_begin_line( modem );
    for (nn = 0; nn < modem->call_count; nn++) {
        AVoiceCall  vcall = modem->calls + nn;
        ACall       call  = &vcall->call;
        if (call->mode == A_CALL_VOICE) {
            /* see TS 22.067 Table 1 for the definition of priority */
            /* +CLCC: <ccid1>,<dir>,<stat>,<mode>,<mpty>,<number>,<type>,<alpha>,<priority>,<CLI validity> */
            const char* number = (call->numberPresentation == 0) ? call->number : "";
            amodem_add_line( modem, "+CLCC: %d,%d,%d,%d,%d,\"%s\",%d,\"\",2,%d\r\n",
                             call->id, call->dir, call->state, call->mode,
                             call->multi, number, 129 , call->numberPresentation);
        }
    }
    return amodem_end_line( modem );
}

static const char*
handleLastCallFailCause( const char* cmd, AModem modem )
{
    amodem_add_line( modem, "+CEER: %d\n", modem->last_call_fail_cause );
    return amodem_end_line( modem );
}

static const char*
handleCallForwardGetReq( const char* cmd, AModem modem )
{
    int i, j;
    int reason = 0;
    // According to TS 27.007, the default value is 7.
    int classx = 7;
    ACallForward records[CALL_FORWARDING_MAX_CLASSX_OFFSET + 1];
    bool records_processed[CALL_FORWARDING_MAX_CLASSX_OFFSET + 1];

    if (sscanf(cmd, "+CCFC=%d,%*[^,],,,%d", &reason, &classx) < 1) {
        // Incorrect parameters
        return "+CME ERROR: 50";
    }

    if ((classx >> (CALL_FORWARDING_MAX_CLASSX_OFFSET + 1))) {
        // Invalid classx
        return "+CME ERROR: 50";
    }

    // Iterative all service.
    for (i = 0; i <= CALL_FORWARDING_MAX_CLASSX_OFFSET; i++) {
        records[i] = NULL;
        if (classx & (0x1 << i)) {
            records[i] = asupplementary_get_call_foward(modem->supplementary,
                                                        reason, i);
            records_processed[i] = false;
        } else {
            records_processed[i] = true;
        }
    }

    amodem_begin_line(modem);
    for (i = 0; i <= CALL_FORWARDING_MAX_CLASSX_OFFSET; i++) {
        if (records_processed[i]) {
            continue;
        }

        // Merge if the configuration is the same.
        // (For example, if both data and voice are forwarded to +18005551212,
        // then a single CCFC can be returned with the service class set to
        // "data + voice = 3")
        classx = (0x1 << i);
        for (j = i + 1; j <= CALL_FORWARDING_MAX_CLASSX_OFFSET; j++) {
            if (records_processed[j]) {
                continue;
            }

            if (records[i] == records[j] ||
                (records[i] && records[j] &&
                 records[i]->enabled == records[j]->enabled &&
                 records[i]->toa == records[j]->toa &&
                 records[i]->time == records[j]->time &&
                 strcmp(records[i]->number, records[j]->number) == 0)) {
                classx = classx | (0x1 << j);
                records_processed[j] = true;
            }
        }

        if (records[i]) {
            amodem_add_line(modem, "+CCFC: %d,%d,\"%s\",%d,,,%d\r\n"
                                 , (records[i]->enabled) ? 1 : 0
                                 , classx
                                 , records[i]->number
                                 , records[i]->toa
                                 , records[i]->time);
        } else {
            amodem_add_line(modem, "+CCFC: 0,%d\r\n", classx);
        }
    }
    return amodem_end_line(modem);
}

static const char*
handleCallForwardSetReq( const char* cmd, AModem modem )
{
    int i = 0;
    int reason = 0;
    int mode = 0;
    int toa = 0;
    // According to TS 27.007, the default value is 7.
    int classx = 7;
    // According to TS 27.007, the default value is 20.
    int time = 20;
    char number[CALL_FORWARDING_MAX_NUMBERS + 1];

    if (sscanf(cmd, "+CCFC=%d,%d,\"%[^\"]\",%d,%d,,,%d",
               &reason, &mode, number, &toa, &classx, &time) < 4) {
        // Incorrect parameters
        return "+CME ERROR: 50";
    }

    if ((classx >> (CALL_FORWARDING_MAX_CLASSX_OFFSET + 1))) {
        // Invalid classx
        return "+CME ERROR: 50";
    }

    // Iterative all service.
    for (i = 0; i <= CALL_FORWARDING_MAX_CLASSX_OFFSET; i++) {
        if (!(classx & (0x01 << i))) {
            continue;
        }

        switch (mode) {
            case A_CALL_FORWARDING_MODE_ERASURE:
                asupplementary_remove_call_forward(modem->supplementary, reason, i);
                break;
            case A_CALL_FORWARDING_MODE_ENABLE:
            case A_CALL_FORWARDING_MODE_REGISTRATION:
                asupplementary_set_call_forward(modem->supplementary, reason, i,
                                                true, number, toa, time);
                break;
            case A_CALL_FORWARDING_MODE_DISABLE:
                asupplementary_set_call_forward(modem->supplementary, reason, i,
                                                false, number, toa, time);
                break;
            default:
                // Incorrect parameters
                return "+CME ERROR: 50";
        }
    }

    return "OK";
}

static const char*
handleCallForwardReq( const char* cmd, AModem modem )
{
    int reason = 0;
    int mode = 0;

    if (sscanf(cmd, "+CCFC=%d,%d%*s", &reason, &mode) < 2) {
        // Incorrect parameters
        return "+CME ERROR: 50";
    }

    if (reason == A_CALL_FORWARDING_REASON_ALL ||
        reason == A_CALL_FORWARDING_REASON_ALL_CONDITIONAL) {
        // We don't support A_CALL_FORWARDING_REASON_ALL and
        // A_CALL_FORWARDING_REASON_ALL_CONDITIONAL for now, because there is no
        // detailed information about how to handle these two types in TS 22.082.
        return "+CME ERROR: 50";
    }

    if (mode == A_CALL_FORWARDING_MODE_QUERY) {
        return handleCallForwardGetReq(cmd, modem);
    } else {
        return handleCallForwardSetReq(cmd, modem);
    }
}

static const char*
handleFacilityLockReq( const char* cmd, AModem modem )
{
    char fac[64];
    char passwd[64];
    int mode;
    // According to TS 27.007, the default value is 7.
    int class = 7;

    // AT+CLCK=<fac>,<mode>[,<password>[,<class>]].
    int argc = sscanf(cmd, "+CLCK=\"%[^\"]\",%d,\"%[^\"]\",%d", fac, &mode, passwd, &class);
    if (argc < 2) {
        // Incorrect parameters
        return "+CME ERROR: 50";
    }

    // Now we only support "pin" facility lock.
    if (strcmp(fac, "SC")) {
        // Operation not supported
        return "+CME ERROR: 4";
    }

    // Now we only support voice service class.
    if (!(class & 1)) {
        // Operation not supported
        return "+CME ERROR: 4";
    }

    switch (mode) {
        case 0: // Unlock
        case 1: // Lock
            if (argc < 3) {
                // Incorrect parameters
                return "+CME ERROR: 50";
            }

            if (!asimcard_set_pin_enabled(modem->sim, (mode == 1), passwd)) {
                // Incorrect password
                return "+CME ERROR: 16";
            }

            return "OK";
        case 2: //Query status.
            return amodem_printf(modem, "+CLCK: %d,%d\r\n",
                                 asimcard_get_pin_enabled(modem->sim) ? 1 : 0, 1);
    }

    // Incorrect parameters
    return "+CME ERROR: 50";
}

/* Add a(n unsolicited) time response.
 *
 * retrieve the current time and zone in a format suitable
 * for %CTZV: unsolicited message
 *  "yy/mm/dd,hh:mm:ss(+/-)tz"
 *   mm is 0-based
 *   tz is in number of quarter-hours
 *
 * it seems reference-ril doesn't parse the comma (,) as anything else than a token
 * separator, so use a column (:) instead, the Java parsing code won't see a difference
 *
 */
static void
amodem_addTimeUpdate( AModem  modem )
{
    time_t       now = time(NULL);
    struct tm    utc, local;
    long         e_local, e_utc;
    long         tzdiff;
    char         tzname[64];

    tzset();

    utc   = *gmtime( &now );
    local = *localtime( &now );

    e_local = local.tm_min + 60*(local.tm_hour + 24*local.tm_yday);
    e_utc   = utc.tm_min   + 60*(utc.tm_hour   + 24*utc.tm_yday);

    if ( utc.tm_year < local.tm_year )
        e_local += 24*60;
    else if ( utc.tm_year > local.tm_year )
        e_utc += 24*60;

    tzdiff = e_local - e_utc;  /* timezone offset in minutes */

   /* retrieve a zoneinfo-compatible name for the host timezone
    */
    {
        char*  end = tzname + sizeof(tzname);
        char*  p = bufprint_zoneinfo_timezone( tzname, end );
        if (p >= end)
            strcpy(tzname, "Unknown/Unknown");

        /* now replace every / in the timezone name by a "!"
         * that's because the code that reads the CTZV line is
         * dumb and treats a / as a field separator...
         */
        p = tzname;
        while (1) {
            p = strchr(p, '/');
            if (p == NULL)
                break;
            *p = '!';
            p += 1;
        }
    }

   /* as a special extension, we append the name of the host's time zone to the
    * string returned with %CTZ. the system should contain special code to detect
    * and deal with this case (since it normally relied on the operator's country code
    * which is hard to simulate on a general-purpose computer
    */
    amodem_add_line( modem, "%%CTZV: %02d/%02d/%02d:%02d:%02d:%02d%c%d:%d:%s\r\n",
             (utc.tm_year + 1900) % 100, utc.tm_mon + 1, utc.tm_mday,
             utc.tm_hour, utc.tm_min, utc.tm_sec,
             (tzdiff >= 0) ? '+' : '-', (tzdiff >= 0 ? tzdiff : -tzdiff) / 15,
             (local.tm_isdst > 0),
             tzname );
}

static const char*
handleEndOfInit( const char*  cmd, AModem  modem )
{
    amodem_begin_line( modem );
    amodem_addTimeUpdate( modem );
    return amodem_end_line( modem );
}


static const char*
handleListPDPContexts( const char*  cmd, AModem  modem )
{
    int  nn;
    assert( !memcmp( cmd, "+CGACT?", 7 ) );
    amodem_begin_line( modem );
    for (nn = 0; nn < MAX_DATA_CONTEXTS; nn++) {
        ADataContext  data = modem->data_contexts + nn;
        /* The read command returns the current activation states for all the
         * defined PDP contexts. */
        if (data->id <= 0)
            continue;
        amodem_add_line( modem, "+CGACT: %d,%d\r\n", data->id, data->active );
    }
    return amodem_end_line( modem );
}

static const char*
handleDefinePDPContext( const char*  cmd, AModem  modem )
{
    assert( !memcmp( cmd, "+CGDCONT=", 9 ) );
    cmd += 9;
    if (cmd[0] == '?') {
        /* +CGDCONT=? is used to query the ranges of supported PDP Contexts.
         * We only really support IP ones in the emulator, so don't try to
         * fake PPP ones.
         */
        amodem_begin_line( modem );
        amodem_add_line( modem, "+CGDCONT: (1-%d),\"IP\",,,(0-2),(0-4)",
                         MAX_DATA_CONTEXTS );
        return amodem_end_line( modem );
    }

    /* Template is +CGDCONT=[<cid>[,<PDP_type>[,<APN>[,<PDP_addr>[...]]]]] */
    int           cid;
    ADataContext  data;
    ADataType     type;
    char          apn[A_DATA_APN_SIZE];
    char          addr[INET_ADDRSTRLEN];
    const char*   p;
    int           len;

    /* <cid> */

    /* 3GPP TS 27.007 subclause 10.1.1 says that <cid> is optional but doesn't
     * mention how to handle that correctly.
     */
    if ( 1 != sscanf( cmd, "%d", &cid ) )
        goto BadCommand;

    if ( cid <= 0 || cid > MAX_DATA_CONTEXTS )
        goto BadCommand;

    data = modem->data_contexts + cid - 1;
    if (data->active) {
        /* Data connection in use. Operation not allowed. */
        return "+CME ERROR: 3";
    }

    cmd += 1;
    if ( !*cmd ) {
        /* No additional parameters. Undefine the specified PDP context. */
        data->id = -1;
        return "OK";
    }

    /* <PDP_type> */

    if ( !memcmp( cmd, ",\"IP\"", 5 ) ) {
        type = A_DATA_IP;
        cmd += 5;
    } else
        goto BadCommand;

    /* <APN> */

    if ( ',' != cmd[0] || '"' != cmd[1] )
        goto BadCommand;

    cmd += 2;
    p = strchr(cmd, '"');
    if ( p == NULL )
        goto BadCommand;

    len = p - cmd;
    if ( !len || len >= sizeof(apn) )
        goto BadCommand;

    memcpy( apn, cmd, len );
    apn[len] = '\0';

    /* <PDP_addr> */

    cmd = p + 1;
    addr[0] = '\0';
    if ( ',' == cmd[0] && '"' == cmd[1] ) {
        cmd += 2;
        p = strchr(cmd, '"');
        if ( p == NULL )
            goto BadCommand;

        len = p - cmd;
        if ( !len || len >= sizeof(addr) )
            goto BadCommand;

        memcpy( addr, cmd, len );
        addr[len] = '\0';
        cmd = p + 1;
    }

    data->id     = cid;
    data->active = 0;
    data->type   = type;
    strcpy( data->apn, apn );
    if (inet_pton( AF_INET, addr, &data->addr.in.s_addr) <= 0) {
        data->addr.in.s_addr = 0;
    }

    return "OK";
BadCommand:
    return "ERROR: BAD COMMAND";
}

static const char*
handleQueryPDPContext( const char* cmd, AModem modem )
{
    int  nn;
    amodem_begin_line(modem);
    for (nn = 0; nn < MAX_DATA_CONTEXTS; nn++) {
        ADataContext  data = modem->data_contexts + nn;
        char          addr[INET_ADDRSTRLEN];

        if (data->id <= 0)
            continue;

        /* The read command returns current settings for each defined context. */
        if (data->addr.in.s_addr) {
            inet_ntop( AF_INET, &data->addr.in, addr, sizeof addr);
        } else {
            addr[0] = '\0';
        }
        amodem_add_line( modem, "+CGDCONT: %d,\"%s\",\"%s\",\"%s\",0,0\r\n",
                         data->id,
                         data->type == A_DATA_IP ? "IP" : "PPP",
                         data->apn,
                         addr );
    }
    return amodem_end_line(modem);
}

static const char*
handleQueryPDPDynamicProp( const char* cmd, AModem modem )
{
    int i, entries;

    assert( !memcmp( cmd, "+CGCONTRDP=?", 12 ) );

    entries = 0;
    amodem_begin_line( modem );
    amodem_add_line( modem, "+CGCONTRDP: (" );

    for ( i = 0; i < MAX_DATA_CONTEXTS; i++ ) {
        ADataContext context = modem->data_contexts + i;

        /* Returns the relevant information for an/all active non secondary PDP
         * contexts. */
        if ( !context->active )
            continue;

        ++entries;
        amodem_add_line( modem, ( entries == 1 ? "%d" : ",%d" ), context->id );
    }

    amodem_add_line(modem, ")");

    return amodem_end_line( modem );
}

static const char*
handleListPDPDynamicProp( const char* cmd, AModem modem )
{
    int cid = -1;
    int i, j, entries;

    assert( !memcmp( cmd, "+CGCONTRDP", 10 ) );

    cmd += 10;
    if ( '\0' == *cmd ) {
        // List all.
    } else if ( sscanf( cmd, "=%d", &cid ) != 1 ||
                cid <= 0 ) {
        return "+CME ERROR: 50"; // Incorrect parameters.
    }

    entries = 0;
    amodem_begin_line( modem );

    for ( i = 0; i < MAX_DATA_CONTEXTS; i++ ) {
        ADataContext context = modem->data_contexts + i;

        /* Returns the relevant information for an/all active non secondary PDP
         * contexts. */
        if ( !context->active )
            continue;

        if ( cid > 0 && context->id != cid )
            continue;

        ++entries;

        ADataNet net = context->net;
        char     addr[INET_ADDRSTRLEN];

        /* This is a dirty hack for passing kernel netif num to rild. */
        const char* bearer_id = net->nd->name + strlen("rmnet.");
        amodem_add_line( modem, "+CGCONTRDP: %d,%s,\"%s\"",
                         context->id, bearer_id, context->apn );

        inet_ntop( AF_INET, &net->addr.in, addr, sizeof addr);
        amodem_add_line( modem, ",\"%s/24\"", addr );
        inet_ntop( AF_INET, &net->gw.in, addr, sizeof addr);
        amodem_add_line( modem, ",\"%s\"", addr );
        for ( j = 0; j < NUM_DNS_PER_RMNET; j++ ) {
            if (!net->dns[j].in.s_addr) {
                break;
            }
            inet_ntop( AF_INET, &net->dns[j].in, addr, sizeof addr);
            amodem_add_line( modem, ",\"%s\"", addr );
        }

        amodem_add_line( modem, "\r\n" );
    }

    if ( cid > 0 && !entries ) {
        return "+CME ERROR: 50"; // Incorrect parameters.
    }

    if ( entries ) {
        // Remove the trailing "\r\n"
        modem->out_size -= 2;
    }

    return amodem_end_line( modem );
}

static const char*
handleActivatePDPContext( const char*  cmd, AModem  modem )
{
    int enable, cid, items;

    assert( !memcmp( cmd, "+CGACT=", 7 ) );

    cmd += 7;
    if (cmd[0] == '?') {
        // +CGACT=? is used to query the list of supported <state>s.
        return "+CGACT: (0-1)\r\n";
    }

    items = sscanf(cmd, "%d,%d", &enable, &cid);
    if (items != 2) {
        // activation rejected, unspecified
        return "+CME ERROR: 131";
    }

    return amodem_activate_data_call(modem, cid, enable);
}

static const char*
handleStartPDPContext( const char*  cmd, AModem  modem )
{
    /* D*99***<n>#
     * <n> is the <cid> in the +CGDCONT command
     */
    cmd += 7;
    return amodem_activate_data_call(modem, cmd[0] - '0', 1);
}


static void
remote_voice_call_event( void*  _vcall, int  success )
{
    AVoiceCall  vcall = _vcall;
    AModem      modem = vcall->modem;

    /* NOTE: success only means we could send the "gsm in new" command
     * to the remote emulator, nothing more */

    if (!success) {
        /* aargh, the remote emulator probably quitted at that point */
        amodem_free_call(modem, vcall, CALL_FAIL_NORMAL);
        amodem_unsol( modem, "NO CARRIER\r");
    }
}


static void
voice_call_event( void*  _vcall )
{
    AVoiceCall  vcall = _vcall;
    ACall       call  = &vcall->call;

    switch (call->state) {
        case A_CALL_DIALING:
            // Check number is valid or not.
            if (strspn(call->number, "+0123456789") != strlen(call->number)) {
                amodem_free_call(vcall->modem, vcall, CALL_FAIL_UNOBTAINABLE_NUMBER);
                break;
            }

            call->state = A_CALL_ALERTING;

            if (vcall->is_remote) {
                if ( remote_call_dial( call->number, vcall->modem,
                                       remote_voice_call_event, vcall ) < 0 )
                {
                   /* we could not connect, probably because the corresponding
                    * emulator is not running, so simply destroy this call.
                    * XXX: should we send some sort of message to indicate BAD NUMBER ? */
                    /* it seems the Android code simply waits for changes in the list   */
                    amodem_free_call( vcall->modem, vcall, CALL_FAIL_NORMAL );
                }
            }
            break;

        case A_CALL_ALERTING:
            break;

        case A_CALL_ACTIVE:
            break;

        case A_CALL_HELD:
            break;

        case A_CALL_INCOMING:
            break;

        case A_CALL_WAITING:
            break;

        default:
            assert( 0 && "unreachable event call state" );
    }
    amodem_send_calls_update(vcall->modem);
}

static int amodem_is_emergency( AModem modem, const char *number )
{
    int i;

    if (!number) return 0;
    for (i = 0; i < MAX_EMERGENCY_NUMBERS; i++) {
        if ( modem->emergency_numbers[i] && !strcmp( number, modem->emergency_numbers[i] )) break;
    }

    if (i < MAX_EMERGENCY_NUMBERS) return 1;

    return 0;
}

static const char*
handleDial( const char*  cmd, AModem  modem )
{
    AVoiceCall  vcall = amodem_alloc_call( modem );
    ACall       call  = &vcall->call;
    int         len;

    if (call == NULL)
        return "ERROR: TOO MANY CALLS";

    assert( cmd[0] == 'D' );
    call->dir   = A_CALL_OUTBOUND;
    call->state = A_CALL_DIALING;
    call->mode  = A_CALL_VOICE;
    call->multi = 0;

    cmd += 1;
    len  = strlen(cmd);
    if (len > 0 && cmd[len-1] == ';')
        len--;

    /* clir */
    if (len > 0 && (cmd[len-1] == 'I' || cmd[len-1] == 'i'))
        len--;

    if (len >= sizeof(call->number))
        len = sizeof(call->number)-1;

    /* Converts 4, 5, 7, and 10 digits number to 11 digits */
    if ((len == 10 && (!strncmp(cmd, PHONE_PREFIX+1, 5) && ((cmd[5] - '1') == modem->instance_id)))
        || (len == 7 && (!strncmp(cmd, PHONE_PREFIX+4, 2) && ((cmd[2] - '1') == modem->instance_id)))
        || (len == 5 && ((cmd[0] - '1') == modem->instance_id))) {
        memcpy( call->number, PHONE_PREFIX, 11 - len );
        memcpy( call->number + 11 - len, cmd, len );
        call->number[11] = 0;
    } else if (len == 4) {
        memcpy( call->number, PHONE_PREFIX, 6 );
        call->number[6] = '1' + modem->instance_id;
        memcpy( call->number+7, cmd, len );
        call->number[11] = 0;
    } else {
        memcpy( call->number, cmd, len );
        call->number[len] = 0;
    }

    call->numberPresentation = 0;

    amodem_send_calls_update( modem );

    amodem_begin_line( modem );
    if (amodem_is_emergency(modem, call->number)) {
        modem->in_emergency_mode = 1;
        amodem_add_line( modem, "+WSOS: 1" );
    }
    vcall->is_remote = (remote_number_string_to_port(call->number, modem, NULL, NULL) > 0);

    vcall->timer = sys_timer_create();
    sys_timer_set( vcall->timer, sys_time_ms() + CALL_DELAY_DIAL,
                   voice_call_event, vcall );

    return amodem_end_line( modem );
}


static const char*
handleAnswer( const char*  cmd, AModem  modem )
{
    int  nn;
    for (nn = 0; nn < modem->call_count; nn++) {
        AVoiceCall  vcall = modem->calls + nn;
        ACall       call  = &vcall->call;

        if (cmd[0] == 'A') {
            if (call->state == A_CALL_INCOMING) {
                acall_set_state( vcall, A_CALL_ACTIVE );
            }
            else if (call->state == A_CALL_ACTIVE) {
                acall_set_state( vcall, A_CALL_HELD );
            }
        } else if (cmd[0] == 'H') {
            /* ATH: hangup, since user is busy */
            if (call->state == A_CALL_INCOMING) {
                amodem_free_call( modem, vcall, CALL_FAIL_NORMAL );
                break;
            }
        }
    }
    return "OK";
}

int android_snapshot_update_time = 1;
int android_snapshot_update_time_request = 0;

static const char*
handleSignalStrength( const char*  cmd, AModem  modem )
{
    amodem_begin_line( modem );

    /* Sneak time updates into the SignalStrength request, because it's periodic.
     * Ideally, we'd be able to prod the guest into asking immediately on restore
     * from snapshot, but that'd require a driver.
     */
    if ( android_snapshot_update_time && android_snapshot_update_time_request ) {
      amodem_addTimeUpdate( modem );
      android_snapshot_update_time_request = 0;
    }

    // rssi = 0 (<-113dBm) 1 (<-111) 2-30 (<-109--53) 31 (>=-51) 99 (?!)
    // ber (bit error rate) - always 99 (unknown), apparently.
    // TODO: return 99 if modem->radio_state==A_RADIO_STATE_OFF, once radio_state is in snapshot.
    int rssi = modem->rssi;
    int ber = modem->ber;
    rssi = (0 > rssi || rssi > 31) ? 99 : rssi ;
    ber = (0 > ber || ber > 7 ) ? 99 : ber;

    // Handling of LTE signal strength.
    int rxlev = modem->rxlev;
    int rsrp = modem->rsrp;
    int rssnr = modem->rssnr;
    rxlev = (0 > rxlev || rxlev > 63) ? 99 : rxlev;
    rsrp = (44 > rsrp || rsrp > 140) ? 0x7FFFFFFF : rsrp;
    rssnr = (-200 > rssnr || rssnr > 300) ? 0x7FFFFFFF : rssnr;

    amodem_add_line( modem, "+CSQ: %i,%i,85,130,90,6,4,%i,%i,2147483647,%i,2147483647\r\n", rssi, ber, rxlev, rsrp, rssnr );
    return amodem_end_line( modem );
}

static int
hasWaitingCall( AModem  modem )
{
  int nn;
  for (nn = 0; nn < modem->call_count; nn++) {
    AVoiceCall  vcall = modem->calls + nn;
    ACall       call  = &vcall->call;
    if (call->mode == A_CALL_VOICE && call->state == A_CALL_WAITING) {
      return 1;
    }
  }
  return 0;
}

static const char*
handleHangup( const char*  cmd, AModem  modem )
{
    if ( !memcmp(cmd, "+CHLD=", 6) ) {
        int  nn;
        cmd += 6;
        switch (cmd[0]) {
            case '0':  /* release all held, and set busy for waiting calls */
                for (nn = 0; nn < modem->call_count; nn++) {
                    AVoiceCall  vcall = modem->calls + nn;
                    ACall       call  = &vcall->call;
                    if (call->mode != A_CALL_VOICE)
                        continue;
                    if (call->state == A_CALL_HELD    ||
                        call->state == A_CALL_WAITING ||
                        call->state == A_CALL_INCOMING) {
                        amodem_free_call(modem, vcall, CALL_FAIL_NORMAL);
                        nn--;
                    }
                }
                break;

            case '1':
                if (cmd[1] == 0) { /* release all active, accept held one */
                    int waitingCallOnly = hasWaitingCall(modem);
                    for (nn = 0; nn < modem->call_count; nn++) {
                        AVoiceCall  vcall = modem->calls + nn;
                        ACall       call  = &vcall->call;
                        if (call->mode != A_CALL_VOICE)
                            continue;
                        if (call->state == A_CALL_ACTIVE) {
                            amodem_free_call(modem, vcall, CALL_FAIL_NORMAL);
                            nn--;
                        }
                        else if ((call->state == A_CALL_HELD && !waitingCallOnly) ||
                                 call->state == A_CALL_WAITING) {
                            acall_set_state( vcall, A_CALL_ACTIVE );
                        }
                    }
                } else {  /* release specific call */
                    int  id = cmd[1] - '0';
                    AVoiceCall  vcall = amodem_find_call( modem, id );
                    if (vcall != NULL)
                        amodem_free_call( modem, vcall, CALL_FAIL_NORMAL );
                }
                break;

            case '2':
                if (cmd[1] == 0) {  /* place all active on hold, accept held or waiting one */
                    int waitingCallOnly = hasWaitingCall(modem);
                    for (nn = 0; nn < modem->call_count; nn++) {
                        AVoiceCall  vcall = modem->calls + nn;
                        ACall       call  = &vcall->call;
                        if (call->mode != A_CALL_VOICE)
                            continue;
                        if (call->state == A_CALL_ACTIVE) {
                            acall_set_state( vcall, A_CALL_HELD );
                        }
                        else if ((call->state == A_CALL_HELD && !waitingCallOnly) ||
                                 call->state == A_CALL_WAITING) {
                            acall_set_state( vcall, A_CALL_ACTIVE );
                        }
                    }
                } else {  /* place all active on hold, except a specific one */
                    int   id = cmd[1] - '0';
                    for (nn = 0; nn < modem->call_count; nn++) {
                        AVoiceCall  vcall = modem->calls + nn;
                        ACall       call  = &vcall->call;
                        if (call->mode != A_CALL_VOICE)
                            continue;
                        if (call->id == id) {
                            if (call->state != A_CALL_ACTIVE)
                                return "+CME ERROR: 3";
                        } else if (call->state == A_CALL_HELD)
                            return "+CME ERROR: 3";
                    }

                    // Checked, now proceed to set states.
                    for (nn = 0; nn < modem->call_count; nn++) {
                        AVoiceCall  vcall = modem->calls + nn;
                        ACall       call  = &vcall->call;
                        if (call->mode != A_CALL_VOICE)
                            continue;
                        if (call->id == id)
                            acall_unset_multi( vcall );
                        else if (call->state == A_CALL_ACTIVE)
                            acall_set_state( vcall, A_CALL_HELD );
                    }
                }
                break;

            case '3': { /* Join a single active call and a single held call together, or
                         * join a single held call and an active MPTY together, or
                         * join a single active call and a held MPTY together.
                         * See 3GPP TS 22.084, clause 1.3.8.1 and 1.3.8.4.
                         */
                if (modem->call_count < 2)
                    return "+CME ERROR: 3";

                if (modem->multi_count >= 5) {
                    // In gsm, the maximum number of multiparty calls is 5.
                    // See 3GPP TS 22.084, clause 1.2.1.
                    return "+CME ERROR: 3";
                }

                bool  hasHeld = false;
                int  id = -1;
                for (nn = 0; nn < modem->call_count; nn++) {
                    AVoiceCall  vcall = modem->calls + nn;
                    ACall       call  = &vcall->call;
                    if (call->mode != A_CALL_VOICE)
                        continue;
                    if (call->state == A_CALL_HELD) {
                        hasHeld = true;
                    }
                    else if (call->state == A_CALL_ACTIVE) {
                       if (id == -1)
                           id = call->id;
                    }
                }

                if (!hasHeld || id == -1)
                    return "+CME ERROR: 3";

                // Checked, now proceed to set states.
                for (nn = 0; nn < modem->call_count; nn++) {
                    AVoiceCall  vcall = modem->calls + nn;
                    ACall       call  = &vcall->call;
                    if (call->mode != A_CALL_VOICE)
                        continue;
                    if (call->state == A_CALL_HELD) {
                        acall_set_multi( vcall );
                        acall_set_state( vcall, A_CALL_ACTIVE );
                    }
                    else if (call->state == A_CALL_ACTIVE) {
                       if (call->id == id)
                           acall_set_multi( vcall );
                    }
                }
                break;
            }

            case '4':  /* connect the two calls */
                for (nn = 0; nn < modem->call_count; nn++) {
                    AVoiceCall  vcall = modem->calls + nn;
                    ACall       call  = &vcall->call;
                    if (call->mode != A_CALL_VOICE)
                        continue;
                    if (call->state == A_CALL_HELD) {
                        acall_set_state( vcall, A_CALL_ACTIVE );
                        break;
                    }
                }
                break;
        }
        amodem_send_calls_update( modem );
    }
    else
        return "ERROR: BAD COMMAND";

    return "OK";
}

/*
 * SMSC address AT command handler
 *
 * @see 3GPP 27.005 Clause 3.3.1
 */
SmsAddress
amodem_get_smsc_address( AModem  modem )
{
    return &modem->smsc_address;
}

int
amodem_set_smsc_address( AModem  modem, const char *smsc, unsigned char toa )
{
    SmsAddressRec smsc_address;
    sms_address_from_str( &smsc_address, smsc, strlen(smsc) );

    if (toa == 0 || toa == smsc_address.toa) {
        memcpy( &modem->smsc_address, &smsc_address, sizeof(SmsAddressRec) );
        amodem_nvram_set( modem, NV_MODEM_SMSC_ADDRESS, smsc );
        return 0;
    }

    return -1;
}

static const char*
handleSmscAddress( const char*  cmd, AModem  modem )
{
    char address[32] = {0};
    if ( !memcmp(cmd, "+CSCA?", 6) ) {
        // Get SMSC address
        // Return format
        //   +CSCA: "<sca>",<tosca>
        sms_address_to_str( &modem->smsc_address, address, sizeof(address) - 1 );
        return amodem_printf( modem, "+CSCA: \"%s\",%d", address,
                                                  modem->smsc_address.toa );
    } else if ( !memcmp(cmd, "+CSCA=", 6) ) {
        // Set SMSC address
        // Expect format
        //   +CSCA="<sca>"[,<tosca>]

        // Get sca
        const char *addr_begin = strchr(cmd, '"');
        if (!addr_begin) {
            goto EndCommand;
        }

        addr_begin++;
        const char *addr_end = strchr(addr_begin, '"');
        if (!addr_end) {
            goto EndCommand;
        }

        int addr_len = (int)(addr_end - addr_begin);
        if (addr_len >= sizeof(address)) {
            addr_len = sizeof(address) - 1;
        }

        strncpy(address, addr_begin, addr_len);

        // Get tosca if possible
        unsigned char toa = 0;
        const char *toa_pos = strchr(addr_end, ',');
        if (toa_pos) {
            toa_pos++;
            toa = (unsigned char)atoi(toa_pos);
        }

        if (amodem_set_smsc_address(modem, address, toa)) {
            goto EndCommand;
        }

        return "OK";
    }
EndCommand:
    return "+CMS ERROR: 304";
}

/* a function used to deal with a non-trivial request */
typedef const char*  (*ResponseHandler)(const char*  cmd, AModem  modem);

static const struct {
    const char*      cmd;     /* command coming from libreference-ril.so, if first
                                 character is '!', then the rest is a prefix only */

    const char*      answer;  /* default answer, NULL if needs specific handling or
                                 if OK is good enough */

    ResponseHandler  handler; /* specific handler, ignored if 'answer' is not NULL,
                                 NULL if OK is good enough */
} sDefaultResponses[] =
{
    /* see onRadioPowerOn() */
    { "%CPHS=1", NULL, NULL },
    { "%CTZV=1", NULL, NULL },

    /* see onSIMReady() */
    { "+CSMS=1", "+CSMS: 1, 1, 1", NULL },
    { "+CNMI=1,2,2,1,1", NULL, NULL },

    /* see requestRadioPower() */
    { "+CFUN=0", NULL, handleRadioPower },
    { "+CFUN=1", NULL, handleRadioPower },

    { "+CTEC=?", "+CTEC: 0,1,2,3", NULL }, /* Query available Techs */
    { "!+CTEC", NULL, handleTech }, /* Set/get current Technology and preferred mode */

    { "+WRMP=?", "+WRMP: 0,1,2", NULL }, /* Query Roam Preference */
    { "!+WRMP", NULL, handleRoamPref }, /* Set/get Roam Preference */

    { "+CCSS=?", "+CTEC: 0,1", NULL }, /* Query available subscription sources */
    { "!+CCSS", NULL, handleSubscriptionSource }, /* Set/Get current subscription source */

    { "+WSOS=?", "+WSOS: 0", NULL}, /* Query supported +WSOS values */
    { "!+WSOS=", NULL, handleEmergencyMode },

    { "+WPRL?", NULL, handlePrlVersion }, /* Query the current PRL version */

    /* see requestOrSendPDPContextList() */
    { "+CGACT?", NULL, handleListPDPContexts },

    /* see requestOperator() */
    { "+COPS=3,0;+COPS?;+COPS=3,1;+COPS?;+COPS=3,2;+COPS?", NULL, handleRequestOperator },

    /* see requestQueryNetworkSelectionMode() */
    { "!+COPS", NULL, handleOperatorSelection },

    /* see requestGetCurrentCalls() */
    { "+CLCC", NULL, handleListCurrentCalls },

    /* see requestWriteSmsToSim() */
    { "!+CMGW=", NULL, handleSendSMStoSIM },

    /* see requestHangup() */
    { "!+CHLD=", NULL, handleHangup },

    /* see requestSignalStrength() */
    { "+CSQ", NULL, handleSignalStrength },

    /* see requestRegistrationState() */
    { "!+CREG", NULL, handleNetworkRegistration },
    { "!+CGREG", NULL, handleNetworkRegistration },

    /* see requestSendSMS() */
    { "!+CMGS=", NULL, handleSendSMS },

    /* see requestSetupDefaultPDP() */
    { "%CPRIM=\"GMM\",\"CONFIG MULTISLOT_CLASS=<10>\"", NULL, NULL },
    { "%DATA=2,\"UART\",1,,\"SER\",\"UART\",0", NULL, NULL },

    { "!+CGDCONT=", NULL, handleDefinePDPContext },
    { "+CGDCONT?", NULL, handleQueryPDPContext },
    { "+CGCONTRDP=?", NULL, handleQueryPDPDynamicProp },
    { "!+CGCONTRDP", NULL, handleListPDPDynamicProp },
    { "!+CGQREQ=", NULL, NULL },
    { "!+CGQMIN=", NULL, NULL },
    { "+CGEREP=1,0", NULL, NULL },
    { "!+CGACT=", NULL, handleActivatePDPContext },
    { "!D*99***", NULL, handleStartPDPContext },

    /* see requestDial() */
    { "!D", NULL, handleDial },  /* the code says that success/error is ignored, the call state will
                              be polled through +CLCC instead */

    /* see requestSMSAcknowledge() */
    { "+CNMA=1", NULL, NULL },
    { "+CNMA=2", NULL, NULL },

    /* see requestSIM_IO() */
    { "!+CRSM=", NULL, handleSIM_IO },

    /* see onRequest() */
    { "+CHLD=0", NULL, handleHangup },
    { "+CHLD=1", NULL, handleHangup },
    { "+CHLD=2", NULL, handleHangup },
    { "+CHLD=3", NULL, handleHangup },
    { "A", NULL, handleAnswer },  /* answer the call */
    { "H", NULL, handleAnswer },  /* user is busy */
    { "!+VTS=", NULL, handleSetDialTone },
    { "+CIMI", OPERATOR_HOME_MCCMNC "000000000", NULL },   /* request internation subscriber identification number */
    { "+CGSN", "000000000000000", NULL },   /* request model version */
    { "+CUSD=2",NULL, NULL }, /* Cancel USSD */
    { "+COPS=0", NULL, handleOperatorSelection }, /* set network selection to automatic */
    { "!+CMGD=", NULL, handleDeleteSMSonSIM }, /* delete SMS on SIM */
    { "!+CPIN=", NULL, handleChangeOrEnterPIN },
    { "!+CPINR=", NULL, handleGetRemainingRetries }, /* get remaining PIN retries*/
    { "+CEER", NULL, handleLastCallFailCause },
    { "!+CCFC", NULL, handleCallForwardReq }, /* call forward request */
    { "!+CLCK", NULL, handleFacilityLockReq }, /* facility lock request */

    /* see getSIMStatus() */
    { "+CPIN?", NULL, handleSIMStatusReq },
    { "+CNMI?", "+CNMI: 1,2,2,1,1", NULL },

    /* see isRadioOn() */
    { "+CFUN?", NULL, handleRadioPowerReq },

    /* see initializeCallback() */
    { "E0Q0V1", NULL, NULL },
    { "S0=0", NULL, NULL },
    { "+CMEE=1", NULL, NULL },
    { "+CCWA=1", NULL, NULL },
    { "+CMOD=0", NULL, NULL },
    { "+CMUT=0", NULL, NULL },
    { "+CSSN=0,1", NULL, NULL },
    { "+COLP=0", NULL, NULL },
    { "+CSCS=\"HEX\"", NULL, NULL },
    { "+CUSD=1", NULL, NULL },
    { "+CGEREP=1,0", NULL, NULL },
    { "+CMGF=0", NULL, handleEndOfInit },  /* now is a goof time to send the current tme and timezone */
    { "%CPI=3", NULL, NULL },
    { "%CSTAT=1", NULL, NULL },

    { "!+CSCA", NULL, handleSmscAddress },

    /* end of list */
    {NULL, NULL, NULL}
};


#define  REPLY(str)  do { amodem_reply(modem, str); return modem->wait_sms; } while (0)

int  amodem_send( AModem  modem, const char*  cmd )
{
    const char*  answer;

    if ( modem->wait_sms != 0 ) {
        modem->wait_sms = 0;
        R( "SMS<< %s\n", quote(cmd) );
        answer = handleSendSMSText( cmd, modem );
        if (answer) {
            amodem_reply(modem, answer);
        }
        return modem->wait_sms;
    }

    /* everything that doesn't start with 'AT' is not a command, right ? */
    if ( cmd[0] != 'A' || cmd[1] != 'T' || cmd[2] == 0 ) {
        /* R( "-- %s\n", quote(cmd) ); */
        return modem->wait_sms;
    }
    R( "<< %s\n", quote(cmd) );

    cmd += 2;

    /* TODO: implement command handling */
    {
        int  nn, found = 0;

        for (nn = 0; ; nn++) {
            const char*  scmd = sDefaultResponses[nn].cmd;

            if (!scmd) /* end of list */
                break;

            if (scmd[0] == '!') { /* prefix match */
                int  len = strlen(++scmd);

                if ( !memcmp( scmd, cmd, len ) ) {
                    found = 1;
                    break;
                }
            } else { /* full match */
                if ( !strcmp( scmd, cmd ) ) {
                    found = 1;
                    break;
                }
            }
        }

        if ( !found )
        {
            D( "** UNSUPPORTED COMMAND **\n" );
            REPLY( "ERROR: UNSUPPORTED" );
        }
        else
        {
            const char*      answer  = sDefaultResponses[nn].answer;
            ResponseHandler  handler = sDefaultResponses[nn].handler;

            if ( answer != NULL ) {
                REPLY( answer );
            }

            if (handler == NULL) {
                REPLY( "OK" );
            }

            answer = handler( cmd, modem );
            if (answer == NULL) {
                // handler has sent reply out.
                return modem->wait_sms;
            }

            REPLY( answer );
        }
    }
}
