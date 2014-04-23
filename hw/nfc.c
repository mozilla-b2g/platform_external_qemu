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
#include "nfc.h"

void
nfc_device_init(struct nfc_device* nfc)
{
    assert(nfc);

    nfc->state = NFC_FSM_STATE_IDLE;
    nfc->rf_state = NFC_RFST_IDLE;
    nfc_rf_init(&nfc->rf[0], NCI_RF_INTERFACE_NFC_DEP);
    nfc_rf_init(&nfc->rf[1], NCI_RF_INTERFACE_FRAME);
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
nfc_find_rf_by_rf_interface(struct nfc_device* nfc, enum nci_rf_interface iface)
{
    size_t i;

    assert(nfc);

    for (i = 0; i < ARRAY_SIZE(nfc->rf); i++) {
        if (iface == nfc->rf[i].iface) {
            return &nfc->rf[i];
        }
    }
    return NULL;
}
