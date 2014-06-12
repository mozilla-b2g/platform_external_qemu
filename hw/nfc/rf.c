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
#include "hw/nfc/debug.h"
#include "hw/nfc/rf.h"

void
nfc_rf_init(struct nfc_rf* rf, enum nci_rf_interface iface)
{
    assert(rf);

    rf->iface = iface;
    switch (iface) {
        case NCI_RF_INTERFACE_FRAME:
            rf->mode = NCI_RF_NFC_A_PASSIVE_POLL_MODE;
            break;
        case NCI_RF_INTERFACE_NFC_DEP:
            rf->mode = NCI_RF_NFC_F_PASSIVE_POLL_MODE;
            break;
        default:
            assert(0);
            break;
    }
}

int
nfc_set_rf_mode_by_protocol(struct nfc_rf* rf, enum nci_rf_protocol proto)
{
    assert(rf);

    switch(proto) {
        case NCI_RF_PROTOCOL_T1T:
        case NCI_RF_PROTOCOL_T2T:
            rf->mode = NCI_RF_NFC_A_PASSIVE_POLL_MODE;
            break;
        case NCI_RF_PROTOCOL_T3T:
        case NCI_RF_PROTOCOL_NFC_DEP:
            rf->mode = NCI_RF_NFC_F_PASSIVE_POLL_MODE;
            break;
        default:
            return -1;
    }

    return 0;
}

enum nfc_rfst
nfc_rf_state_transition(enum nfc_rfst* rf_state, unsigned long bits,
                        enum nfc_rfst state)
{
    enum nfc_rfst rfst;

    assert(rf_state);

    rfst = *rf_state;

    if (!((1<<rfst) & bits)) {
        return NUMBER_OF_NFC_RFSTS;
    }

    NFC_D("rf_state from %d to %d\n", *rf_state, state);

    *rf_state = state;

    return rfst;
}
