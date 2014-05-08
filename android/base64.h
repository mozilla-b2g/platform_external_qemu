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

#ifndef base64_h
#define base64_h

#include <sys/types.h>

ssize_t
encode_base64(const unsigned char* in, size_t ilen, char* out, size_t olen);

ssize_t
decode_base64(const char* in, size_t ilen, unsigned char* out, size_t olen);

#endif
