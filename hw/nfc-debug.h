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

#ifndef hw_nfc_debug_h
#define hw_nfc_debug_h

#include <android/utils/debug.h>
#include <stdio.h>

#if DEBUG
#  define  NFC_D(...)   VERBOSE_PRINT(nfc, __VA_ARGS__ )
#else
#  define  NFC_D(...)   ((void)0)
#endif

#endif
