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

#include <assert.h>
#include <limits.h>
#include "base64.h"

ssize_t
encode_base64(const unsigned char* in, size_t ilen, char* out, size_t olen)
{
    static const unsigned char value[64] = {
         [0] = 'A',  [1] = 'B',  [2] = 'C',  [3] = 'D',
         [4] = 'E',  [5] = 'F',  [6] = 'G',  [7] = 'H',
         [8] = 'I',  [9] = 'J', [10] = 'K', [11] = 'L',
        [12] = 'M', [13] = 'N', [14] = 'O', [15] = 'P',
        [16] = 'Q', [17] = 'R', [18] = 'S', [19] = 'T',
        [20] = 'U', [21] = 'V', [22] = 'W', [23] = 'X',
        [24] = 'Y', [25] = 'Z', [26] = 'a', [27] = 'b',
        [28] = 'c', [29] = 'd', [30] = 'e', [31] = 'f',
        [32] = 'g', [33] = 'h', [34] = 'i', [35] = 'j',
        [36] = 'k', [37] = 'l', [38] = 'm', [39] = 'n',
        [40] = 'o', [41] = 'p', [42] = 'q', [43] = 'r',
        [44] = 's', [45] = 't', [46] = 'u', [47] = 'v',
        [48] = 'w', [49] = 'x', [50] = 'y', [51] = 'z',
        [52] = '0', [53] = '1', [54] = '2', [55] = '3',
        [56] = '4', [57] = '5', [58] = '6', [59] = '7',
        [60] = '8', [61] = '9', [62] = '+', [63] = '/'
    };

    size_t len;
    long shift;
    unsigned char rest;

    assert(CHAR_BIT == 8); /* should be true on most modern platforms */
    assert(in || !ilen);
    assert(olen || !ilen);
    assert(out || !olen);

    /* encode input string */
    for (len = 0, shift = 2, rest = 0; ilen; --olen, ++out) {
        unsigned char c = *in;
        assert(!(shift % 2));
        assert(olen);
        if (!shift) {
            *out = value[rest];
            rest = 0;
            ++len;
        } else if (shift == 2) {
            *out = value[rest | (c >> 2)];
            rest = (c&0x03) << 4;
            ++in; --ilen; ++len;
        } else if (shift == 4) {
            *out = value[rest | (c >> 4)];
            rest = (c&0x0f) << 2;
            ++in; --ilen; ++len;
        } else if (shift == 6) {
            *out = value[rest | (c >> 6)];
            rest = (c&0x3f) << 0;
            ++in; --ilen; ++len;
        }
        shift = (shift + 2) % 8;
    }
    /* append remaining bits */
    if (rest) {
        *out = value[rest];
        ++out; --olen; ++len;
    }
    /* append padding bytes */
    while (len % 4) {
        assert(olen);
        *out = '=';
        ++out; --olen; ++len;
    }
    return len;
}

ssize_t
decode_base64(const char* in, size_t ilen, unsigned char* out, size_t olen)
{
    static const unsigned char value[1<<CHAR_BIT] = {
        ['A'] =  0, ['B'] =  1, ['C'] =  2, ['D'] =  3,
        ['E'] =  4, ['F'] =  5, ['G'] =  6, ['H'] =  7,
        ['I'] =  8, ['J'] =  9, ['K'] = 10, ['L'] = 11,
        ['M'] = 12, ['N'] = 13, ['O'] = 14, ['P'] = 15,
        ['Q'] = 16, ['R'] = 17, ['S'] = 18, ['T'] = 19,
        ['U'] = 20, ['V'] = 21, ['W'] = 22, ['X'] = 23,
        ['Y'] = 24, ['Z'] = 25, ['a'] = 26, ['b'] = 27,
        ['c'] = 28, ['d'] = 29, ['e'] = 30, ['f'] = 31,
        ['g'] = 32, ['h'] = 33, ['i'] = 34, ['j'] = 35,
        ['k'] = 36, ['l'] = 37, ['m'] = 38, ['n'] = 39,
        ['o'] = 40, ['p'] = 41, ['q'] = 42, ['r'] = 43,
        ['s'] = 44, ['t'] = 45, ['u'] = 46, ['v'] = 47,
        ['w'] = 48, ['x'] = 49, ['y'] = 50, ['z'] = 51,
        ['0'] = 52, ['1'] = 53, ['2'] = 54, ['3'] = 55,
        ['4'] = 56, ['5'] = 57, ['6'] = 58, ['7'] = 59,
        ['8'] = 60, ['9'] = 61, ['+'] = 62, ['/'] = 63,
        ['='] = 0xff
    };

    size_t len;
    long shift;

    assert(CHAR_BIT == 8); /* should be true on most modern platforms */
    assert(in || !ilen);
    assert(olen || !ilen);
    assert(out || !olen);

    for (len = 0, shift = 0; ilen; --ilen, ++in) {
        unsigned long c = value[(unsigned char)(*in)];
        if (c == 0xff) {
            break; /* ignoring padding at the end of input */
        }
        if (!c && (*in != 'A')) {
            return -1; /* non-base64 input */
        }
        assert(!(shift % 2));
        assert(olen);
        if (!shift) {
            /* input value aligned to highest bit of current field */
            shift += 2;
            *out = c << shift;
        } else if (shift == 6) {
            /* input value aligned to lowest bit of current field */
            shift = 0;
            *out |= c;
            if (!olen) {
                return -1; /* out-of-memory */
            }
            ++out; --olen; ++len;
        } else {
            /* input value crosses field boundary */
            *out |= c >> (6-shift);
            if (!olen) {
                return -1; /* out-of-memory */
            }
            ++out; --olen; ++len;
            shift += 2;
            assert(olen);
            *out = c << shift;
        }
    }
    return len;
}
