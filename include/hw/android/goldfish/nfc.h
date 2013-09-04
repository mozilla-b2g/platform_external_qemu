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

#ifndef goldfish_nfc_h
#define goldfish_nfc_h

#include <stddef.h>

struct nfc_device;
union nci_packet;

void
goldfish_nfc_send_dta(size_t (*create)(void*, struct nfc_device*,
                                       union nci_packet*), void* data);

void
goldfish_nfc_send_ntf(size_t (*create)(void*, struct nfc_device*,
                                       union nci_packet*), void* data);

#endif
