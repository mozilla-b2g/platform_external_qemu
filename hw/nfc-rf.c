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
#include "nfc-debug.h"
#include "nfc-rf.h"

void
nfc_rf_init(struct nfc_rf* rf, enum nci_rf_interface iface)
{
    assert(rf);

    rf->state = NFC_RFST_IDLE;
    rf->iface = iface;
    rf->mode = NCI_RF_NFC_F_PASSIVE_POLL_MODE;
}

enum nfc_rfst
nfc_rf_state_transition(struct nfc_rf* rf, unsigned long bits,
                        enum nfc_rfst state)
{
    enum nfc_rfst rfst;

    assert(rf);

    rfst = rf->state;

    if (!((1<<rfst) & bits)) {
        return NUMBER_OF_NFC_RFSTS;
    }

    NFC_D("rf->state from %d to %d\n", rf->state, state);

    rf->state = state;

    return rfst;
}
