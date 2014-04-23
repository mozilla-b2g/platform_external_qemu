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

#include <assert.h>
#include <string.h>
#include "bswap.h"
#include "nfc-debug.h"
#include "nfc.h"
#include "nfc-re.h"
#include "nfc-nci.h"
#include "goldfish_nfc.h"

/* first value is offset, second is number of bytes */
static const uint8_t config_id_value[256][2] = {
    [NCI_CONFIG_PARAM_TOTAL_DURATION] = {0, 2},
    [NCI_CONFIG_PARAM_PN_NFC_DEP_SPEED] = {2, 1},
    [NCI_CONFIG_PARAM_PN_ATR_REQ_GEN_BYTES] = {3, 20},
    [NCI_CONFIG_PARAM_LA_BIT_FRAME_SDD] = {23, 1},
    [NCI_CONFIG_PARAM_LA_PLATFORM_CONFIG] = {24, 1},
    [NCI_CONFIG_PARAM_LA_SEL_INFO] = {25, 1},
    [NCI_CONFIG_PARAM_LB_SENSB_INFO] = {26, 1},
    [NCI_CONFIG_PARAM_LF_PROTOCOL_TYPE] = {27, 1},
    [NCI_CONFIG_PARAM_LF_T3T_PMM] = {28, 8},
    [NCI_CONFIG_PARAM_LI_FWI] = {36, 1},
    [NCI_CONFIG_PARAM_LN_WT] = {37, 1},
    [NCI_CONFIG_PARAM_LN_ATR_RES_GEN_BYTES] = {38, 20},
    [NCI_CONFIG_PARAM_RF_FIELD_INFO] = {58, 1},
    [NCI_CONFIG_PARAM_BCM2079x_CONTINUE_MODE] = {59, 1},
    [NCI_CONFIG_PARAM_BCM2079x_I93_DATARATE] = {60, 3},
    [NCI_CONFIG_PARAM_BCM2079x_TAGSNIFF_CFG] = {63, 33},
    [NCI_CONFIG_PARAM_BCM2079x_ACT_ORDER] = {96, 1},
    [NCI_CONFIG_PARAM_BCM2079x_RFU_CONFIG] = {97, 20},
    [NCI_CONFIG_PARAM_BCM2079x_EMVCO_ENABLE] = {117, 1}
};

/* default values for all config options */
static const uint8_t config_id_default[256][33] = {
  /* all zero */
};

static ssize_t
nfc_rf_field_info_ntf_cb(void* data,
                         struct nfc_device* nfc, size_t maxlen,
                         union nci_packet* ntf)
{
    return nfc_create_rf_field_info_ntf(nfc, ntf);
}

static void
nfc_nci_device_set(struct nfc_device* nfc, enum nci_config_param_id id,
                   uint8_t len, const uint8_t* value)
{
    assert(config_id_value[id][1]);
    assert(config_id_value[id][1] >= len);

    nfc_device_set(nfc, config_id_value[id][0], len, value);

    if ((id == NCI_CONFIG_PARAM_BCM2079x_I93_DATARATE) && (value[2] & 0x1)) {
        goldfish_nfc_send_ntf(nfc_rf_field_info_ntf_cb, NULL);
    }
}

static void
nfc_nci_device_get(struct nfc_device* nfc, enum nci_config_param_id id,
                   uint8_t len, uint8_t* value)
{
    assert(config_id_value[id][1] == len);

    nfc_device_get(nfc, config_id_value[id][0], len, value);
}

/*
 * Data
 */

size_t
nfc_create_nci_dta(union nci_packet* rsp, enum nci_pbf pbf,
                   uint8_t connid, unsigned char l)
{
    assert(rsp);

    NFC_D("creating NCI data pbf=%d connid=%d length=%d", pbf, connid, l);

    rsp->data.mt = NCI_MT_DTA;
    rsp->data.pbf = pbf;
    rsp->data.connid = connid;
    rsp->data.rfu = 0;
    rsp->data.l = l;

    return 3 + l;
}

static size_t
process_nci_dta(const union nci_packet* dta, struct nfc_device* nfc,
                union nci_packet* rsp)
{
    enum nfc_rfst rfst;

    assert(dta);
    assert(nfc);

    rfst = nfc_rf_state_transition(&nfc->rf_state,
                                   NFC_RFST_POLL_ACTIVE_BIT|
                                   NFC_RFST_LISTEN_ACTIVE_BIT,
                                   nfc->rf_state);
    assert(rfst != NUMBER_OF_NFC_RFSTS);

    /* data gets processed by RE */
    return nfc_re_process_data(nfc->active_re, dta, rsp);
}

size_t
nfc_create_dta(const void* data, size_t len,
               struct nfc_device* nfc,
               union nci_packet* nci)
{
    enum nfc_rfst rfst;
    size_t payload_len;

    assert(nfc);
    assert(nci);

    /* RF state transition */
    rfst = nfc_rf_state_transition(&nfc->rf_state,
                                   NFC_RFST_POLL_ACTIVE_BIT|
                                   NFC_RFST_LISTEN_ACTIVE_BIT,
                                   nfc->rf_state);
    assert(rfst != NUMBER_OF_NFC_RFSTS);

    /* create data message */

    payload_len = nfc_re_create_dta_act(nfc->active_re, data, len,
                                        nci->data.payload);

    return nfc_create_nci_dta(nci, NCI_PBF_END, nfc->active_re->connid,
                              payload_len);
}

/*
 * Commands
 */

static size_t
create_control_rsp(union nci_packet* rsp, enum nci_pbf pbf,
                   enum nci_gid gid, enum nci_oid oid, unsigned char l)
{
    assert(rsp);

    rsp->control.mt = NCI_MT_RSP;
    rsp->control.pbf = pbf;
    rsp->control.gid = gid;
    rsp->control.rfu = 0;
    rsp->control.oid = oid;
    rsp->control.l = l;

    return 3 + l;
}

static size_t
create_control_status_rsp(union nci_packet* rsp, enum nci_gid gid,
                          enum nci_oid oid, enum nci_status_code status)
{
    assert(rsp);

    rsp->control.payload[0] = status;

    return create_control_rsp(rsp, NCI_PBF_END, gid, oid, 1);
}

static size_t
create_semantic_error_rsp(const union nci_packet* cmd,
                          struct nfc_device* nfc,
                          union nci_packet* rsp)
{
    assert(cmd);

    return create_control_status_rsp(rsp, cmd->control.gid,
                                     cmd->control.oid,
                                     NCI_STATUS_SEMANTIC_ERROR);
}

/* IDLE state */

static size_t
idle_process_oid_core_reset_cmd(const union nci_packet* cmd,
                                struct nfc_device* nfc,
                                union nci_packet* rsp)
{
    assert(cmd);
    assert(nfc);
    assert(rsp);

    nfc->state = NFC_FSM_STATE_RESET;

    if (cmd->control.payload[0]) {
        nfc->rf_state = NFC_RFST_IDLE;
    }

    rsp->control.payload[0] = NCI_STATUS_OK;
    rsp->control.payload[1] = NCI_VERSION_1_0;
    rsp->control.payload[2] = cmd->control.payload[0];

    return create_control_rsp(rsp, NCI_PBF_END, cmd->control.gid,
                              cmd->control.oid, 3);
}

static size_t
idle_process_oid_bcm2079x_get_build_info_cmd(const union nci_packet* cmd,
                                             struct nfc_device* nfc,
                                             union nci_packet* rsp)
{
    // status code
    rsp->control.payload[0] = NCI_STATUS_OK;
    // unknown
    rsp->control.payload[1] = 0x4d;
    rsp->control.payload[2] = 0x61;
    rsp->control.payload[3] = 0x72;
    rsp->control.payload[4] = 0x20;
    rsp->control.payload[5] = 0x31;
    rsp->control.payload[6] = 0x37;
    rsp->control.payload[7] = 0x20;
    rsp->control.payload[8] = 0x32;
    rsp->control.payload[9] = 0x30;
    rsp->control.payload[10] = 0x31;
    rsp->control.payload[11] = 0x32;
    rsp->control.payload[12] = 0x31;
    rsp->control.payload[13] = 0x30;
    rsp->control.payload[14] = 0x3a;
    rsp->control.payload[15] = 0x35;
    rsp->control.payload[16] = 0x34;
    rsp->control.payload[17] = 0x3a;
    rsp->control.payload[18] = 0x31;
    rsp->control.payload[19] = 0x35;
    rsp->control.payload[20] = 0x1;
    rsp->control.payload[21] = 0x3;
    rsp->control.payload[22] = 0xa;
    rsp->control.payload[23] = 0x44;
    // HW id
    rsp->control.payload[24] = 0x3;
    rsp->control.payload[25] = 0x1b;
    rsp->control.payload[26] = 0x79;
    rsp->control.payload[27] = 0x20;
    // chip-version length
    rsp->control.payload[28] = 7;
    // chip version
    rsp->control.payload[29] = '2';
    rsp->control.payload[30] = '0';
    rsp->control.payload[31] = '7';
    rsp->control.payload[32] = '9';
    rsp->control.payload[33] = '1';
    rsp->control.payload[34] = 'B';
    rsp->control.payload[35] = '3';

    return create_control_rsp(rsp, NCI_PBF_END, cmd->control.gid,
                              cmd->control.oid, 36);
}

static size_t
idle_process_oid_bcm2079x_get_patch_version_cmd(const union nci_packet* cmd,
                                                struct nfc_device* nfc,
                                                union nci_packet* rsp)
{
    struct nci_bcm2079x_get_patch_version_rsp* payload;

    payload =
        (struct nci_bcm2079x_get_patch_version_rsp*)rsp->control.payload;

    payload->projectid = cpu_to_le16(0x100);
    payload->rfu = 0;
    payload->chipver = 7;
    payload->patchinfover[0] = '2';
    payload->patchinfover[1] = '0';
    payload->patchinfover[2] = '7';
    payload->patchinfover[3] = '9';
    payload->patchinfover[4] = '1';
    payload->patchinfover[5] = 'B';
    payload->patchinfover[6] = '3';
    memset(payload->patchinfover+payload->chipver, 0,
           sizeof(payload->patchinfover)-payload->chipver);
    payload->major = cpu_to_le16(0x93);
    payload->minor = cpu_to_le16(0x9d);
    payload->maxsize = cpu_to_le16(256);
    payload->patchmaxsize = cpu_to_le16(256);
    payload->lpmsize = cpu_to_le16(0);
    payload->fpmsize = cpu_to_le16(0x1b52);
    payload->lpmbadcrc = 0;
    payload->fpmbadcrc = 0;
    payload->nvmtype = NCI_BCM2079x_SPD_NVM_EEPROM;

    return create_control_rsp(rsp, NCI_PBF_END, cmd->control.gid,
                              cmd->control.oid, sizeof(*payload));
}

static size_t
idle_process_cmd(const union nci_packet* cmd, struct nfc_device* nfc,
                 union nci_packet* rsp)
{
    static size_t (* const core_oid[NUMBER_OF_NCI_CMDS])
      (const union nci_packet*, struct nfc_device*, union nci_packet*) = {
        [NCI_OID_CORE_RESET_CMD] = idle_process_oid_core_reset_cmd,
        [NCI_OID_CORE_INIT_CMD] = create_semantic_error_rsp,
        [NCI_OID_CORE_SET_CONFIG_CMD] = create_semantic_error_rsp,
        [NCI_OID_CORE_GET_CONFIG_CMD] = create_semantic_error_rsp,
        [NCI_OID_CORE_CONN_CREATE_CMD] = create_semantic_error_rsp,
        [NCI_OID_CORE_CONN_CLOSE_CMD] = create_semantic_error_rsp
    };
    static size_t (* const rf_oid[NUMBER_OF_NCI_CMDS])
      (const union nci_packet*, struct nfc_device*, union nci_packet*) = {
        [NCI_OID_RF_DISCOVER_MAP_CMD] = create_semantic_error_rsp,
        [NCI_OID_RF_SET_LISTEN_MODE_ROUTING_CMD] = create_semantic_error_rsp,
        [NCI_OID_RF_GET_LISTEN_MODE_ROUTING_CMD] = create_semantic_error_rsp,
        [NCI_OID_RF_DISCOVER_CMD] = create_semantic_error_rsp,
        [NCI_OID_RF_DISCOVER_SELECT_CMD] = create_semantic_error_rsp,
        [NCI_OID_RF_DEACTIVATED_CMD] = create_semantic_error_rsp,
        [NCI_OID_RF_T3T_POLLING_CMD] = create_semantic_error_rsp,
        [NCI_OID_RF_PARAMETER_UPDATE_CMD] = create_semantic_error_rsp
    };
    static size_t (* const nfcee_oid[NUMBER_OF_NCI_CMDS])
      (const union nci_packet*, struct nfc_device*, union nci_packet*) = {
        [NCI_OID_NFCEE_DISCOVER_CMD] = create_semantic_error_rsp,
        [NCI_OID_NFCEE_MODE_SET_CMD] = create_semantic_error_rsp
    };
    static size_t (* const prop_oid[NUMBER_OF_NCI_CMDS])
      (const union nci_packet*, struct nfc_device*, union nci_packet*) = {
        [NCI_OID_BCM2079x_GET_BUILD_INFO_CMD] =
          idle_process_oid_bcm2079x_get_build_info_cmd,
        [NCI_OID_BCM2079x_HCI_NETWK_CMD] = create_semantic_error_rsp,
        [NCI_OID_BCM2079x_SET_FWFSM_CMD] = create_semantic_error_rsp,
        [NCI_OID_BCM2079x_GET_PATCH_VERSION_CMD] =
          idle_process_oid_bcm2079x_get_patch_version_cmd
    };

    size_t (*process)(const union nci_packet*, struct nfc_device*,
                      union nci_packet* rsp);

    assert(cmd);

    NFC_D("gid=0x%x oid=0x%x", cmd->control.gid, cmd->control.oid);

    if (cmd->control.gid == NCI_GID_CORE) {
        process = core_oid[cmd->control.oid];
    } else if (cmd->control.gid == NCI_GID_RF) {
        process = rf_oid[cmd->control.oid];
    } else if (cmd->control.gid == NCI_GID_NFCEE) {
        process = nfcee_oid[cmd->control.oid];
    } else if (cmd->control.gid == NCI_GID_PROP) {
        process = prop_oid[cmd->control.oid];
    } else {
        process = NULL;
    }

    assert(process);
    if (!process)
        return 0; /* [NCI] SEC 3.2.2, ignore unknown commands */

    return process(cmd, nfc, rsp);
}

/* RESET state */

static size_t
reset_process_oid_core_init_cmd(const union nci_packet* cmd,
                                struct nfc_device* nfc,
                                union nci_packet* rsp)
{
    struct nci_core_init_rsp* payload;
    uint8_t i;

    assert(nfc);
    assert(rsp);

    nfc->state = NFC_FSM_STATE_INITIALIZED;

    payload = (struct nci_core_init_rsp*)rsp->control.payload;
    payload->status = NCI_STATUS_OK;
    payload->features = cpu_to_le32(0x0);
    payload->nrfs = NUMBER_OF_SUPPORTED_NCI_RF_INTERFACES;
    payload->nconns = 0;
    payload->maxrtabsize = cpu_to_le16(0x0);
    payload->payloadsize = 255;
    payload->maxlparamsize = cpu_to_le16(0x0);
    payload->vendor = 0x0;
    payload->device = cpu_to_le32(0x0);

    for (i = 0; i < payload->nrfs; i++) {
        payload->rf[i] = nfc->rf[i].iface;
    }

    return create_control_rsp(rsp, NCI_PBF_END, cmd->control.gid,
                              cmd->control.oid, sizeof(*payload));
}

static size_t
reset_process_cmd(const union nci_packet* cmd, struct nfc_device* nfc,
                  union nci_packet* rsp)
{
    static size_t (* const core_oid[NUMBER_OF_NCI_CMDS])
      (const union nci_packet*, struct nfc_device*, union nci_packet*) = {
        [NCI_OID_CORE_RESET_CMD] = create_semantic_error_rsp,
        [NCI_OID_CORE_INIT_CMD] = reset_process_oid_core_init_cmd,
        [NCI_OID_CORE_SET_CONFIG_CMD] = create_semantic_error_rsp,
        [NCI_OID_CORE_GET_CONFIG_CMD] = create_semantic_error_rsp,
        [NCI_OID_CORE_CONN_CREATE_CMD] = create_semantic_error_rsp,
        [NCI_OID_CORE_CONN_CLOSE_CMD] = create_semantic_error_rsp
    };
    static size_t (* const rf_oid[NUMBER_OF_NCI_CMDS])
      (const union nci_packet*, struct nfc_device*, union nci_packet*) = {
        [NCI_OID_RF_DISCOVER_MAP_CMD] = create_semantic_error_rsp,
        [NCI_OID_RF_SET_LISTEN_MODE_ROUTING_CMD] = create_semantic_error_rsp,
        [NCI_OID_RF_GET_LISTEN_MODE_ROUTING_CMD] = create_semantic_error_rsp,
        [NCI_OID_RF_DISCOVER_CMD] = create_semantic_error_rsp,
        [NCI_OID_RF_DISCOVER_SELECT_CMD] = create_semantic_error_rsp,
        [NCI_OID_RF_DEACTIVATED_CMD] = create_semantic_error_rsp,
        [NCI_OID_RF_T3T_POLLING_CMD] = create_semantic_error_rsp,
        [NCI_OID_RF_PARAMETER_UPDATE_CMD] = create_semantic_error_rsp
    };
    static size_t (* const nfcee_oid[NUMBER_OF_NCI_CMDS])
      (const union nci_packet*, struct nfc_device*, union nci_packet*) = {
        [NCI_OID_NFCEE_DISCOVER_CMD] = create_semantic_error_rsp,
        [NCI_OID_NFCEE_MODE_SET_CMD] = create_semantic_error_rsp
    };
    static size_t (* const prop_oid[NUMBER_OF_NCI_CMDS])
      (const union nci_packet*, struct nfc_device*, union nci_packet*) = {
        [NCI_OID_BCM2079x_GET_BUILD_INFO_CMD] = create_semantic_error_rsp,
        [NCI_OID_BCM2079x_HCI_NETWK_CMD] = create_semantic_error_rsp,
        [NCI_OID_BCM2079x_SET_FWFSM_CMD] = create_semantic_error_rsp,
        [NCI_OID_BCM2079x_GET_PATCH_VERSION_CMD] = create_semantic_error_rsp
    };

    size_t (*process)(const union nci_packet*, struct nfc_device*,
                      union nci_packet* rsp);

    assert(cmd);

    NFC_D("gid=0x%x oid=0x%x", cmd->control.gid, cmd->control.oid);

    if (cmd->control.gid == NCI_GID_CORE) {
        process = core_oid[cmd->control.oid];
    } else if (cmd->control.gid == NCI_GID_RF) {
        process = rf_oid[cmd->control.oid];
    } else if (cmd->control.gid == NCI_GID_NFCEE) {
        process = nfcee_oid[cmd->control.oid];
    } else if (cmd->control.gid == NCI_GID_PROP) {
        process = prop_oid[cmd->control.oid];
    } else {
        process = NULL;
    }

    assert(process);
    if (!process)
        return 0; /* [NCI] SEC 3.2.2, ignore unknown commands */

    return process(cmd, nfc, rsp);
}

/* INITIALIZED state */

static size_t
init_process_oid_core_reset_cmd(const union nci_packet* cmd,
                                struct nfc_device* nfc,
                                union nci_packet* rsp)
{
    assert(cmd);
    assert(nfc);
    assert(rsp);

    nfc->state = NFC_FSM_STATE_RESET;

    if (cmd->control.payload[0]) {
        nfc->rf_state = NFC_RFST_IDLE;
    }

    rsp->control.payload[0] = NCI_STATUS_OK;
    rsp->control.payload[1] = NCI_VERSION_1_0;
    rsp->control.payload[2] = cmd->control.payload[0];

    return create_control_rsp(rsp, NCI_PBF_END, cmd->control.gid,
                              cmd->control.oid, 3);
}

static size_t
init_process_oid_core_set_config_cmd(const union nci_packet* cmd,
                                     struct nfc_device* nfc,
                                     union nci_packet* rsp)
{
    const struct nci_core_set_config_cmd *payload;
    unsigned char i, off;

    payload = (struct nci_core_set_config_cmd*)cmd->control.payload;

    NFC_D("number of params=%d", payload->nparams);

    for (i = 0, off = 0; i < payload->nparams; ++i) {
        const struct nci_core_config_field* field =
            (const struct nci_core_config_field*)(payload->param+off);

        NFC_D("  param%d: id=0x%x, len=%d", i, field->id, field->len);

        if (field->len) {
            nfc_nci_device_set(nfc, field->id, field->len, field->val);
        } else {
            nfc_nci_device_set(nfc, field->id, config_id_value[field->id][0],
                               config_id_default[field->id]);
        }

        off += 2 + field->len;
    }

    rsp->control.payload[0] = NCI_STATUS_OK;
    rsp->control.payload[1] = 0;

    return create_control_rsp(rsp, NCI_PBF_END, cmd->control.gid,
                                                cmd->control.oid, 2);
}

static size_t
init_process_oid_rf_discover_map_cmd(const union nci_packet* cmd,
                                     struct nfc_device* nfc,
                                     union nci_packet* rsp)
{
    const struct nci_rf_discover_map_cmd *payload;
    unsigned char i;

    payload = (struct nci_rf_discover_map_cmd*)cmd->control.payload;

    NFC_D("number of RF mappings=%d", payload->nmappings);

    for (i = 0; i < payload->nmappings; ++i) {
        const struct nci_rf_discover_mapping* mapping = payload->mapping+i;

        NFC_D("  RF mapping %d: rfproto=0x%x, mode=%d, rfinterface=%d",
              i, mapping->proto, mapping->mode, mapping->iface);
    }

    rsp->control.payload[0] = NCI_STATUS_OK;

    return create_control_rsp(rsp, NCI_PBF_END, cmd->control.gid,
                                                cmd->control.oid, 1);
}

static size_t
init_process_oid_rf_discover_cmd(const union nci_packet* cmd,
                                 struct nfc_device* nfc,
                                 union nci_packet* rsp)
{
    enum nfc_rfst rfst;
    const struct nci_rf_discover_cmd *payload;
    unsigned char i;

    rfst = nfc_rf_state_transition(&nfc->rf_state, NFC_RFST_IDLE_BIT,
                                   NFC_RFST_DISCOVERY);
    assert(rfst != NUMBER_OF_NFC_RFSTS);

    payload = (struct nci_rf_discover_cmd*)cmd->control.payload;

    NFC_D("number of discovery configs=%d", payload->nconfigs);

    for (i = 0; i < payload->nconfigs; ++i) {
        const struct nci_rf_discover_config* config = payload->config+i;

        NFC_D("  RF discovery config %d: rfmode=%x, freq=%x",
              i, config->mode, config->freq);
    }

    rsp->control.payload[0] = NCI_STATUS_OK;

    return create_control_rsp(rsp, NCI_PBF_END, cmd->control.gid,
                                                cmd->control.oid, 1);
}

static size_t
init_process_oid_rf_discover_select_cmd(const union nci_packet* cmd,
                                        struct nfc_device* nfc,
                                        union nci_packet* rsp)
{
    const struct nci_rf_discover_select_cmd *payload;
    struct nfc_re* re;
    enum nfc_rfst rfst;

    /* Verify payload */

    payload = (struct nci_rf_discover_select_cmd*)cmd->control.payload;

    if (!payload->id || payload->id == 255) {
        NFC_D("invalid payload id %d", payload->id);
        goto status_rejected;
    }

    re = nfc_get_re_by_id(payload->id);

    if (!re) {
        NFC_D("couldn't find payload id %d", payload->id);
        goto status_rejected;
    }

    if (payload->rfproto != re->rfproto) {
        NFC_D("invalid RF protocol %d, expected %d", payload->rfproto, re->rfproto);
        goto status_rejected;
    }

    /* make RF active */
    nfc->active_rf = nfc_find_rf_by_rf_interface(nfc, payload->iface);
    if (!nfc->active_rf) {
        goto status_rejected;
    }

    /* RF state transition */
    rfst = nfc_rf_state_transition(&nfc->rf_state, NFC_RFST_W4_HOST_SELECT_BIT,
                                   NFC_RFST_W4_HOST_SELECT);
    assert(rfst != NUMBER_OF_NFC_RFSTS);

    /* make RE active */
    nfc->active_re = re;

    return create_control_status_rsp(rsp, cmd->control.gid,
                                     cmd->control.oid, NCI_STATUS_OK);

status_rejected:
    return create_control_status_rsp(rsp, cmd->control.gid,
                                     cmd->control.oid, NCI_STATUS_REJECTED);
}

static size_t
init_process_oid_rf_deactivate_cmd(const union nci_packet* cmd,
                                   struct nfc_device* nfc,
                                   union nci_packet* rsp)
{
    const struct nci_rf_deactivate_cmd *payload;
    unsigned long bits;
    enum nfc_rfst rfst;
    size_t i;

    payload = (struct nci_rf_deactivate_cmd*)cmd->control.payload;

    /* RF state transition */

    switch (payload->type) {
        case NCI_RF_DEACT_IDLE_MODE:
            bits = NFC_RFST_DISCOVERY_BIT|NFC_RFST_W4_ALL_DISCOVERIES_BIT|
                   NFC_RFST_W4_HOST_SELECT_BIT|NFC_RFST_POLL_ACTIVE_BIT|
                   NFC_RFST_LISTEN_ACTIVE_BIT|NFC_RFST_LISTEN_SLEEP_BIT;
            rfst = NFC_RFST_IDLE;
            break;
        case NCI_RF_DEACT_SLEEP_MODE:
            /* same transitions, fall through */
        case NCI_RF_DEACT_SLEEP_AF_MODE:
            bits = NFC_RFST_POLL_ACTIVE_BIT|NFC_RFST_LISTEN_ACTIVE_BIT;
            if (nfc->rf_state == NFC_RFST_POLL_ACTIVE) {
                rfst = NFC_RFST_W4_HOST_SELECT;
            } else if (nfc->rf_state == NFC_RFST_LISTEN_ACTIVE) {
                rfst = NFC_RFST_LISTEN_SLEEP;
            } else {
                assert(0);
            }
            break;
        case NCI_RF_DEACT_DISCOVERY:
            bits = NFC_RFST_POLL_ACTIVE_BIT|NFC_RFST_LISTEN_ACTIVE_BIT;
            rfst = NFC_RFST_DISCOVERY;
            break;
        default:
            bits = 0;
            rfst = NUMBER_OF_NFC_RFSTS;
    }

    rfst = nfc_rf_state_transition(&nfc->rf_state, bits, rfst);
    assert(rfst != NUMBER_OF_NFC_RFSTS);

    /* reset state */

    nfc->active_re = NULL;
    nfc->active_rf = NULL;

    for (i = 0; i < sizeof(nfc_res)/sizeof(nfc_res[0]); ++i) {
        nfc_res[i].id = 0;
    }

    return create_control_status_rsp(rsp, cmd->control.gid,
                                     cmd->control.oid, NCI_STATUS_OK);
}

static size_t
init_process_oid_nfcee_discover_cmd(const union nci_packet* cmd,
                                    struct nfc_device* nfc,
                                    union nci_packet* rsp)
{
    struct nci_nfcee_discovery_rsp* payload =
        (struct nci_nfcee_discovery_rsp*)rsp->control.payload;

    payload->status = NCI_STATUS_OK;
    payload->nnfcees = 0; /* no NFCEEs for now */

    return create_control_rsp(rsp, NCI_PBF_END, cmd->control.gid,
                              cmd->control.oid, sizeof(*payload));
}

static size_t
init_process_oid_bcm2079x_get_build_info_cmd(const union nci_packet* cmd,
                                             struct nfc_device* nfc,
                                             union nci_packet* rsp)
{
    // status code
    rsp->control.payload[0] = NCI_STATUS_OK;
    // unknown
    rsp->control.payload[1] = 0x4d;
    rsp->control.payload[2] = 0x61;
    rsp->control.payload[3] = 0x72;
    rsp->control.payload[4] = 0x20;
    rsp->control.payload[5] = 0x31;
    rsp->control.payload[6] = 0x37;
    rsp->control.payload[7] = 0x20;
    rsp->control.payload[8] = 0x32;
    rsp->control.payload[9] = 0x30;
    rsp->control.payload[10] = 0x31;
    rsp->control.payload[11] = 0x32;
    rsp->control.payload[12] = 0x31;
    rsp->control.payload[13] = 0x30;
    rsp->control.payload[14] = 0x3a;
    rsp->control.payload[15] = 0x35;
    rsp->control.payload[16] = 0x34;
    rsp->control.payload[17] = 0x3a;
    rsp->control.payload[18] = 0x31;
    rsp->control.payload[19] = 0x35;
    rsp->control.payload[20] = 0x1;
    rsp->control.payload[21] = 0x3;
    rsp->control.payload[22] = 0xa;
    rsp->control.payload[23] = 0x44;
    // HW id
    rsp->control.payload[24] = 0x3;
    rsp->control.payload[25] = 0x1b;
    rsp->control.payload[26] = 0x79;
    rsp->control.payload[27] = 0x20;
    // chip-version length
    rsp->control.payload[28] = 7;
    // chip version
    rsp->control.payload[29] = '2';
    rsp->control.payload[30] = '0';
    rsp->control.payload[31] = '7';
    rsp->control.payload[32] = '9';
    rsp->control.payload[33] = '1';
    rsp->control.payload[34] = 'B';
    rsp->control.payload[35] = '3';

    return create_control_rsp(rsp, NCI_PBF_END, cmd->control.gid,
                              cmd->control.oid, 36);
}

static size_t
init_process_oid_bcm2079x_hci_netwk_cmd(const union nci_packet* cmd,
                                        struct nfc_device* nfc,
                                        union nci_packet* rsp)
{
    return create_control_status_rsp(rsp, cmd->control.gid,
                                     cmd->control.oid, NCI_STATUS_OK);
}

static size_t
init_process_oid_bcm2079x_set_fwfsm_cmd(const union nci_packet* cmd,
                                        struct nfc_device* nfc,
                                        union nci_packet* rsp)
{
    return create_control_status_rsp(rsp, cmd->control.gid,
                                     cmd->control.oid, NCI_STATUS_OK);
}

static size_t
init_process_oid_bcm2079x_get_patch_version_cmd(const union nci_packet* cmd,
                                                struct nfc_device* nfc,
                                                union nci_packet* rsp)
{
    struct nci_bcm2079x_get_patch_version_rsp* payload;

    payload =
        (struct nci_bcm2079x_get_patch_version_rsp*)rsp->control.payload;

    payload->projectid = cpu_to_le16(0x100);
    payload->rfu = 0;
    payload->chipver = 7;
    payload->patchinfover[0] = '2';
    payload->patchinfover[1] = '0';
    payload->patchinfover[2] = '7';
    payload->patchinfover[3] = '9';
    payload->patchinfover[4] = '1';
    payload->patchinfover[5] = 'B';
    payload->patchinfover[6] = '3';
    memset(payload->patchinfover+payload->chipver, 0,
           sizeof(payload->patchinfover)-payload->chipver);
    payload->major = cpu_to_le16(0x93);
    payload->minor = cpu_to_le16(0x9d);
    payload->maxsize = cpu_to_le16(256);
    payload->patchmaxsize = cpu_to_le16(256);
    payload->lpmsize = cpu_to_le16(0);
    payload->fpmsize = cpu_to_le16(0x1b52);
    payload->lpmbadcrc = 0;
    payload->fpmbadcrc = 0;
    payload->nvmtype = NCI_BCM2079x_SPD_NVM_EEPROM;

    return create_control_rsp(rsp, NCI_PBF_END, cmd->control.gid,
                              cmd->control.oid, sizeof(*payload));
}

static size_t
init_process_cmd(const union nci_packet* cmd, struct nfc_device* nfc,
                 union nci_packet* rsp)
{
    /* fill NULL pointers if necessary */
    static size_t (* const core_oid[NUMBER_OF_NCI_CMDS])
      (const union nci_packet*, struct nfc_device*, union nci_packet*) = {
        [NCI_OID_CORE_RESET_CMD] = init_process_oid_core_reset_cmd,
        [NCI_OID_CORE_INIT_CMD] = create_semantic_error_rsp,
        [NCI_OID_CORE_SET_CONFIG_CMD] = init_process_oid_core_set_config_cmd,
        [NCI_OID_CORE_GET_CONFIG_CMD] = NULL,
        [NCI_OID_CORE_CONN_CREATE_CMD] = NULL,
        [NCI_OID_CORE_CONN_CLOSE_CMD] = NULL
    };
    static size_t (* const rf_oid[NUMBER_OF_NCI_CMDS])
      (const union nci_packet*, struct nfc_device*, union nci_packet*) = {
        [NCI_OID_RF_DISCOVER_MAP_CMD] = init_process_oid_rf_discover_map_cmd,
        [NCI_OID_RF_SET_LISTEN_MODE_ROUTING_CMD] = NULL,
        [NCI_OID_RF_GET_LISTEN_MODE_ROUTING_CMD] = NULL,
        [NCI_OID_RF_DISCOVER_CMD] = init_process_oid_rf_discover_cmd,
        [NCI_OID_RF_DISCOVER_SELECT_CMD] = init_process_oid_rf_discover_select_cmd,
        [NCI_OID_RF_DEACTIVATED_CMD] = init_process_oid_rf_deactivate_cmd,
        [NCI_OID_RF_T3T_POLLING_CMD] = NULL,
        [NCI_OID_RF_PARAMETER_UPDATE_CMD] = NULL
    };
    static size_t (* const nfcee_oid[NUMBER_OF_NCI_CMDS])
      (const union nci_packet*, struct nfc_device*, union nci_packet*) = {
        [NCI_OID_NFCEE_DISCOVER_CMD] = init_process_oid_nfcee_discover_cmd,
        [NCI_OID_NFCEE_MODE_SET_CMD] = NULL
    };
    static size_t (* const prop_oid[NUMBER_OF_NCI_CMDS])
      (const union nci_packet*, struct nfc_device*, union nci_packet*) = {
        [NCI_OID_BCM2079x_GET_BUILD_INFO_CMD] = init_process_oid_bcm2079x_get_build_info_cmd,
        [NCI_OID_BCM2079x_HCI_NETWK_CMD] = init_process_oid_bcm2079x_hci_netwk_cmd,
        [NCI_OID_BCM2079x_SET_FWFSM_CMD] = init_process_oid_bcm2079x_set_fwfsm_cmd,
        [NCI_OID_BCM2079x_GET_PATCH_VERSION_CMD] = init_process_oid_bcm2079x_get_patch_version_cmd
    };

    size_t (*process)(const union nci_packet*, struct nfc_device*,
                      union nci_packet* rsp);

    assert(cmd);

    NFC_D("gid=0x%x oid=0x%x", cmd->control.gid, cmd->control.oid);

    if (cmd->control.gid == NCI_GID_CORE) {
        process = core_oid[cmd->control.oid];
    } else if (cmd->control.gid == NCI_GID_RF) {
        process = rf_oid[cmd->control.oid];
    } else if (cmd->control.gid == NCI_GID_NFCEE) {
        process = nfcee_oid[cmd->control.oid];
    } else if (cmd->control.gid == NCI_GID_PROP) {
        process = prop_oid[cmd->control.oid];
    } else {
        process = NULL;
    }

    assert(process);
    if (!process)
        return 0; /* [NCI] SEC 3.2.2, ignore unknown commands */

    return process(cmd, nfc, rsp);
}

static size_t
process_nci_cmd(const union nci_packet* cmd, struct nfc_device* nfc,
                union nci_packet* rsp)
{
    static size_t (* const process[NUMBER_OF_NFC_FSM_STATES])
      (const union nci_packet*, struct nfc_device*, union nci_packet*) = {
        [NFC_FSM_STATE_IDLE] = idle_process_cmd,
        [NFC_FSM_STATE_RESET] = reset_process_cmd,
        [NFC_FSM_STATE_INITIALIZED] = init_process_cmd
    };

    NFC_D("NCI mt=%d pbf=%d unused=%d; NFC state=%d",
          cmd->common.mt, cmd->common.pbf, cmd->common.unused, nfc->state);

    assert(nfc->state < NUMBER_OF_NFC_FSM_STATES);
    assert(process[nfc->state]);

    return process[nfc->state](cmd, nfc, rsp);
}

/*
 * Message handling
 */

size_t
nfc_process_nci_msg(const union nci_packet* pkt, struct nfc_device* nfc,
                    union nci_packet* rsp)
{
    assert(pkt);

    if (pkt->common.mt == NCI_MT_DTA) {
        return process_nci_dta(pkt, nfc, rsp);
    } else if (pkt->common.mt == NCI_MT_CMD) {
        return process_nci_cmd(pkt, nfc, rsp);
    } else {
        return 0; /* [NCI], Sec 3.2.2; ignore anything but commands */
    }
}

/*
 * Notifications
 */

size_t
nfc_create_nci_ntf(union nci_packet* ntf, enum nci_pbf pbf,
                   enum nci_gid gid, enum nci_oid oid, unsigned char l)
{
    assert(ntf);

    NFC_D("NCI notification pbf=%d gid=%d oid=%d", pbf, gid, oid);

    ntf->control.mt = NCI_MT_NTF;
    ntf->control.pbf = pbf;
    ntf->control.gid = gid;
    ntf->control.rfu = 0;
    ntf->control.oid = oid;
    ntf->control.l = l;

    return 3 + l;
}

size_t
nfc_create_rf_discovery_ntf(struct nfc_re* re,
                            enum nci_notification_type type,
                            struct nfc_device* nfc,
                            union nci_packet* ntf)
{
    struct nci_rf_discover_ntf* payload;
    unsigned long bits;
    enum nfc_rfst rfst;

    assert(re);
    assert(type < NUMBER_OF_NCI_NOTIFICATION_TYPES);

    if (re->id) {
        return 0; /* RE already discovered */
    }

    ntf->control.mt = NCI_MT_NTF;
    ntf->control.pbf = NCI_PBF_END;
    ntf->control.gid = NCI_GID_RF;
    ntf->control.oid = NCI_OID_RF_DISCOVER_NTF;

    re->id = nfc_device_incr_id(nfc);

    payload = (struct nci_rf_discover_ntf*)ntf->control.payload;
    payload->id = re->id;
    payload->rfproto = re->rfproto;
    payload->mode = nfc->rf[0].mode;
    payload->nparams = 0;

    switch (nfc->rf_state) {
        case NFC_RFST_DISCOVERY:
            assert(type == NCI_MORE_NOTIFICATIONS);
            payload->end[payload->nparams] = NCI_MORE_NOTIFICATIONS;
            bits = NFC_RFST_DISCOVERY_BIT;
            rfst = NFC_RFST_W4_ALL_DISCOVERIES;
            break;
        case NFC_RFST_W4_ALL_DISCOVERIES:
            payload->end[payload->nparams] = type;
            bits = NFC_RFST_W4_ALL_DISCOVERIES_BIT;
            if (type == NCI_MORE_NOTIFICATIONS) {
                rfst = NFC_RFST_W4_ALL_DISCOVERIES;
            } else {
                rfst = NFC_RFST_W4_HOST_SELECT;
            }
            break;
        default:
            bits = 0;
            rfst = 0;
            break;
    }

    rfst = nfc_rf_state_transition(&nfc->rf_state, bits, rfst);
    assert(rfst != NUMBER_OF_NFC_RFSTS);

    return nfc_create_nci_ntf(ntf, NCI_PBF_END, NCI_GID_RF,
                              NCI_OID_RF_DISCOVER_NTF,
                              sizeof(*payload)+payload->nparams+1);
}

size_t
nfc_create_rf_intf_activated_ntf(struct nfc_re* re,
                                 struct nfc_device* nfc,
                                 union nci_packet* ntf)
{
    struct nci_rf_intf_activated_ntf* payload;
    struct nci_rf_intf_activated_ntf_tail* tail;
    unsigned long bits;
    enum nfc_rfst rfst;

    assert(re);
    assert(ntf);
    assert(nfc->active_rf);

    /* create notification */

    ntf->control.mt = NCI_MT_NTF;
    ntf->control.pbf = NCI_PBF_END;
    ntf->control.gid = NCI_GID_RF;
    ntf->control.oid = NCI_OID_RF_INTF_ACTIVATED_NTF;

    payload = (struct nci_rf_intf_activated_ntf*)ntf->control.payload;

    if (!re->id) {
        re->id = nfc_device_incr_id(nfc);
    }

    payload->id = re->id;
    payload->iface = nfc->active_rf->iface;
    payload->rfproto = re->rfproto;
    payload->actmode = nfc->active_rf->mode;
    payload->maxpayload = 255;
    payload->ncredits = 0xff; /* disable flow control */
    payload->nparams = 0;

    tail = (struct nci_rf_intf_activated_ntf_tail*)
        (payload->param + payload->nparams);

    tail->mode = re->mode;
    tail->tx_bitrate = NCI_NFC_BIT_RATE_106;
    tail->rx_bitrate = NCI_NFC_BIT_RATE_106;
    tail->nactparams =
        nfc_re_create_rf_intf_activated_ntf_act(re, tail->actparam);

    /* RF state transition */

    switch (payload->actmode) { /* maybe tail->mode? */
        case NCI_RF_NFC_A_PASSIVE_POLL_MODE:
            /* fall through */
        case NCI_RF_NFC_B_PASSIVE_POLL_MODE:
            /* fall through */
        case NCI_RF_NFC_F_PASSIVE_POLL_MODE:
            bits = NFC_RFST_DISCOVERY_BIT|NFC_RFST_W4_HOST_SELECT_BIT;
            rfst = NFC_RFST_POLL_ACTIVE;
            break;
        case NCI_RF_NFC_A_PASSIVE_LISTEN_MODE:
            /* fall through */
        case NCI_RF_NFC_B_PASSIVE_LISTEN_MODE:
            /* fall through */
        case NCI_RF_NFC_F_PASSIVE_LISTEN_MODE:
            bits = NFC_RFST_DISCOVERY_BIT|NFC_RFST_LISTEN_SLEEP_BIT;
            rfst = NFC_RFST_LISTEN_ACTIVE;
            break;
        default:
            assert(0);
            break;
    }

    rfst = nfc_rf_state_transition(&nfc->rf_state, bits, rfst);
    assert(rfst != NUMBER_OF_NFC_RFSTS);

    /* update NFC */

    if (!nfc->active_re) {
        nfc->active_re = re;
    }
    assert(nfc->active_re == re);

    return nfc_create_nci_ntf(ntf, NCI_PBF_END, NCI_GID_RF,
                              NCI_OID_RF_INTF_ACTIVATED_NTF,
                              sizeof(*payload)+payload->nparams+
                              sizeof(*tail)+tail->nactparams);
}

size_t
nfc_create_rf_field_info_ntf(struct nfc_device* nfc,
                             union nci_packet* ntf)
{
    struct nci_rf_field_info_ntf* payload;

    assert(ntf);

    payload = (struct nci_rf_field_info_ntf*)ntf->control.payload;
    payload->status = 0; /* TODO: may depend on active RE's mode */

    return nfc_create_nci_ntf(ntf, NCI_PBF_END, NCI_GID_RF,
                              NCI_OID_RF_FIELD_INFO_NTF, sizeof(*payload));
}
