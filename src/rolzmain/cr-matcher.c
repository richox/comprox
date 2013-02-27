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

int flexible_parsing = 0;
int using_ctx4 = 0;

#define M_table_elem(n)     (matcher->m_table[n])
#define M_table_item(x, n)  (M_table_elem(x).m_item[(M_table_elem(x).m_head + M_rolz_indices - (n)) % M_rolz_indices])
#define M_table_hash(x, n)  (M_table_elem(x).m_hash[(M_table_elem(x).m_head + M_rolz_indices - (n)) % M_rolz_indices])

static inline uint32_t M_rolz_hash_ctx(unsigned char* x) {
    return using_ctx4
        ? (uint32_t)(x[0] * 1313131 + x[-1] * 13131 + x[-2] * 131 + x[-3]) % M_rolz_buckets
        : (uint32_t)(x[0] * 1313131 + x[-1] * 13131 + x[-2] * 131        ) % M_rolz_buckets;
}

int matcher_init(matcher_t* matcher) {
    uint32_t i;

    if((matcher->m_table = malloc(M_rolz_buckets * sizeof(matcher->m_table[0]))) != NULL) {
        memset(matcher->m_table, -1, M_rolz_buckets * sizeof(matcher->m_table[0]));
        for(i = 0; i < M_rolz_buckets; i++) {
            M_table_elem(i).m_head = 0;
        }
        matcher->m_context = 0;
        memset(matcher->m_short_table, 0, sizeof(matcher->m_short_table));
        matcher->m_short_context = 0;
        return 0;
    }
    return -1;
}

int matcher_free(matcher_t* matcher) {
    free(matcher->m_table);
    return 0;
}

int matcher_update(matcher_t* matcher, unsigned char* data, uint32_t pos, int encode) {
    if(pos < 16) { /* no enough context */
        return 0;
    }
    M_table_elem(matcher->m_context).m_head = (M_table_elem(matcher->m_context).m_head + 1) % M_rolz_indices;
    M_table_item(matcher->m_context, 0) = pos;
    if(encode) {
        M_table_hash(matcher->m_context, 0) = data[pos];
    }
    matcher->m_context = M_rolz_hash_ctx(data + pos);

    memmove(matcher->m_short_table[matcher->m_short_context] + 1,
            matcher->m_short_table[matcher->m_short_context], (M_rolz_indices_short - 1) * sizeof(uint32_t));
    matcher->m_short_table[matcher->m_short_context][0] = pos;
    matcher->m_short_context = data[pos];
    return 0;
}

int matcher_getpos(matcher_t* matcher, uint32_t idx) {
    if(idx < M_rolz_indices) {
        return M_table_item(matcher->m_context, idx);
    }
    return matcher->m_short_table[matcher->m_short_context][idx - M_rolz_indices];
}

static matcher_ret_t match(matcher_t* matcher, unsigned char* data, uint32_t pos, uint32_t context, int minlen) {
    matcher_ret_t ret = {-1, minlen - 1};
    uint32_t i;
    uint32_t j;
    uint32_t offset;

    for(i = 0; i < M_rolz_indices && ret.m_len < M_rolz_maxlength && M_table_item(context, i) != -1; i++) {
        offset = M_table_item(context, i);
        j = ret.m_len;

        /* fast check with hashbits */
        if(M_table_hash(context, i) == data[pos]) {
            while(j < M_rolz_maxlength && data[pos + j] == data[offset + j]) { /* compare for (pos >= ret.m_len) */
                j++;
            }
            if(j > ret.m_len && memcmp(data + pos, data + offset, ret.m_len) == 0) { /* compare for (pos < ret.m_len) */
                /* a better match found */
                ret.m_idx = i;
                ret.m_len = j;
                if(ret.m_len == M_rolz_maxlength) {
                    return ret;
                }
            }
        }
    }
    if(ret.m_len < minlen) {
        ret.m_idx = -1;
        ret.m_len = 1;
    }
    return ret;
}

matcher_ret_t matcher_lookup(matcher_t* matcher, unsigned char* data, uint32_t pos) {
    uint32_t prices[260];
    uint32_t maxprice;
    uint32_t find_short;
    uint32_t offset;
    uint32_t i;
    uint32_t j;
    matcher_ret_t ret;
    matcher_ret_t ret2;

    if(pos < 16) { /* no enough context */
        ret.m_idx = -1;
        ret.m_len = 1;
        return ret;
    }

    /* match current position */
    ret = match(matcher, data, pos, matcher->m_context, M_rolz_minlength);
    find_short = (ret.m_len < M_rolz_minlength);

    /* flexible parsing */
    if(flexible_parsing && !find_short) {
        /* price() function:
         *  assume matched byte costs 3 bits, unmatched byte costs 8 bits, idx/len costs 3 bits */
#define M_price_ml(l)   ((l) * 3 * M_rolz_indices)
#define M_price_ul(l)   ((l) * 9 * M_rolz_indices)
#define M_price(i, l)   ((l) >= M_rolz_minlength ? (M_price_ml((l)-1)) - 3*(i) : M_price_ul(1))

        for(i = 1; i <= ret.m_len; i++) {
            ret2 = match(matcher, data, pos + i, M_rolz_hash_ctx(data + pos + i - 1), M_rolz_minlength);
            prices[i] = M_price(ret2.m_idx, ret2.m_len);
        }
        maxprice = M_price(ret.m_idx, ret.m_len) + prices[ret.m_len];

        for(i = ret.m_len - 1; i >= 1; i--) {
            if(M_price(ret.m_idx, i) + prices[i] > maxprice) {
                ret.m_len = i;
                maxprice = M_price(ret.m_idx, i) + prices[i];
            }
        }
    }

    /* find shorter match */
    if(find_short) {
        ret.m_len = M_rolz_minlength - 1;
        ret.m_idx = -1;
        for(i = 0; i < M_rolz_indices_short; i++) {
            offset = matcher->m_short_table[matcher->m_short_context][i];
            j = 0;
            while(j < M_rolz_maxlength && data[pos + j] == data[offset + j]) {
                j++;
            }
            if(j > ret.m_len) { /* a better match found */
                ret.m_idx = M_rolz_indices + i;
                ret.m_len = j;
            }
        }
    }
    if(ret.m_len < M_rolz_minlength) {
        ret.m_idx = -1;
        ret.m_len = 1;
    }

    /* lazy parsing */
    if((!flexible_parsing || find_short) && ret.m_len > 1) {
        for(i = 1; i < M_rolz_minlength; i++) {
            ret2 = match(matcher, data, pos + i, M_rolz_hash_ctx(data + pos + i - 1), M_rolz_minlength);
            if(M_price(ret2.m_idx, ret2.m_len) > M_price(ret.m_idx, ret.m_len) + i * M_rolz_indices) {
                ret.m_idx = -1;
                ret.m_len = 1;
                break;
            }
        }
    }
    return ret;
}
