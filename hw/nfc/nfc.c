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
#include "qemu-common.h"
#include "hw/nfc/nfc.h"
#include "hw/nfc/nci.h"

void
nfc_device_init(struct nfc_device* nfc)
{
    assert(nfc);

    nfc->state = NFC_FSM_STATE_IDLE;
    nfc->rf_state = NFC_RFST_IDLE;

    nfc_rf_init(&nfc->rf[0], NCI_RF_INTERFACE_NFC_DEP,  NCI_RF_NFC_A_PASSIVE_POLL_MODE);
    nfc_rf_init(&nfc->rf[1], NCI_RF_INTERFACE_NFC_DEP,  NCI_RF_NFC_F_PASSIVE_POLL_MODE);
    nfc_rf_init(&nfc->rf[2], NCI_RF_INTERFACE_FRAME,    NCI_RF_NFC_A_PASSIVE_POLL_MODE);
    nfc_rf_init(&nfc->rf[3], NCI_RF_INTERFACE_FRAME,    NCI_RF_NFC_B_PASSIVE_POLL_MODE);
    nfc_rf_init(&nfc->rf[4], NCI_RF_INTERFACE_FRAME,    NCI_RF_NFC_F_PASSIVE_POLL_MODE);
    nfc_rf_init(&nfc->rf[5], NCI_RF_INTERFACE_ISO_DEP,  NCI_RF_NFC_A_PASSIVE_POLL_MODE);
    nfc_rf_init(&nfc->rf[6], NCI_RF_INTERFACE_ISO_DEP,  NCI_RF_NFC_B_PASSIVE_POLL_MODE);
    nfc_rf_init(&nfc->rf[7], NCI_RF_INTERFACE_ISO_DEP,  NCI_RF_NFC_F_PASSIVE_POLL_MODE);

    nfc->id = 0;
    nfc->active_re = NULL;
    nfc->active_rf = NULL;

    memset(nfc->config_id_value, 0, sizeof(nfc->config_id_value));
}

void
nfc_device_set(struct nfc_device* nfc, size_t off, size_t len,
               const void* value)
{
    assert(off+len <
           sizeof(nfc->config_id_value)/sizeof(nfc->config_id_value[0]));

    memcpy(nfc->config_id_value+off, value, len);
}

void
nfc_device_get(const struct nfc_device* nfc, size_t off, size_t len,
               void* value)
{
    assert(off+len <
           sizeof(nfc->config_id_value)/sizeof(nfc->config_id_value[0]));

    memcpy(value, nfc->config_id_value+off, len);
}

uint8_t
nfc_device_incr_id(struct nfc_device* nfc)
{
    assert(nfc);

    /* don't hand out 0 and 255 */
    nfc->id = (nfc->id%254) + 1;

    return nfc->id;
}

struct nfc_rf*
nfc_find_rf_by_protocol_and_mode(struct nfc_device* nfc,
                                 enum nci_rf_protocol proto,
                                 enum nci_rf_tech_mode mode)
{
    uint8_t i;
    enum nci_rf_interface rf_iface;
    enum nci_rf_tech_mode rf_mode;

    assert(nfc);

    switch (proto) {
        case NCI_RF_PROTOCOL_T1T:
        case NCI_RF_PROTOCOL_T2T:
        case NCI_RF_PROTOCOL_T3T:
            rf_iface = NCI_RF_INTERFACE_FRAME;
            break;
        case NCI_RF_PROTOCOL_ISO_DEP:
            rf_iface = NCI_RF_INTERFACE_ISO_DEP;
            break;
        case NCI_RF_PROTOCOL_NFC_DEP:
            rf_iface = NCI_RF_INTERFACE_NFC_DEP;
            break;
        default:
            assert(0);
            break;
    }

    switch (mode) {
        case NCI_RF_NFC_A_PASSIVE_POLL_MODE:
        case NCI_RF_NFC_A_PASSIVE_LISTEN_MODE:
            rf_mode = NCI_RF_NFC_A_PASSIVE_POLL_MODE;
            break;
        case NCI_RF_NFC_B_PASSIVE_POLL_MODE:
        case NCI_RF_NFC_B_PASSIVE_LISTEN_MODE:
            rf_mode = NCI_RF_NFC_B_PASSIVE_POLL_MODE;
            break;
        case NCI_RF_NFC_F_PASSIVE_POLL_MODE:
        case NCI_RF_NFC_F_PASSIVE_LISTEN_MODE:
            rf_mode = NCI_RF_NFC_F_PASSIVE_POLL_MODE;
            break;
    }

    for (i = 0; i < ARRAY_SIZE(nfc->rf); i++) {
        if (nfc->rf[i].iface == rf_iface && nfc->rf[i].mode == rf_mode) {
            return nfc->rf + i;
        }
    }

    return NULL;
}

void
nfc_delivery_cb_setup(struct nfc_delivery_cb* cb, enum nfc_buf_type type,
                      void* data, ssize_t (*func)(void*, union nci_packet*))
{
    assert(cb);

    cb->type = type;
    cb->data = data;
    cb->func = func;
}
