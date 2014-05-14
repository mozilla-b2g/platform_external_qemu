/* Copyright (C) 2007-2008 The Android Open Source Project
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
#ifndef _android_supplementary_service_h
#define _android_supplementary_service_h

typedef struct ASupplementaryServiceRec_*    ASupplementaryService;

extern ASupplementaryService  asupplementary_create( int base_port, int instance_id );
extern void                   asupplementary_destroy( ASupplementaryService sim );

#endif /* _android_supplementary_service_h */
