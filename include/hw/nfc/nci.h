/* Copyright (C) 2013 Mozilla Foundation
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

#ifndef nfc_nci_h
#define nfc_nci_h

#ifndef BITORDER_MSB_FIRST
#define BITORDER_MSB_FIRST 0
#endif

#include <stddef.h>
#include <stdint.h>

struct nfc_device;
struct nfc_re;

enum nci_mt {
    NCI_MT_DTA = 0x0,
    NCI_MT_CMD = 0x1,
    NCI_MT_RSP = 0x2,
    NCI_MT_NTF = 0x3
};

enum nci_pbf {
    NCI_PBF_END = 0x0,
    NCI_PBF_SEG = 0x1
};

enum nci_gid {
    NCI_GID_CORE = 0x0,
    NCI_GID_RF = 0x1,
    NCI_GID_NFCEE = 0x2,
    NCI_GID_PROP = 0xf
};

enum nci_oid {
    /* GID == 0x0 */
    NCI_OID_CORE_RESET_CMD = 0x00,
    NCI_OID_CORE_RESET_RSP = 0x00,
    NCI_OID_CORE_RESET_NTF = 0x00,
    NCI_OID_CORE_INIT_CMD = 0x01,
    NCI_OID_CORE_INIT_RSP = 0x01,
    NCI_OID_CORE_SET_CONFIG_CMD = 0x02,
    NCI_OID_CORE_SET_CONFIG_RSP = 0x02,
    NCI_OID_CORE_GET_CONFIG_CMD = 0x03,
    NCI_OID_CORE_GET_CONFIG_RSP = 0x03,
    NCI_OID_CORE_CONN_CREATE_CMD = 0x04,
    NCI_OID_CORE_CONN_CREATE_RSP = 0x04,
    NCI_OID_CORE_CONN_CLOSE_CMD = 0x05,
    NCI_OID_CORE_CONN_CLOSE_RSP = 0x05,
    NCI_OID_CORE_CONN_CREDITS_NTF = 0x06,
    NCI_OID_CORE_GENERIC_ERROR_NTF = 0x07,
    NCI_OID_CORE_INTERFACE_ERROR_NTF = 0x08,
    /* GID == 0x1 */
    NCI_OID_RF_DISCOVER_MAP_CMD = 0x00,
    NCI_OID_RF_DISCOVER_MAP_RSP = 0x00,
    NCI_OID_RF_SET_LISTEN_MODE_ROUTING_CMD = 0x01,
    NCI_OID_RF_SET_LISTEN_MODE_ROUTING_RSP = 0x01,
    NCI_OID_RF_GET_LISTEN_MODE_ROUTING_CMD = 0x02,
    NCI_OID_RF_GET_LISTEN_MODE_ROUTING_RSP = 0x02,
    NCI_OID_RF_GET_LISTEN_MODE_ROUTING_NTF = 0x02,
    NCI_OID_RF_DISCOVER_CMD = 0x03,
    NCI_OID_RF_DISCOVER_RSP = 0x03,
    NCI_OID_RF_DISCOVER_NTF = 0x03,
    NCI_OID_RF_DISCOVER_SELECT_CMD = 0x04,
    NCI_OID_RF_DISCOVER_SELECT_RSP = 0x04,
    NCI_OID_RF_INTF_ACTIVATED_NTF = 0x05,
    NCI_OID_RF_DEACTIVATED_CMD = 0x06,
    NCI_OID_RF_DEACTIVATED_RSP = 0x06,
    NCI_OID_RF_DEACTIVATED_NTF = 0x06,
    NCI_OID_RF_FIELD_INFO_NTF = 0x07,
    NCI_OID_RF_T3T_POLLING_CMD = 0x08,
    NCI_OID_RF_T3T_POLLING_RSP = 0x08,
    NCI_OID_RF_T3T_POLLING_NTF = 0x08,
    NCI_OID_RF_NFCEE_ACTION_NTF = 0x09,
    NCI_OID_RF_NFCEE_DISCOVERY_REQ_NTF = 0x0a,
    NCI_OID_RF_PARAMETER_UPDATE_CMD = 0x0b,
    NCI_OID_RF_PARAMETER_UPDATE_RSP = 0x0b,
    /* GID == 0x2 */
    NCI_OID_NFCEE_DISCOVER_CMD = 0x00,
    NCI_OID_NFCEE_DISCOVER_RSP = 0x00,
    NCI_OID_NFCEE_DISCOVER_NTF = 0x00,
    NCI_OID_NFCEE_MODE_SET_CMD = 0x01,
    NCI_OID_NFCEE_MODE_SET_RSP = 0x01,
    /* GID == 0xf */
    NCI_OID_BCM2079x_GET_BUILD_INFO_CMD = 0x4,
    NCI_OID_BCM2079x_GET_BUILD_INFO_RSP = 0x4,
    NCI_OID_BCM2079x_HCI_NETWK_CMD = 0x5,
    NCI_OID_BCM2079x_HCI_NETWK_RSP = 0x5,
    NCI_OID_BCM2079x_SET_FWFSM_CMD = 0x6,
    NCI_OID_BCM2079x_SET_FWFSM_RSP = 0x6,
    NCI_OID_BCM2079x_GET_PATCH_VERSION_CMD = 0x2d,
    NCI_OID_BCM2079x_GET_PATCH_VERSION_RSP = 0x2d
};

enum {
    NUMBER_OF_NCI_CMDS = 64
};

enum nci_status_code {
    /* generic status codes  */
    NCI_STATUS_OK = 0x00,
    NCI_STATUS_REJECTED = 0x01,
    NCI_STATUS_RF_FRAME_CORRUPTED = 0x02,
    NCI_STATUS_FAILED = 0x03,
    NCI_STATUS_NOT_INITIALIZED = 0x04,
    NCI_STATUS_SYNTAX_ERROR = 0x05,
    NCI_STATUS_SEMANTIC_ERROR = 0x06,
    NCI_STATUS_INVALID_PARAM = 0x09,
    NCI_STATUS_MESSAGE_SIZE_EXCEEDED = 0xa,
    /* RF discovery status codes */
    NCI_DISCOVERY_ALREADY_STARTED = 0xa0,
    NCI_DISCOVERY_TARGET_ACTIVATION_FAILED = 0xa1,
    NCI_DISCOVERY_TEAR_DOWN = 0xa2,
    /* RF transmission status codes */
    NCI_RF_TRANSMISSION_ERROR = 0xb0,
    NCI_RF_PROTOCOL_ERROR = 0xb1,
    NCI_RF_TIMEOUT_ERROR = 0xb2,
    /* NFCEE status codes */
    NCI_NFCEE_INTERFACE_ACTIVATION_FAILED = 0xc0,
    NCI_NFCEE_TRANSMISSION_ERROR = 0xc1,
    NCI_NFCEE_PROTOCOL_ERROR = 0xc2,
    NCI_NFCEE_TIMEOUT_ERROR = 0xc3
};

enum nci_version {
    NCI_VERSION_1_0 = 0x10
};

enum nci_notification_type {
    NCI_LAST_NOTIFICATION = 0,
    NCI_LIMIT_NOTIFICATION = 1,
    NCI_MORE_NOTIFICATIONS = 2
};

struct nci_common_hdr {
#if BITORDER_MSB_FIRST
    uint8_t mt:3;
    uint8_t pbf:1;
    uint8_t unused:4;
#else
    uint8_t unused:4;
    uint8_t pbf:1;
    uint8_t mt:3;
#endif
} __attribute__((packed));

struct nci_data_packet {
#if BITORDER_MSB_FIRST
    uint8_t mt:3;
    uint8_t pbf:1;
    uint8_t connid:4;
#else
    uint8_t connid:4;
    uint8_t pbf:1;
    uint8_t mt:3;
#endif
    uint8_t rfu;
    uint8_t l;
    uint8_t payload[256];
} __attribute__((packed));

struct nci_control_packet {
#if BITORDER_MSB_FIRST
    uint8_t mt:3;
    uint8_t pbf:1;
    uint8_t gid:4;
#else
    uint8_t gid:4;
    uint8_t pbf:1;
    uint8_t mt:3;
#endif
#if BITORDER_MSB_FIRST
    uint8_t rfu:2;
    uint8_t oid:6;
#else
    uint8_t oid:6;
    uint8_t rfu:2;
#endif
    uint8_t l;
    uint8_t payload[256];
};

union nci_packet {
    struct nci_common_hdr common;
    struct nci_data_packet data;
    struct nci_control_packet control;
};

/* NCI_CORE_RESET */

struct nci_core_reset_cmd {
    uint8_t rfconfig;
};

struct nci_core_reset_rsp {
    uint8_t status;
#if BITORDER_MSB_FIRST
    uint8_t major:4;
    uint8_t minor:4;
#else
    uint8_t minor:4;
    uint8_t major:4;
#endif
    uint8_t rfconfig;
};

/* NCI_CORE_INIT */

struct nci_core_init_cmd {
};

struct nci_core_init_rsp {
    uint8_t status;
    uint32_t features; /* little endian */
    uint8_t nrfs;
    uint8_t rf[1];
    uint8_t nconns;
    uint16_t maxrtabsize; /* little endian */
    uint8_t payloadsize;
    uint16_t maxlparamsize; /* little endian */
    uint8_t vendor;
    uint32_t device; /* little endian */
} __attribute__((packed));

/* NCI_CORE_SET_CONFIG */

enum nci_config_param_id {
    NCI_CONFIG_PARAM_TOTAL_DURATION = 0x00,
    NCI_CONFIG_PARAM_CON_DEVICES_LIMIT = 0x01,
    NCI_CONFIG_PARAM_PA_BAIL_OUT = 0x02,
    NCI_CONFIG_PARAM_PB_AFI = 0x10,
    NCI_CONFIG_PARAM_PB_BAIL_OUT = 0x11,
    NCI_CONFIG_PARAM_PB_ATTRIB_PARAM1 = 0x12,
    NCI_CONFIG_PARAM_PB_SENB_REQ_PARAM = 0x13,
    NCI_CONFIG_PARAM_PF_BIT_RATE = 0x18,
    NCI_CONFIG_PARAM_PF_RC_CODE = 0x19,
    NCI_CONFIG_PARAM_PB_H_INFO = 0x20,
    NCI_CONFIG_PARAM_PI_BIT_RATE = 0x21,
    NCI_CONFIG_PARAM_PA_ADV_FEAT = 0x22,
    NCI_CONFIG_PARAM_PN_NFC_DEP_SPEED = 0x28,
    NCI_CONFIG_PARAM_PN_ATR_REQ_GEN_BYTES = 0x29,
    NCI_CONFIG_PARAM_PN_ATR_REQ_CONFIG = 0x2a,
    NCI_CONFIG_PARAM_LA_BIT_FRAME_SDD = 0x30,
    NCI_CONFIG_PARAM_LA_PLATFORM_CONFIG = 0x31,
    NCI_CONFIG_PARAM_LA_SEL_INFO = 0x32,
    NCI_CONFIG_PARAM_LA_NFCID1 = 0x33,
    NCI_CONFIG_PARAM_LB_SENSB_INFO = 0x38,
    NCI_CONFIG_PARAM_LB_NFCID0 = 0x39,
    NCI_CONFIG_PARAM_LB_APPLICATION_DATA = 0x3a,
    NCI_CONFIG_PARAM_LB_SFGI = 0x3b,
    NCI_CONFIG_PARAM_LB_ADC_FO = 0x3c,
    NCI_CONFIG_PARAM_LF_T3T_IDENTIFIERS_1 = 0x40,
    NCI_CONFIG_PARAM_LF_T3T_IDENTIFIERS_2 = 0x41,
    NCI_CONFIG_PARAM_LF_T3T_IDENTIFIERS_3 = 0x42,
    NCI_CONFIG_PARAM_LF_T3T_IDENTIFIERS_4 = 0x43,
    NCI_CONFIG_PARAM_LF_T3T_IDENTIFIERS_5 = 0x44,
    NCI_CONFIG_PARAM_LF_T3T_IDENTIFIERS_6 = 0x45,
    NCI_CONFIG_PARAM_LF_T3T_IDENTIFIERS_7 = 0x46,
    NCI_CONFIG_PARAM_LF_T3T_IDENTIFIERS_8 = 0x47,
    NCI_CONFIG_PARAM_LF_T3T_IDENTIFIERS_9 = 0x48,
    NCI_CONFIG_PARAM_LF_T3T_IDENTIFIERS_10 = 0x49,
    NCI_CONFIG_PARAM_LF_T3T_IDENTIFIERS_11 = 0x4a,
    NCI_CONFIG_PARAM_LF_T3T_IDENTIFIERS_12 = 0x4b,
    NCI_CONFIG_PARAM_LF_T3T_IDENTIFIERS_13 = 0x4c,
    NCI_CONFIG_PARAM_LF_T3T_IDENTIFIERS_14 = 0x4d,
    NCI_CONFIG_PARAM_LF_T3T_IDENTIFIERS_15 = 0x4e,
    NCI_CONFIG_PARAM_LF_T3T_IDENTIFIERS_16 = 0x4f,
    NCI_CONFIG_PARAM_LF_PROTOCOL_TYPE = 0x50,
    NCI_CONFIG_PARAM_LF_T3T_PMM = 0x51,
    NCI_CONFIG_PARAM_LF_T3T_MAX = 0x52,
    NCI_CONFIG_PARAM_LF_T3T_FLAGS = 0x53,
    NCI_CONFIG_PARAM_LF_CON_BITR_F = 0x54,
    NCI_CONFIG_PARAM_LF_ADV_FEAT = 0x55,
    NCI_CONFIG_PARAM_LI_FWI = 0x58,
    NCI_CONFIG_PARAM_LA_HIST_BY = 0x59,
    NCI_CONFIG_PARAM_LB_H_INFO_RESP = 0x5a,
    NCI_CONFIG_PARAM_LI_BIT_RATE = 0x5b,
    NCI_CONFIG_PARAM_LN_WT = 0x60,
    NCI_CONFIG_PARAM_LN_ATR_RES_GEN_BYTES = 0x61,
    NCI_CONFIG_PARAM_LN_ATR_RES_CONFIG = 0x62,
    NCI_CONFIG_PARAM_RF_FIELD_INFO = 0x80,
    NCI_CONFIG_PARAM_RF_NFCEE_ACTION = 0x81,
    NCI_CONFIG_PARAM_NFCDEP_OP = 0x82,
    /* BMC2979x specific */
    NCI_CONFIG_PARAM_BCM2079x_CONTINUE_MODE = 0xa5,
    NCI_CONFIG_PARAM_BCM2079x_I93_DATARATE = 0xb7,
    NCI_CONFIG_PARAM_BCM2079x_TAGSNIFF_CFG = 0xb9,
    NCI_CONFIG_PARAM_BCM2079x_ACT_ORDER = 0xc5,
    NCI_CONFIG_PARAM_BCM2079x_RFU_CONFIG = 0xca,
    NCI_CONFIG_PARAM_BCM2079x_EMVCO_ENABLE = 0xcb
};

struct nci_core_config_field {
    uint8_t id;
    uint8_t len;
    uint8_t val[];
};

struct nci_core_set_config_cmd {
    uint8_t nparams;
    uint8_t param[];
};

struct nci_core_set_config_rsp {
    uint8_t status;
    uint8_t nparams;
    uint8_t param[];
};

/* NCI_CORE_GET_CONFIG */

struct nci_core_get_config_cmd {
    uint8_t nparams;
    uint8_t param[];
};

struct nci_core_get_config_rsp {
    uint8_t status;
    uint8_t nparams;
    uint8_t param[];
};

/* NCI_CORE_CONN_CREATE */

struct nci_core_conn_create_cmd {
    uint8_t desttype;
    uint8_t nparams;
    uint8_t params[];
};

struct nci_core_conn_create_rsp {
    uint8_t status;
    uint8_t maxpayloadsize;
    uint8_t ncredits;
    uint8_t connid;
};

/* NCI_CORE_CONN_CLOSE */

struct nci_core_conn_close_cmd {
    uint8_t connid;
};

struct nci_core_conn_close_rsp {
    uint8_t status;
};

/* NCI_CORE_CONN_CREDITS */

struct nci_core_conn_credits_ntf {
    uint8_t nentries;
    uint8_t entries[];
};

/* NCI_CORE_GENERIC_ERROR */

struct nci_core_generic_error_ntf {
    uint8_t status;
};

/* NCI_CORE_INTERFACE_ERROR */

struct nci_core_interface_error_ntf {
    uint8_t status;
    uint8_t connid;
};

/* NCI_RF_DISCOVER_MAP */

struct nci_rf_discover_mapping {
    uint8_t proto;
    uint8_t mode;
    uint8_t iface;
};

struct nci_rf_discover_map_cmd {
    uint8_t nmappings;
    struct nci_rf_discover_mapping mapping[];
};

/* NCI_RF_DISCOVER */

struct nci_rf_discover_config {
    uint8_t mode;
    uint8_t freq;
};

struct nci_rf_discover_cmd {
    uint8_t nconfigs;
    struct nci_rf_discover_config config[];
};

struct nci_rf_discover_ntf {
    uint8_t id;
    uint8_t rfproto;
    uint8_t mode;
    uint8_t nparams;
    uint8_t end[];
};

/* NCI_RF_DISCOVER_SELECT */

struct nci_rf_discover_select_cmd {
    uint8_t id;
    uint8_t rfproto;
    uint8_t iface;
};

/* NCI_RF_INTF_ACTIVATED */

/* [NCI], Table 97 */
enum nci_bitrate {
    NCI_NFC_BIT_RATE_106 = 0x01,
    NCI_NFC_BIT_RATE_212 = 0x02,
    NCI_NFC_BIT_RATE_424 = 0x03,
    NCI_NFC_BIT_RATE_848 = 0x04,
    NCI_NFC_BIT_RATE_1695 = 0x05,
    NCI_NFC_BIT_RATE_3390 = 0x06,
    NCI_NFC_BIT_RATE_6780 = 0x07
};

struct nci_rf_intf_activated_ntf {
    uint8_t id;
    uint8_t iface;
    uint8_t rfproto;
    uint8_t actmode;
    uint8_t maxpayload;
    uint8_t ncredits;
    uint8_t nparams;
    uint8_t param[];
};

struct nci_rf_intf_activated_ntf_tail {
    /* anything after params */
    uint8_t mode;
    uint8_t tx_bitrate;
    uint8_t rx_bitrate;
    uint8_t nactparams;
    uint8_t actparam[];
};

/* NCI_RF_DEACTIVATE */

enum nci_rf_deactivation_type {
    NCI_RF_DEACT_IDLE_MODE = 0x00,
    NCI_RF_DEACT_SLEEP_MODE = 0x01,
    NCI_RF_DEACT_SLEEP_AF_MODE = 0x02,
    NCI_RF_DEACT_DISCOVERY = 0x03
};

struct nci_rf_deactivate_cmd {
    uint8_t type;
};

/* NCI_RF_FIELD_INFO */

enum nci_rf_field_status {
    NCI_RF_FIELD_STATUS_REMOTE_GENERATED = 0x01
};

struct nci_rf_field_info_ntf {
    uint8_t status;
};

/* NCI_NFCEE_DISCOVERY */

enum nci_nfcee_discovery_enabled {
    NCI_NFCEE_DISCOVERY_DISABLED = 0x00,
    NCI_NFCEE_DISCOVERY_ENABLED = 0x01
};

struct nci_nfcee_discovery_cmd {
    uint8_t enable;
};

struct nci_nfcee_discovery_rsp {
    uint8_t status;
    uint8_t nnfcees;
};

/* NCI_BCM2079x_GET_PATCH_VERSION */

enum {
    NCI_BCM2079x_SPD_NVM_NONE = 0x00,
    NCI_BCM2079x_SPD_NVM_EEPROM = 0x01,
    NCI_BCM2079x_SPD_NVM_UICC = 0x02
};

struct nci_bcm2079x_get_patch_version_rsp {
    uint16_t projectid; /* little endian */
    uint8_t rfu;
    uint8_t chipver;
    uint8_t patchinfover[16];
    uint16_t major; /* little endian */
    uint16_t minor; /* little endian */
    uint16_t maxsize; /* little endian */
    uint16_t patchmaxsize; /* little endian */
    uint16_t lpmsize; /* little endian */
    uint16_t fpmsize; /* little endian */
    uint8_t lpmbadcrc;
    uint8_t fpmbadcrc;
    uint8_t nvmtype;
} __attribute__((packed));

size_t
nfc_process_nci_msg(const union nci_packet* cmd, struct nfc_device* nfc,
                    union nci_packet* rsp);

size_t
nfc_create_nci_dta(union nci_packet* rsp, enum nci_pbf pbf,
                   uint8_t connid, unsigned char l);

size_t
nfc_create_nci_ntf(union nci_packet* ntf, enum nci_pbf pbf,
                   enum nci_gid gid, enum nci_oid oid, unsigned char l);

size_t
nfc_create_rf_discovery_ntf(struct nfc_re* re,
                            struct nfc_device* nfc,
                            union nci_packet* ntf);

size_t
nfc_create_rf_intf_activated_ntf(struct nfc_re* re,
                                 struct nfc_device* nfc,
                                 union nci_packet* ntf);

size_t
nfc_create_rf_field_info_ntf(struct nfc_device* nfc,
                             union nci_packet* ntf);

size_t
nfc_create_dta(const void* data, size_t len,
               struct nfc_device* nfc,
               union nci_packet* nci);

#endif
