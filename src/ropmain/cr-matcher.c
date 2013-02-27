/*
 * Copyright (C) 2011-2012 by Zhang Li <RichSelian at gmail.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the project nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE PROJECT AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE PROJECT OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#include "cr-matcher.h"

#define M_hash2_(x)     ((*(uint16_t*)(x)))
#define M_hash4_(x)     ((*(uint32_t*)(x) ^ (*(uint32_t*)(x) >>  6) ^ (*(uint32_t*)(x) >> 12)) & 0x0fffff)
#define M_hash8_(x)     ((*(uint64_t*)(x) ^ (*(uint64_t*)(x) >> 20) ^ (*(uint64_t*)(x) >> 40)) & 0xffffff)

int matcher_init(matcher_t* matcher) {
    int i;
    matcher->m_lzp8 = malloc((1 << 24) * sizeof(uint32_t));
    matcher->m_lzp4 = malloc((1 << 20) * sizeof(uint32_t));
    matcher->m_lzp2 = malloc((1 << 16) * sizeof(uint32_t));
    for(i = 0; i < (1 << 24); i++) {
        matcher->m_lzp8[i] = 8;
    }
    for(i = 0; i < (1 << 20); i++) {
        matcher->m_lzp4[i] = 4;
    }
    for(i = 0; i < (1 << 16); i++) {
        matcher->m_lzp2[i] = 2;
    }
    return 0;
}

int matcher_free(matcher_t* matcher) {
    free(matcher->m_lzp8);
    free(matcher->m_lzp4);
    free(matcher->m_lzp2);
    return 0;
}

int matcher_getpos(matcher_t* matcher, unsigned char* data, uint32_t pos) {
    uint32_t lzpos[3] = {
        matcher->m_lzp8[M_hash8_(data + pos - 8)],
        matcher->m_lzp4[M_hash4_(data + pos - 4)],
        matcher->m_lzp2[M_hash2_(data + pos - 2)],
    };

    if(memcmp(data + lzpos[0] - 8, data + pos - 8, 8) == 0) {
        return lzpos[0];
    }
    if(memcmp(data + lzpos[1] - 4, data + pos - 4, 4) == 0) {
        return lzpos[1];
    }
    return lzpos[2];
}

int matcher_lookup(matcher_t* matcher, unsigned char* data, uint32_t pos) {
    uint32_t match_pos = matcher_getpos(matcher, data, pos);
    uint32_t match_len = 0;

    /* match content */
    if(match_pos != 0) {
        while(match_len < match_max && data[match_pos + match_len] == data[pos + match_len]) {
            match_len++;
        }
    }
    if(match_len < match_min) { /* too short */
        match_len = 1;
    }
    return match_len;
}

int matcher_update(matcher_t* matcher, unsigned char* data, uint32_t pos) {
    matcher->m_lzp8[M_hash8_(data + pos - 8)] = pos;
    matcher->m_lzp4[M_hash4_(data + pos - 4)] = pos;
    matcher->m_lzp2[M_hash2_(data + pos - 2)] = pos;
    return 0;
}
