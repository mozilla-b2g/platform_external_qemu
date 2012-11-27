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
#include "sim_card.h"
#include <string.h>
#include <assert.h>
#include <stdio.h>

/* set ENABLE_DYNAMIC_RECORDS to 1 to enable dynamic records
 * for now, this is an experimental feature that needs more testing
 */
#define  ENABLE_DYNAMIC_RECORDS  0

#define  A_SIM_PIN_SIZE  4
#define  A_SIM_PUK_SIZE  8

typedef struct ASimCardRec_ {
    ASimStatus  status;
    char        pin[ A_SIM_PIN_SIZE+1 ];
    char        puk[ A_SIM_PUK_SIZE+1 ];
    int         pin_retries;
    int         port;

    char        out_buff[ 256 ];
    int         out_size;

} ASimCardRec;

static ASimCardRec  _s_card[1];

ASimCard
asimcard_create(int port)
{
    ASimCard  card    = _s_card;
    card->status      = A_SIM_STATUS_READY;
    card->pin_retries = 0;
    strncpy( card->pin, "0000", sizeof(card->pin) );
    strncpy( card->puk, "12345678", sizeof(card->puk) );
    card->port = port;
    return card;
}

void
asimcard_destroy( ASimCard  card )
{
    /* nothing really */
    card=card;
}

static __inline__ int
asimcard_ready( ASimCard  card )
{
    return card->status == A_SIM_STATUS_READY;
}

ASimStatus
asimcard_get_status( ASimCard  sim )
{
    return sim->status;
}

void
asimcard_set_status( ASimCard  sim, ASimStatus  status )
{
    sim->status = status;
}

const char*
asimcard_get_pin( ASimCard  sim )
{
    return sim->pin;
}

const char*
asimcard_get_puk( ASimCard  sim )
{
    return sim->puk;
}

void
asimcard_set_pin( ASimCard  sim, const char*  pin )
{
    strncpy( sim->pin, pin, A_SIM_PIN_SIZE );
    sim->pin_retries = 0;
}

void
asimcard_set_puk( ASimCard  sim, const char*  puk )
{
    strncpy( sim->puk, puk, A_SIM_PUK_SIZE );
    sim->pin_retries = 0;
}


int
asimcard_check_pin( ASimCard  sim, const char*  pin )
{
    if (sim->status != A_SIM_STATUS_PIN   &&
        sim->status != A_SIM_STATUS_READY )
        return 0;

    if ( !strcmp( sim->pin, pin ) ) {
        sim->status      = A_SIM_STATUS_READY;
        sim->pin_retries = 0;
        return 1;
    }

    if (sim->status != A_SIM_STATUS_READY) {
        if (++sim->pin_retries == 3)
            sim->status = A_SIM_STATUS_PUK;
    }
    return 0;
}


int
asimcard_check_puk( ASimCard  sim, const char* puk, const char*  pin )
{
    if (sim->status != A_SIM_STATUS_PUK)
        return 0;

    if ( !strcmp( sim->puk, puk ) ) {
        strncpy( sim->puk, puk, A_SIM_PUK_SIZE );
        strncpy( sim->pin, pin, A_SIM_PIN_SIZE );
        sim->status      = A_SIM_STATUS_READY;
        sim->pin_retries = 0;
        return 1;
    }

    if ( ++sim->pin_retries == 6 ) {
        sim->status = A_SIM_STATUS_ABSENT;
    }
    return 0;
}

typedef enum {
    SIM_FILE_DM = 0,
    SIM_FILE_DF,
    SIM_FILE_EF_DEDICATED,
    SIM_FILE_EF_LINEAR,
    SIM_FILE_EF_CYCLIC
} SimFileType;

typedef enum {
    SIM_FILE_READ_ONLY       = (1 << 0),
    SIM_FILE_NEED_PIN = (1 << 1),
} SimFileFlags;

/* descriptor for a known SIM File */
#define  SIM_FILE_HEAD       \
    SimFileType     type;    \
    unsigned short  id;      \
    unsigned short  flags;

typedef struct {
    SIM_FILE_HEAD
} SimFileAnyRec, *SimFileAny;

typedef struct {
    SIM_FILE_HEAD
    cbytes_t   data;
    int        length;
} SimFileEFDedicatedRec, *SimFileEFDedicated;

typedef struct {
    SIM_FILE_HEAD
    byte_t     rec_count;
    byte_t     rec_len;
    cbytes_t   records;
} SimFileEFLinearRec, *SimFileEFLinear;

typedef SimFileEFLinearRec   SimFileEFCyclicRec;
typedef SimFileEFCyclicRec*  SimFileEFCyclic;

typedef union {
    SimFileAnyRec          any;
    SimFileEFDedicatedRec  dedicated;
    SimFileEFLinearRec     linear;
    SimFileEFCyclicRec     cyclic;
} SimFileRec, *SimFile;


#if ENABLE_DYNAMIC_RECORDS
/* convert a SIM File descriptor into an ASCII string,
   assumes 'dst' is NULL or properly sized.
   return the number of chars, or -1 on error */
static int
sim_file_to_hex( SimFile  file, bytes_t  dst )
{
    SimFileType  type   = file->any.type;
    int          result = 0;

    /* see 9.2.1 in TS 51.011 */
    switch (type) {
        case SIM_FILE_EF_DEDICATED:
        case SIM_FILE_EF_LINEAR:
        case SIM_FILE_EF_CYCLIC:
            {
                if (dst) {
                    int  file_size, perm;

                    memcpy(dst, "0000", 4);  /* bytes 1-2 are RFU */
                    dst += 4;

                    /* bytes 3-4 are the file size */
                    if (type == SIM_FILE_EF_DEDICATED)
                        file_size = file->dedicated.length;
                    else
                        file_size = file->linear.rec_count * file->linear.rec_len;

                    gsm_hex_from_short( dst, file_size );
                    dst += 4;

                    /* bytes 5-6 are the file id */
                    gsm_hex_from_short( dst, file->any.id );
                    dst += 4;

                    /* byte 7 is the file type - always EF, i.e. 0x04 */
                    dst[0] = '0';
                    dst[1] = '4';
                    dst   += 2;

                    /* byte 8 is RFU, except bit 7 for cyclic files, which indicates
                       that INCREASE is allowed. Since we don't support this yet... */
                    dst[0] = '0';
                    dst[1] = '0';
                    dst   += 2;

                    /* byte 9-11 are access conditions */
                    if (file->any.flags & SIM_FILE_READ_ONLY) {
                        if (file->any.flags & SIM_FILE_NEED_PIN)
                            perm = 0x1a;
                        else
                            perm = 0x0a;
                    } else {
                        if (file->any.flags & SIM_FILE_NEED_PIN)
                            perm = 0x11;
                        else
                            perm = 0x00;
                    }
                    gsm_hex_from_byte(dst, perm);
                    memcpy( dst+2, "a0aa", 4 );
                    dst += 6;

                    /* byte 12 is file status, we don't support invalidation */
                    dst[0] = '0';
                    dst[1] = '0';
                    dst   += 2;

                    /* byte 13 is length of the following data, always 2 */
                    dst[0] = '0';
                    dst[1] = '2';
                    dst   += 2;

                    /* byte 14 is struct of EF */
                    dst[0] = '0';
                    if (type == SIM_FILE_EF_DEDICATED)
                        dst[1] = '0';
                    else if (type == SIM_FILE_EF_LINEAR)
                        dst[1] = '1';
                    else
                        dst[1] = '3';

                    /* byte 15 is lenght of record, or 0 */
                    if (type == SIM_FILE_EF_DEDICATED) {
                        dst[0] = '0';
                        dst[1] = '0';
                    } else
                        gsm_hex_from_byte( dst, file->linear.rec_len );
                }
                result = 30;
            }
            break;

        default:
            result = -1;
    }
    return result;
}


static const byte_t  _const_spn_cphs[20] = {
    0x41, 0x6e, 0x64, 0x72, 0x6f, 0x69, 0x64, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
};

static const byte_t  _const_voicemail_cphs[1] = {
    0x55
};

static const byte_t  _const_iccid[10] = {
    0x98, 0x10, 0x14, 0x30, 0x12, 0x11, 0x81, 0x15, 0x70, 0x02
};

static const byte_t  _const_cff_cphs[1] = {
    0x55
};

static SimFileEFDedicatedRec  _const_files_dedicated[] =
{
    { SIM_FILE_EF_DEDICATED, 0x6f14, SIM_FILE_READ_ONLY | SIM_FILE_NEED_PIN,
      _const_spn_cphs, sizeof(_const_spn_cphs) },

    { SIM_FILE_EF_DEDICATED, 0x6f11, SIM_FILE_NEED_PIN,
      _const_voicemail_cphs, sizeof(_const_voicemail_cphs) },

    { SIM_FILE_EF_DEDICATED, 0x2fe2, SIM_FILE_READ_ONLY,
      _const_iccid, sizeof(_const_iccid) },

    { SIM_FILE_EF_DEDICATED, 0x6f13, SIM_FILE_NEED_PIN,
      _const_cff_cphs, sizeof(_const_cff_cphs) },

    { 0, 0, 0, NULL, 0 }  /* end of list */
};
#endif /* ENABLE_DYNAMIC_RECORDS */

const char*
asimcard_io( ASimCard  sim, const char*  cmd )
{
    int  nn;
#if ENABLE_DYNAMIC_RECORDS
    int  command, id, p1, p2, p3;
#endif
    static const struct { const char*  cmd; const char*  answer; } answers[] =
    {
        // CPHS Network Operator Name(6F14):
        //   PLMN Name: "Android"
        // @see Common PCN Handset Specification (Version 4.2) B.4.1.2 Network Operator Name
        { "+CRSM=192,28436,0,0,15", "+CRSM: 144,0,000000146f1404001aa0aa01020000" },
        { "+CRSM=176,28436,0,0,20", "+CRSM: 144,0,416e64726f6964ffffffffffffffffffffffffff" },

        // CPHS Voice message waiting flag(6F11):
        //   Voice Message Waiting Indicator flags:
        //     Line 1: no messages waiting.
        //     Line 2: no messages waiting.
        // @see Common PCN Handset Specification (Version 4.2) B.4.2.3 Voice Message Waiting Flags in the SIM
        { "+CRSM=192,28433,0,0,15", "+CRSM: 144,0,000000016f11040011a0aa01020000" },
        { "+CRSM=176,28433,0,0,1", "+CRSM: 144,0,55" },

        // ICC Identification(2FE2):
        //   Identification number: 89014103211118518720
        // @see 3GPP TS 11.011 section 10.1.1 EFiccid (ICC Identification)
        { "+CRSM=192,12258,0,0,15", "+CRSM: 144,0,0000000a2fe204000fa0aa01020000" },
        { "+CRSM=176,12258,0,0,10", "+CRSM: 144,0,98101430121181157002" },

        // CPHS Call forwarding flags(6F13):
        //   Voice Call forward unconditional flags:
        //     Line 1: no call forwarding message waiting.
        //     Line 2: no call forwarding message waiting.
        // @see Common PCN Handset Specification (Version 4.2) B.4.5 Diverted Call Status Indicator
        { "+CRSM=192,28435,0,0,15", "+CRSM: 144,0,000000016f13040011a0aa01020000" },
        { "+CRSM=176,28435,0,0,1",  "+CRSM: 144,0,55" },

        // SIM Service Table(6F38):
        //   Enabled: 1..4, 7, 9..19, 26, 27, 29, 38, 51..55
        // @see 3GPP TS 51.011 section 10.3.7 EFsst (SIM Service Table)
        { "+CRSM=192,28472,0,0,15", "+CRSM: 144,0,0000000f6f3804001aa0aa01020000" },
        { "+CRSM=176,28472,0,0,15", "+CRSM: 144,0,ff30ffff3f003c03000c0000f03f00" },

        // Mailbox Identifier(6FC9):
        //   Mailbox Dialing Number Identifier - Voicemail:      1
        //   Mailbox Dialing Number Identifier - Fax:            no mailbox dialing number associated
        //   Mailbox Dialing Number Identifier - Eletronic Mail: no mailbox dialing number associated
        //   Mailbox Dialing Number Identifier - Other:          no mailbox dialing number associated
        //   Mailbox Dialing Number Identifier - Videomail:      no mailbox dialing number associated
        // @see 3GPP TS 31.102 section 4.2.62 EFmbi (Mailbox Identifier)
        { "+CRSM=192,28617,0,0,15", "+CRSM: 144,0,000000086fc9040011a0aa01020104" },
        { "+CRSM=178,28617,1,4,4",  "+CRSM: 144,0,01000000" },

        // Message Waiting Indication Status(6FCA):
        //   Message Waiting Indicator Status: all inactive
        //   Number of Voicemail Messages Waiting:       0
        //   Number of Fax Messages Waiting:             0
        //   Number of Electronic Mail Messages Waiting: 0
        //   Number of Other Messages Waiting:           0
        //   Number of Videomail Messages Waiting:       0
        // @see 3GPP TS 31.102 section 4.2.63 EFmwis (Message Waiting Indication Status)
        { "+CRSM=192,28618,0,0,15", "+CRSM: 144,0,0000000a6fca040011a0aa01020105" },
        { "+CRSM=178,28618,1,4,5",  "+CRSM: 144,0,0000000000" },

        // Administrative Data(6FAD):
        //   UE Operation mode: normal
        //   Additional information: none
        //   Length of MNC in the IMSI: 3
        // @see 3GPP TS 31.102 section 4.2.18 EFad (Administrative Data)
        { "+CRSM=192,28589,0,0,15", "+CRSM: 144,0,000000046fad04000aa0aa01020000" },
        { "+CRSM=176,28589,0,0,4",  "+CRSM: 144,0,00000003" },

        // EF-IMG (4F20) : Each record of this EF identifies instances of one particular graphical image,
        //                 which graphical image is identified by this EF's record number.
        //   Number of image instance specified by this record:               01
        //   Image instance width 8 points (raster image points):             08
        //   Image instance heigh 8 points  (raster image points):            08
        //   Color image coding scheme:                                       21
        //   Image identifier id of the EF where is store the image instance: 4F02
        //   Offset of the image instance in the 4F02 EF:                     0000
        //   Length of image instance data:                                   0016
        // @see 3GPP TS 51.011 section 10.6.1.1, EF-img
        { "+CRSM=192,20256,1,4,10", "+CRSM: 144,0,000000644f20040000000005020114" },
        { "+CRSM=178,20256,1,4,20", "+CRSM: 144,0,010808214f0200000016ffffffffffffffffffff" },
        { "+CRSM=176,20226,0,0,22", "+CRSM: 144,0,080802030016AAAA800285428142814281528002AAAAFF000000FF000000FF" },
        { "+CRSM=176,20226,0,22,9", "+CRSM: 144,0,0808ff03a59999a5c3ff" },

        // CPHS Information(6F16):
        //   CPHS Phase: 2
        //   CPHS Service Table:
        //     CSP(Customer Service Profile): allocated and activated
        //     Information Numbers:           allocated and activated
        // @see Common PCN Handset Specification (Version 4.2) B.3.1.1 CPHS Information
        { "+CRSM=192,28438,0,0,15", "+CRSM: 144,0,000000026f1604001aa0aa01020000" },
        { "+CRSM=176,28438,0,0,2",  "+CRSM: 144,0,0233" },

        // Service Provider Name(6F46):
        //   Display Condition: 0x1, display network name in HPLMN; display SPN if not in HPLMN.
        //   Service Provider Name: "Android"
        // @see 3GPP TS 31.102 section 4.2.12 EFspn (Service Provider Name)
        // @see 3GPP TS 51.011 section 9.4.4 Referencing Management
        { "+CRSM=192,28486,0,0,15", "+CRSM: 144,0,000000116f4604000aa0aa01020000" },
        { "+CRSM=176,28486,0,0,17", "+CRSM: 144,0,01416e64726f6964ffffffffffffffffff" },

        // Service Provider Display Information(6FCD):
        //   Always return SW1=0x94, SW2=0x04, which means "file ID not found".
        // @see 3GPP TS 31.102 section 4.2.66 EFspdi (Service Provider Display Information)
        // @see 3GPP TS 51.011 section 9.4.4 Referencing Management
        { "+CRSM=192,28621,0,0,15", "+CRSM: 148,4" },

        // PLMN Network Name(6FC5):
        //   FIXME:
        // @see 3GPP TS 31.102 section 4.2.58 EFpnn (PLMN Network Name)
        // @see 3GPP TS 24.008
        { "+CRSM=192,28613,0,0,15", "+CRSM: 144,0,000000f06fc504000aa0aa01020118" },
        { "+CRSM=178,28613,1,4,24", "+CRSM: 144,0,43058441aa890affffffffffffffffffffffffffffffffff" },

        // MSISDN(6F40):
        //   Alpha Identifier: (empty)
        //   Length of BCD number/SSC contents: 7
        //   TON and NPI: 0x81
        //   Dialing Number/SSC String: 15555218135, actual number is "1555521"
        //                              + emulator port, e.g. "15555215554".
        //   Capacity/Configuration 2 Record Identifier: not used
        //   Extension 5 Record Identifier: not used
        // @see 3GPP TS 31.102 section 4.2.26 EFmsisdn (MSISDN)
        { "+CRSM=192,28480,0,0,15", "+CRSM: 144,0,000000806f40040011a0aa01020120" },
        { "+CRSM=178,28480,1,4,32", "+CRSM: 144,0,ffffffffffffffffffffffffffffffffffff07815155258131f5ffffffffffff" },

        // Mailbox Dialing Numbers(6FC7):
        //   Alpha Identifier: "Voicemail"
        //   Length of BCD number/SSC contents: 7
        //   TON and NPI: 0x91
        //   Dialing Number/SSC String: 15552175049
        //   Capacity/Configuration 2 Record Identifier: not used
        //   Extension 6 Record Identifier: not used
        // @see 3GPP TS 31.102 section 4.2.60 EFmbdn (Mailbox Dialing Numbers)
        { "+CRSM=192,28615,0,0,15", "+CRSM: 144,0,000000406fc7040011a0aa01020120" },
        { "+CRSM=178,28615,1,4,32", "+CRSM: 144,0,566f6963656d61696cffffffffffffffffff07915155125740f9ffffffffffff" },

        { NULL, NULL }
    };

    assert( memcmp( cmd, "+CRSM=", 6 ) == 0 );

#if ENABLE_DYNAMIC_RECORDS
    if ( sscanf(cmd, "+CRSM=%d,%d,%d,%d,%d", &command, &id, &p1, &p2, &p3) == 5 ) {
        switch (command) {
            case A_SIM_CMD_GET_RESPONSE:
                {
                    const SimFileEFDedicatedRec*  file = _const_files_dedicated;

                    assert(p1 == 0 && p2 == 0 && p3 == 15);

                    for ( ; file->id != 0; file++ ) {
                        if (file->id == id) {
                            int    count;
                            char*  out = sim->out_buff;
                            strcpy( out, "+CRSM: 144,0," );
                            out  += strlen(out);
                            count = sim_file_to_hex( (SimFile) file, out );
                            if (count < 0)
                                return "ERROR: INTERNAL SIM ERROR";
                            out[count] = 0;
                            return sim->out_buff;
                        }
                    }
                    break;
                }

            case A_SIM_CMD_READ_BINARY:
                {
                    const SimFileEFDedicatedRec*  file = _const_files_dedicated;

                    assert(p1 == 0 && p2 == 0);

                    for ( ; file->id != 0; file++ ) {
                        if (file->id == id) {
                            char*  out = sim->out_buff;

                            if (p3 > file->length)
                                return "ERROR: BINARY LENGTH IS TOO LONG";

                            strcpy( out, "+CRSM: 144,0," );
                            out  += strlen(out);
                            gsm_hex_from_bytes( out, file->data, p3 );
                            out[p3*2] = 0;
                            return sim->out_buff;
                        }
                    }
                    break;
                }

            case A_SIM_CMD_READ_RECORD:
                break;

            default:
                return "ERROR: UNSUPPORTED SIM COMMAND";
        }
    }
#endif

    if (!strcmp("+CRSM=178,28480,1,4,32", cmd)) {
        snprintf( sim->out_buff, sizeof(sim->out_buff), "+CRSM: 144,0,ffffffffffffffffffffffffffffffffffff0781515525%d1%d%df%dffffffffffff", (sim->port / 1000) % 10, (sim->port / 10) % 10, (sim->port / 100) % 10, sim->port % 10);
        return sim->out_buff;
        }

    for (nn = 0; answers[nn].cmd != NULL; nn++) {
        if ( !strcmp( answers[nn].cmd, cmd ) ) {
            return answers[nn].answer;
        }
    }
    return "ERROR: BAD COMMAND";
}

