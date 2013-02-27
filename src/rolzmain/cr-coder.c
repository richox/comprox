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
#include "cr-coder.h"
#include "cr-matcher.h"
#include "../cr-datablock.h"
#include "../cr-rangecoder.h"
#include "../cr-model.h"
#include "../cr-ppm.h"
#include "../miniport-thread.h"

static void update_progress(uint32_t current, uint32_t total) {
    static int p = -1;

    if(total >= 100) {
        if(p != current / (total / 100)) {
            p = current / (total / 100);
            fprintf(stderr, "-> \t\t\r");
            fprintf(stderr, "-> %d%%\r", p);
            fflush(stderr);
        }
    }
    return;
}

/* global models -- for lzencode() and lzdecode() */
static struct {
    model_t idx_model;
    model_t len_model;
    ppm_model_t ppm_model;
} m;

/* global coders -- for lzencode() and lzdecode() */
range_coder_t coder;
range_coder_t idx_coder;

/* block header fields */
struct {
    uint8_t  m_firstbyte;
    uint8_t  m_compressed;
    uint8_t  m_esc;
    uint32_t m_original_size;
    uint32_t m_num_idx;
    uint32_t m_offset_idx;
}
block_header;

/* common model initializer */
static inline void atexit_free_models() {
    ppm_model_free(&m.ppm_model);
    return;
}
void reset_models() {
    int i;
    int register_atexit = 0;

    if(!register_atexit) {
        atexit(atexit_free_models);
        register_atexit = 1;
    }
    ppm_model_free(&m.ppm_model);
    ppm_model_init(&m.ppm_model);

    for(i = 0; i < 256; i++) {
        m.idx_model.m_frq_table[i] = (i < M_rolz_indices + M_rolz_indices_short);
        m.len_model.m_frq_table[i] = (i == 0 || (i >= M_rolz_minlength && i <= M_rolz_maxlength));
    }
    model_recalc_cum(&m.idx_model);
    model_recalc_cum(&m.len_model);
    return;
}

/* pthread-callback wrapper */
typedef struct lzmatch_thread_param_pack_t {
    matcher_t*      m_matcher;
    data_block_t*   m_iblock;
    uint32_t*       m_pos;
    uint32_t*       m_pool_idx;
    uint32_t*       m_pool_len;
} lzmatch_thread_param_pack_t;

#define M_match_rets_size 32000

static void* lzmatch_thread(lzmatch_thread_param_pack_t* args) { /* thread for finding matches */
    uint32_t pos = args->m_pos[0];
    uint32_t i;
    matcher_ret_t ret;
    uint32_t retindex = 0;

    while(pos < args->m_iblock->m_size && retindex < M_match_rets_size) {
        ret.m_idx = -1;
        ret.m_len = 1;
        if(pos + 1024 < args->m_iblock->m_size) { /* find a match -- avoid overflow */
            ret = matcher_lookup(args->m_matcher, args->m_iblock->m_data, pos);
        }

        for(i = 0; i < ret.m_len; i++) { /* update context */
            matcher_update(args->m_matcher, args->m_iblock->m_data, pos + i, 1);
        }
        args->m_pool_idx[retindex] = ret.m_idx;
        args->m_pool_len[retindex] = ret.m_len;
        retindex += 1;
        pos += ret.m_len;
    }

    if(retindex < M_match_rets_size) { /* put an END flag */
        args->m_pool_idx[retindex] = -1;
        args->m_pool_len[retindex] = -1;
    }
    args->m_pos[0] = pos;
    return NULL;
}

void lzencode(data_block_t* ib, data_block_t* ob, int print_information) {
    uint32_t     i;
    uint32_t     match_idx;
    uint32_t     match_len;
    data_block_t idx_block = INITIAL_BLOCK;
    uint32_t     pos = 1;
    uint32_t     counter[256] = {0};
    int          esc = 0;

    lzmatch_thread_param_pack_t thread_args;
    matcher_t matcher;
    pthread_t thread;
    uint32_t  pool_idx[2][M_match_rets_size];
    uint32_t  pool_len[2][M_match_rets_size];
    uint32_t  pool_n = 0;
    uint32_t  pool_index = 0;
    uint32_t  pool_pos = 1;

    if(print_information) {
        fprintf(stderr, "%s\n", "-> running ROLZ encoding...");
    }

    /* configure matcher */
    using_ctx4 = ib->m_size >= 4194304;
    matcher_init(&matcher);

    /* reserve space for block header */
    data_block_resize(ob, sizeof(block_header));
    block_header.m_num_idx = 0;
    block_header.m_firstbyte = ib->m_data[0];

    /* find escape */
    for(i = 0; i < ib->m_size; i++) {
        counter[ib->m_data[i]]++;
    }
    for(i = 1; i < 256; i++) {
        if(counter[esc] > counter[i]) {
            esc = i;
        }
    }
    block_header.m_esc = esc;

    range_encoder_init(&coder);
    range_encoder_init(&idx_coder);

    /* init matching thread */
    thread_args.m_matcher = &matcher;
    thread_args.m_iblock = ib;
    thread_args.m_pos = &pool_pos;
    thread_args.m_pool_len = pool_len[0];
    thread_args.m_pool_idx = pool_idx[0]; lzmatch_thread(&thread_args);
    thread_args.m_pool_len = pool_len[1];
    thread_args.m_pool_idx = pool_idx[1]; pthread_create(&thread, 0, (void*)lzmatch_thread, &thread_args);

    /* lit =    3
     * match =  2
     * eof =    0
     */
    while(pos < ib->m_size) {
        if(print_information) {
            update_progress(pos, ib->m_size);
        }

        if(pool_index == M_match_rets_size) { /* start the next matching thread */
            pthread_join(thread, 0);
            thread_args.m_pool_idx = pool_idx[pool_n];
            thread_args.m_pool_len = pool_len[pool_n];
            pthread_create(&thread, 0, (void*)lzmatch_thread, &thread_args);
            pool_index = 0;
            pool_n = 1 - pool_n;
        }
        match_idx = pool_idx[pool_n][pool_index];
        match_len = pool_len[pool_n][pool_index];
        pool_index += 1;

        if(match_idx != -1) { /* ROLZ match */
            ppm_encode(&coder, &m.ppm_model, esc, ob);
            M_my_enc_(idx_coder, &idx_block, m.len_model, match_len, 4);
            M_my_enc_(idx_coder, &idx_block, m.idx_model, match_idx, 4);
            block_header.m_num_idx += 1;

        } else { /* literal */
            ppm_encode(&coder, &m.ppm_model, ib->m_data[pos], ob);
            if(ib->m_data[pos] == esc) {
                M_my_enc_(idx_coder, &idx_block, m.len_model, 0, 4);
                block_header.m_num_idx += 1;
            }
        }
        for(i = 0; i < match_len; i++) { /* update context */
            ppm_update_context(&m.ppm_model, ib->m_data[pos++]);
        }

        if(ob->m_size >= ib->m_size) { /* cannot compress */
            goto CannotCompress;
        }
    }
    pthread_join(thread, 0);
    matcher_free(&matcher);

    range_encoder_flush(&coder, ob);
    range_encoder_flush(&idx_coder, &idx_block);

    /* set block header */
    block_header.m_compressed = 1;
    block_header.m_original_size = ib->m_size;
    block_header.m_offset_idx = ob->m_size;
    memcpy(ob->m_data, &block_header, sizeof(block_header));

    /* append extra blocks to ob */
    data_block_resize(ob, ob->m_size + idx_block.m_size);
    memcpy(ob->m_data + block_header.m_offset_idx, idx_block.m_data, idx_block.m_size);
    data_block_destroy(&idx_block);
    return;

CannotCompress:
    pthread_join(thread, 0);
    matcher_free(&matcher);

    data_block_resize(ob, sizeof(block_header) + ib->m_size);
    memset(ob->m_data, 0, sizeof(block_header));
    for(i = 0; i < ib->m_size; i++) {
        ob->m_data[sizeof(block_header) + i] = ib->m_data[i];
    }
    data_block_destroy(&idx_block);
    return;
}

/* pthread-callback wrapper */
typedef struct lzdecode_thread_param_pack_t {
    uint32_t* m_idx_queue;
    uint32_t* m_len_queue;
    uint8_t** m_input_idx;
} lzdecode_thread_param_pack_t;

#define M_idx_queue_size   10000

static void* lzdecode_idx_thread(lzdecode_thread_param_pack_t* args) { /* thread for decoding idx/len */
    decode_symbol_t decode_helper;
    uint32_t i;

    /* decode idx */
    for(i = 0; block_header.m_num_idx > 0 && i < M_idx_queue_size; i++) {
        block_header.m_num_idx--;
        args->m_len_queue[i] =                            M_my_dec_(idx_coder, *args->m_input_idx, m.len_model, 4);
        args->m_idx_queue[i] = args->m_len_queue[i] > 0 ? M_my_dec_(idx_coder, *args->m_input_idx, m.idx_model, 4) : 0;
    }
    return 0;
}
void lzdecode(data_block_t* ib, data_block_t* ob, int print_information) {
    uint32_t match_idx;
    uint32_t match_len;
    uint32_t i;
    uint32_t pos;
    unsigned char* input;
    unsigned char* input_idx;

    matcher_t matcher;
    pthread_t thread;
    lzdecode_thread_param_pack_t thread_args;
    uint32_t idx_index = 0;
    uint32_t idx_n = 0;
    uint32_t idx_queue[2][M_idx_queue_size];
    uint32_t len_queue[2][M_idx_queue_size];
    uint32_t decode_symbol;

    if(print_information) {
        fprintf(stderr, "%s\n", "-> running ROLZ decoding...");
    }

    memcpy(&block_header, ib->m_data, sizeof(block_header));
    if(!block_header.m_compressed) {
        for(i = sizeof(block_header); i < ib->m_size; i++) { /* data not compressed, no need to decompress */
            data_block_add(ob, ib->m_data[i]);
        }
        return;
    }
    data_block_reserve(ob, block_header.m_original_size);
    data_block_resize(ob, 1);
    ob->m_data[0] = block_header.m_firstbyte;

    /* configure matcher */
    using_ctx4 = (block_header.m_original_size >= 4194304);
    matcher_init(&matcher);

    input = ib->m_data + sizeof(block_header);
    input_idx = ib->m_data + block_header.m_offset_idx;

    range_decoder_init(&coder, &input);
    range_decoder_init(&idx_coder, &input_idx);

    /* init threads */
    thread_args.m_input_idx = &input_idx;
    thread_args.m_len_queue = len_queue[0];
    thread_args.m_idx_queue = idx_queue[0]; lzdecode_idx_thread(&thread_args);
    thread_args.m_len_queue = len_queue[1];
    thread_args.m_idx_queue = idx_queue[1]; pthread_create(&thread, 0, (void*)lzdecode_idx_thread, &thread_args);

    while(ob->m_size < block_header.m_original_size) {
        if(print_information) {
            update_progress(ob->m_size, block_header.m_original_size);
        }

        if((decode_symbol = ppm_decode(&coder, &m.ppm_model, &input)) == block_header.m_esc) { /* escape */
            if(idx_index >= M_idx_queue_size) { /* decode length (from queue) */
                pthread_join(thread, 0);
                thread_args.m_len_queue = len_queue[idx_n];
                thread_args.m_idx_queue = idx_queue[idx_n];
                pthread_create(&thread, 0, (void*)lzdecode_idx_thread, &thread_args);
                idx_index = 0;
                idx_n = 1 - idx_n;
            }
            match_len = len_queue[idx_n][idx_index];
            match_idx = idx_queue[idx_n][idx_index];
            idx_index++;

            if(match_len == 0) { /* escape character */
                data_block_add(ob, block_header.m_esc);
                match_len = 1;

            } else { /* ROLZ match */
                pos = matcher_getpos(&matcher, match_idx);
                for(i = 0; i < match_len; i++) {
                    data_block_add(ob, ob->m_data[pos + i]);
                }
            }

        } else { /* literal */
            data_block_add(ob, decode_symbol);
            match_len = 1;
        }

        while(match_len > 0) {
            matcher_update(&matcher, ob->m_data, ob->m_size - match_len, 0);
            ppm_update_context(&m.ppm_model, ob->m_data[ob->m_size - match_len]);
            match_len--;
        }
    }
    pthread_join(thread, 0);
    matcher_free(&matcher);
    return;
}
