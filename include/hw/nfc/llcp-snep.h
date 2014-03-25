/* Copyright (C) 2014 Mozilla Foundation
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

#include <stddef.h>
#include <stdint.h>

struct llcp_data_link;
struct snep;

size_t
llcp_sap_snep(struct llcp_data_link* dl, const uint8_t* info, size_t len,
              struct snep* rsp);
