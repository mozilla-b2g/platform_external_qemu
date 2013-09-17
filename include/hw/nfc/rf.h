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

#ifndef nfc_rf_h
#define nfc_rf_h

/* [NCI]; Figure 10 */
enum nfc_rfst {
    NFC_RFST_IDLE = 0,
    NFC_RFST_DISCOVERY,
    NFC_RFST_W4_ALL_DISCOVERIES,
    NFC_RFST_W4_HOST_SELECT,
    NFC_RFST_POLL_ACTIVE,
    NFC_RFST_LISTEN_ACTIVE,
    NFC_RFST_LISTEN_SLEEP,
    NUMBER_OF_NFC_RFSTS
};

enum nfc_rfst_bit {
    NFC_RFST_IDLE_BIT = 1<<NFC_RFST_IDLE,
    NFC_RFST_DISCOVERY_BIT = 1<<NFC_RFST_DISCOVERY,
    NFC_RFST_W4_ALL_DISCOVERIES_BIT = 1<<NFC_RFST_W4_ALL_DISCOVERIES,
    NFC_RFST_W4_HOST_SELECT_BIT = 1<<NFC_RFST_W4_HOST_SELECT,
    NFC_RFST_POLL_ACTIVE_BIT = 1<<NFC_RFST_POLL_ACTIVE,
    NFC_RFST_LISTEN_ACTIVE_BIT = 1<<NFC_RFST_LISTEN_ACTIVE,
    NFC_RFST_LISTEN_SLEEP_BIT = 1<<NFC_RFST_LISTEN_SLEEP,
};

/* [NCI]; Table 96 */
enum nci_rf_tech_mode {
    /* active modes not yet supported */
    NCI_RF_NFC_A_PASSIVE_POLL_MODE = 0x00,
    NCI_RF_NFC_B_PASSIVE_POLL_MODE = 0x01,
    NCI_RF_NFC_F_PASSIVE_POLL_MODE = 0x02,
    NCI_RF_NFC_A_PASSIVE_LISTEN_MODE = 0x80,
    NCI_RF_NFC_B_PASSIVE_LISTEN_MODE = 0x81,
    NCI_RF_NFC_F_PASSIVE_LISTEN_MODE = 0x82
};

/* [NCI]; Table 98 */
enum nci_rf_protocol {
    NCI_RF_PROTOCOL_UNDETERMINED = 0x00,
    NCI_RF_PROTOCOL_T1T = 0x01,
    NCI_RF_PROTOCOL_T2T = 0x02,
    NCI_RF_PROTOCOL_T3T = 0x03,
    NCI_RF_PROTOCOL_ISO_DEP = 0x04,
    NCI_RF_PROTOCOL_NFC_DEP = 0x05
};

/* [NCI]; Table 99 */
enum nci_rf_interface {
    NCI_RF_INTERFACE_NFCEE = 0x00,
    NCI_RF_INTERFACE_FRAME = 0x01,
    NCI_RF_INTERFACE_ISO_DEP = 0x02,
    NCI_RF_INTERFACE_NFC_DEP = 0x03
};

struct nfc_rf {
    enum nfc_rfst state;
    enum nci_rf_interface iface;
    enum nci_rf_tech_mode mode;
};

void
nfc_rf_init(struct nfc_rf* rf, enum nci_rf_interface iface);

enum nfc_rfst
nfc_rf_state_transition(struct nfc_rf* rf, unsigned long bits,
                        enum nfc_rfst state);

#endif
