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
#include "../miniport-thread.h"

int flexible_parsing = 0;

uint32_t match_min_near = 6;
uint32_t match_min;
uint32_t match_max = 255;
uint32_t match_limit = 40; /* default */

static inline uint32_t hash1(unsigned char* s) {
    return s[0] + s[1];
}

static inline uint32_t hash2(unsigned char* s) {
    uint32_t hash = 0;
    uint32_t i;

    for(i = 0; i < match_min; i++) {
        hash = (hash * 123456791) ^ *s++;
    }
    return hash;
}

/* pthread-callback wrapper */
typedef struct matcher_init_thread_param_pack_t {
    matcher_t* m_matcher;
    uint32_t m_start;
    uint32_t m_bucketsize1;
    uint32_t m_bucketsize2;
    uint32_t* m_bucket1;
    uint32_t* m_bucket2;
    unsigned char* m_data;
} matcher_init_thread_param_pack_t;

void* matcher_init_thread(matcher_init_thread_param_pack_t* args) {
    uint32_t hash;
    uint32_t pos;
    uint32_t i;
    uint32_t j;
    matcher_t* matcher = args->m_matcher;
    uint32_t start = args->m_start;
    uint32_t bucketsize1 = args->m_bucketsize1;
    uint32_t bucketsize2 = args->m_bucketsize2;
    uint32_t* bucket1 = args->m_bucket1;
    uint32_t* bucket2 = args->m_bucket2;
    unsigned char* data = args->m_data;

    for(i = start; i < bucketsize1; i += 2) {
        memset(bucket2, -1, bucketsize2 * sizeof(uint32_t));
        for(pos = bucket1[i]; pos != -1; pos = j) {
            hash = hash2(data + pos) % bucketsize2;
            j = matcher->m_next[pos];
            matcher->m_next[pos] = bucket2[hash];
            bucket2[hash] = pos;
        }
    }
    return 0;
}

int matcher_init(matcher_t* matcher, unsigned char* data, uint32_t len, int print_information) {
    const uint32_t bucketsize1 = 20;
    const uint32_t bucketsize2 = 20 + len / 25;
    uint32_t hash;
    uint32_t pos;
    uint32_t* bucket1 =  malloc(bucketsize1 * sizeof(uint32_t));
    uint32_t* bucket21 = malloc(bucketsize2 * sizeof(uint32_t));
    uint32_t* bucket22 = malloc(bucketsize2 * sizeof(uint32_t));
    pthread_t thread1;
    pthread_t thread2;
    matcher_init_thread_param_pack_t args1;
    matcher_init_thread_param_pack_t args2;

    if(print_information) {
        fprintf(stderr, "%s\n", "-> initializing matcher...");
    }
    matcher->m_last_match = 0;
    matcher->m_short_cache = malloc(65536 * sizeof(uint32_t));
    matcher->m_next = malloc(len * sizeof(uint32_t));
    matcher->m_ret_start = 0;
    matcher->m_ret_end = 0;

    memset(matcher->m_short_cache, 0, 65536 * sizeof(uint32_t));
    memset(matcher->m_next, -1, len * sizeof(uint32_t));

    /* start building hash chains */
    memset(bucket1, -1, bucketsize1 * sizeof(uint32_t));

    /* 1st bucket pass */
    for(pos = 0; pos + match_max < len; pos++) {
        hash = hash1(data + len - match_max - 1 - pos) % bucketsize1;
        matcher->m_next[len - match_max - 1 - pos] = bucket1[hash];
        bucket1[hash] = len - match_max - 1 - pos;
    }

    /* 2nd bucket pass (multi-threaded) */
    args1.m_bucketsize1 = bucketsize1;
    args1.m_bucketsize2 = bucketsize2;
    args1.m_matcher = matcher;
    args1.m_bucket1 = bucket1;
    args1.m_data = data;
    args1.m_bucket2 = bucket21;
    args1.m_start = 0;
    args2.m_bucketsize1 = bucketsize1;
    args2.m_bucketsize2 = bucketsize2;
    args2.m_matcher = matcher;
    args2.m_bucket1 = bucket1;
    args2.m_data = data;
    args2.m_bucket2 = bucket22;
    args2.m_start = 1;
    pthread_create(&thread1, 0, (void*)matcher_init_thread, &args1);
    pthread_create(&thread2, 0, (void*)matcher_init_thread, &args2);

    pthread_join(thread1, 0);
    pthread_join(thread2, 0);
    free(bucket22);
    free(bucket21);
    free(bucket1);
    return 0;
}

int matcher_free(matcher_t* matcher) {
    free(matcher->m_short_cache);
    free(matcher->m_next);
    return 0;
}

static inline matcher_ret_t match(
        matcher_t* matcher,
        unsigned char* data,
        uint32_t pos,
        uint32_t match_min,
        uint32_t match_limit,
        uint32_t lazy) {

    uint32_t new_len;
    uint32_t node;
    uint32_t i;
    uint32_t distance_price;
    matcher_ret_t ret;

    ret.m_pos = 0;
    ret.m_len = match_min - 1;

    /* find longest match */
    node = matcher->m_next[pos];
    for(i = 0; i < match_limit && node != -1; i++) {
        new_len = ret.m_len;
        while(new_len < match_max && data[node + new_len] == data[pos + new_len]) {
            new_len += 1;
        }

        /* longer distance results higher price */
        distance_price = 0;
        distance_price += (pos - node) / 1048576 > pos - ret.m_pos;
        distance_price += (pos - node) / 4096 > pos - ret.m_pos;
        distance_price += (pos - node) / 64 > pos - ret.m_pos;

        if(new_len > ret.m_len + distance_price && memcmp(data + pos, data + node, ret.m_len) == 0) {
            ret.m_pos = node;
            ret.m_len = new_len;
            if((lazy && (lazy < ret.m_pos)) || ret.m_len == match_max) {
                return ret; /* in lazy mode, we return directly once a longer match found */
            }
        }
        node = matcher->m_next[node];
    }
    if(ret.m_len < match_min) {
        ret.m_pos = -1;
        ret.m_len = 1;
    }
    return ret;
}

static uint32_t short_hash(unsigned char* s) {
    uint32_t hash = 0;
    uint32_t i;

    for(i = 0; i < match_min_near; i++) {
        hash = (hash * 123456791) ^ *s++;
    }
    return hash;
}

int matcher_update_cache(matcher_t* matcher, unsigned char* data, uint32_t pos) {
    matcher->m_short_cache[short_hash(data + pos) % 65536] = pos;
    return 0;
}

static int fast_log2(uint32_t x) {
    static const uint8_t log_2[256] = {
        0, 1, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 4, 4, 4, 4, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
        6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
        7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
        7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
        8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
        8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
        8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
        8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
    };
    int l = -1;

    if(x >= 256) l += 8, x /= 256;
    if(x >= 256) l += 8, x /= 256;
    if(x >= 256) l += 8, x /= 256;
    return l + log_2[x];
}

matcher_ret_t matcher_lookup(matcher_t* matcher, unsigned char* data, uint32_t pos) {
    matcher_ret_t tmpret1 = {0, 0};
    matcher_ret_t tmpret2 = {0, 0};
    matcher_ret_t ret;
    matcher_ret_t rets[260];
    uint32_t i;
    uint32_t maxprice;

    /* lookup at last_match first */
    if((tmpret1.m_pos = pos - matcher->m_last_match) < pos) {
        for(i = 0; i < match_max && data[pos + i] == data[tmpret1.m_pos + i]; i++) {
            tmpret1.m_len += 1;
        }
    }

    /* find a normal match */
    if(flexible_parsing) { /* flexible parsing */
        if(matcher->m_ret_end <= pos) { /* if match result is already in cache, we don't have to match again */
            matcher->m_ret_cache[0] = match(matcher, data, pos, match_min, match_limit, 0);
            matcher->m_ret_start = pos;
            matcher->m_ret_end = pos + 1;
        } else {
            memmove(matcher->m_ret_cache,
                    matcher->m_ret_cache + (pos - matcher->m_ret_start),
                    sizeof(matcher_ret_t) * (matcher->m_ret_end - pos));
            matcher->m_ret_start = pos;
        }
        rets[0] = matcher->m_ret_cache[0];
        ret = rets[0];

        if(rets[0].m_len >= match_min) {
            /* price() function */
#define M_price_ml(l)   ((l) * 3)
#define M_price_ul(l)   ((l) * 9)
#define M_price(i, l)   ((l) >= match_min ? (M_price_ml((l)-1)) - (fast_log2(pos-(i))*4/5) : M_price_ul(1))

            for(i = 1; i <= rets[0].m_len; i++) {
                if(matcher->m_ret_end <= pos + i) {
                    matcher->m_ret_cache[i] = match(matcher, data, pos + i, match_min, match_limit, 0);
                    matcher->m_ret_end += 1;
                }
                rets[i] = matcher->m_ret_cache[i];
            }
            maxprice = M_price(rets[0].m_pos, rets[0].m_len) + M_price(rets[i - 1].m_pos, rets[i - 1].m_len);

            ret = rets[0];
            for(i = rets[0].m_len - 1; i >= 1; i--) {
                if(maxprice < M_price(ret.m_pos, i) + M_price(rets[i].m_pos, rets[i].m_len)) {
                    ret.m_len = i;
                    maxprice = M_price(ret.m_pos, i) + M_price(rets[i].m_pos, rets[i].m_len);
                }
            }
            if(ret.m_len < match_min) {
                ret.m_pos = -1;
                ret.m_len = 1;
            }
        }

    } else { /* lazy parsing */
        ret = match(matcher, data, pos, match_min, match_limit, 0);
        if(ret.m_len >= match_min && ((
                        /* check more carefully at pos+1 */
                        tmpret2 = match(matcher, data, pos + 1, ret.m_len + 1, match_limit / 4, 1),
                        tmpret2.m_len > ret.m_len + (tmpret2.m_pos < ret.m_pos)) ||

                    match(matcher, data, pos + 2, ret.m_len + 1, match_limit / 8, 1).m_len > 1 ||
                    match(matcher, data, pos + 3, ret.m_len + 2, match_limit / 8, 1).m_len > 1 ||
                    match(matcher, data, pos + 4, ret.m_len + 2, match_limit / 8, 1).m_len > 1 ||
                    match(matcher, data, pos + 5, ret.m_len + 2, match_limit / 8, 1).m_len > 1 ||
                    match(matcher, data, pos + 6, ret.m_len + 3, match_limit / 8, 1).m_len > 1)) {
            ret.m_pos = -1;
            ret.m_len = 1;
        }
    }

    if(ret.m_pos != -1 && ret.m_len < tmpret1.m_len + 3 +
            (ret.m_pos + 64 < pos) +
            (ret.m_pos + 4096 < pos) +
            (ret.m_pos + 1048576 < pos)) {
        ret = tmpret1; /* a last_match will save several bytes for distance than a normal match */
    }

    /* find a shorter match */
    if(ret.m_len < match_min_near) {
        ret.m_pos = matcher->m_short_cache[short_hash(data + pos) % 65536];
        ret.m_len = 0;
        if(ret.m_pos < pos && ret.m_pos + 256 > pos) {
            for(i = 0; i < match_max; i++) {
                if(data[ret.m_pos + i] != data[pos + i]) {
                    break;
                }
            }
            ret.m_len = i;
        }
    }

    if(ret.m_len < match_min_near || (ret.m_len < match_min && ret.m_pos + 256 <= pos)) {
        ret.m_pos = -1;
        ret.m_len = 1;
    } else {
        matcher->m_last_match = pos - ret.m_pos; /* update last match */
    }
    return ret;
}
