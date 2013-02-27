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

/* increment factor for skew coding */
#define M_inc_factor(i) (1<<(i)<<(i))

/* global models -- for lzencode() and lzdecode() */
static struct {
    ppm_model_t ppm_model;
    model_t len_model;
    model_t pos_models[6];
    model_t spos_model;
} m;

/* global coders -- for lzencode() and lzdecode() */
range_coder_t coder;
range_coder_t coder_pos;
range_coder_t coder_len;
range_coder_t coder_spos;

/* block header fields */
struct {
    uint8_t  m_compressed;
    uint8_t  m_match_min;
    uint8_t  m_esc;
    uint32_t m_original_size;
    uint32_t m_num_spos;
    uint32_t m_num_pos;
    uint32_t m_num_len;
    uint32_t m_offset_spos;
    uint32_t m_offset_pos;
    uint32_t m_offset_len;
}
block_header;

/* common model initializer */
static inline void atexit_free_models() {
    ppm_model_free(&m.ppm_model);
    return;
}
void reset_models() {
    int i;
    int k;
    int register_atexit = 0;

    if(!register_atexit) {
        atexit(atexit_free_models);
        register_atexit = 1;
    }
    ppm_model_free(&m.ppm_model);
    ppm_model_init(&m.ppm_model);

    for(i = 0; i < 5; i++) {        /* init pos models */
        for(k = 0; k < 256; k++) {
            m.pos_models[i].m_frq_table[k] = (i == 0 && k % 8 == 0) || (i > 0 && ((i < 2 && k < 256) || (i < 5 && k < 128)));
        }
        model_recalc_cum(&m.pos_models[i]);
    }
    model_init(&m.pos_models[5]);

    for(k = 0; k < 256; k++) {     /* init len model */
        m.len_model.m_frq_table[k] = (k >= match_min_near && k <= match_max) || (k == 0);
    }
    model_recalc_cum(&m.len_model);
    model_init(&m.spos_model);
    return;
}

/* pthread-callback wrapper */
typedef struct lzmatch_thread_param_pack_t {
    matcher_t*      m_matcher;
    matcher_ret_t*  m_rets;
    data_block_t*   m_iblock;
    uint32_t*       m_pos;
} lzmatch_thread_param_pack_t;

#define M_match_rets_size 32000

static void* lzmatch_thread(lzmatch_thread_param_pack_t* args) { /* thread for finding matches */
    matcher_ret_t ret;
    uint32_t pos = args->m_pos[0];
    uint32_t retindex = 0;
    uint32_t i;

    while(pos < args->m_iblock->m_size && retindex < M_match_rets_size) {
        ret.m_pos = -1;
        ret.m_len = 1;
        if(pos + 1024 < args->m_iblock->m_size) { /* find a match -- avoid overflow */
            ret = matcher_lookup(args->m_matcher, args->m_iblock->m_data, pos);
            for(i = 0; i < ret.m_len; i++) {
                matcher_update_cache(args->m_matcher, args->m_iblock->m_data, pos + i); /* update short cache */
            }
        }
        pos += ret.m_len;
        args->m_rets[retindex++] = ret;
    }

    if(retindex < M_match_rets_size) { /* put an END flag */
        ret.m_pos = -1;
        args->m_rets[retindex++] = ret;
    }
    args->m_pos[0] = pos;
    return NULL;
}

void lzencode(data_block_t* ib, data_block_t* ob, int print_information) {
    data_block_t spos_block = INITIAL_BLOCK;
    data_block_t pos_block = INITIAL_BLOCK;
    data_block_t len_block = INITIAL_BLOCK;
    uint32_t match_pos;
    uint32_t match_len;
    uint32_t pos = 0;
    uint32_t last_match = 0;
    uint32_t i;
    uint32_t j;
    uint32_t counter[256] = {0};
    int      esc = 0;

    matcher_t matcher;
    pthread_t thread;
    lzmatch_thread_param_pack_t thread_args;
    uint32_t match_retindex = 0;
    uint32_t match_retn = 0;
    uint32_t match_nextpos = 0;
    matcher_ret_t match_rets[2][M_match_rets_size];

    /* reserve space for block header */
    data_block_resize(ob, sizeof(block_header));
    block_header.m_num_spos = 0;
    block_header.m_num_pos = 0;
    block_header.m_num_len = 0;

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

    /* adjust match_min by blocksize */
    match_min = 10 + (ib->m_size > 16777216);

    /* init matcher */
    matcher_init(&matcher, ib->m_data, ib->m_size, print_information);

    if(print_information) {
        fprintf(stderr, "%s\n", "-> running LZ77 encoding...");
    }
    range_encoder_init(&coder_spos);
    range_encoder_init(&coder_pos);
    range_encoder_init(&coder_len);
    range_encoder_init(&coder);

    thread_args.m_pos = &match_nextpos;
    thread_args.m_iblock = ib;
    thread_args.m_matcher = &matcher;

    thread_args.m_rets = match_rets[0]; lzmatch_thread(&thread_args);
    thread_args.m_rets = match_rets[1]; pthread_create(&thread, 0, (void*)lzmatch_thread, &thread_args);

    /* start encoding */
    while(pos < ib->m_size) {
        if(print_information) {
            update_progress(pos, ib->m_size);
        }

        if(match_retindex >= M_match_rets_size) { /* start the next matching thread */
            pthread_join(thread, 0);
            thread_args.m_rets = match_rets[match_retn];
            pthread_create(&thread, 0, (void*)lzmatch_thread, &thread_args);
            match_retindex = 0;
            match_retn = 1 - match_retn;
        }
        match_pos = match_rets[match_retn][match_retindex].m_pos;
        match_len = match_rets[match_retn][match_retindex].m_len;
        match_retindex += 1;

        if(match_pos != -1) { /* lz77 match */
            ppm_encode(&coder, &m.ppm_model, esc, ob);

            if(pos - match_pos == last_match) { /* same as last match */
                match_pos = pos;
            }
            M_my_enc_(coder_len, &len_block, m.len_model, match_len, 30);
            block_header.m_num_len += 1;

            if(match_len < match_min) { /* shorter match */
                M_my_enc_(coder_spos, &spos_block, m.spos_model, pos - match_pos, 1);
                block_header.m_num_spos += 1;

            } else { /* encode position into m.pos_models */
                j = (pos - match_pos) * 8;
                i = 0;
                while(j >= 128 && i < 2) {
                    M_my_enc_(coder_pos, &pos_block, m.pos_models[i], j % 128 + 128, M_inc_factor(i));
                    i += 1;
                    j /= 128;
                }
                if(i >= 2) {
                    while(j >= 64 && i < 5) {
                        M_my_enc_(coder_pos, &pos_block, m.pos_models[i], j % 64 + 64, M_inc_factor(i));
                        i += 1;
                        j /= 64;
                    }
                }
                M_my_enc_(coder_pos, &pos_block, m.pos_models[i], j, M_inc_factor(i));
                block_header.m_num_pos += 1;
            }
            last_match = pos - match_pos;

        } else { /* literal */
            ppm_encode(&coder, &m.ppm_model, ib->m_data[pos], ob);
            if(ib->m_data[pos] == esc) {
                M_my_enc_(coder_len, &len_block, m.len_model, 0, 30);
                block_header.m_num_len += 1;
            }
        }

        for(i = 0; i < match_len; i++) { /* update context */
            ppm_update_context(&m.ppm_model, ib->m_data[pos++]);
        }
        if(ob->m_size >= ib->m_size) { /* cannot compress */
            goto CannotCompress;
        }
    }

    range_encoder_flush(&coder, ob);
    range_encoder_flush(&coder_spos, &spos_block);
    range_encoder_flush(&coder_pos, &pos_block);
    range_encoder_flush(&coder_len, &len_block);

    pthread_join(thread, 0);
    matcher_free(&matcher);

    /* set block header */
    block_header.m_compressed = 1;
    block_header.m_original_size = ib->m_size;
    block_header.m_match_min = match_min;
    block_header.m_offset_spos = ob->m_size;
    block_header.m_offset_pos = ob->m_size + spos_block.m_size;
    block_header.m_offset_len = ob->m_size + spos_block.m_size + pos_block.m_size;
    memcpy(ob->m_data, &block_header, sizeof(block_header));

    /* append extra blocks to ob */
    data_block_resize(ob, ob->m_size + spos_block.m_size + pos_block.m_size + len_block.m_size);
    memcpy(ob->m_data + block_header.m_offset_spos, spos_block.m_data, spos_block.m_size);
    memcpy(ob->m_data + block_header.m_offset_pos,  pos_block.m_data, pos_block.m_size);
    memcpy(ob->m_data + block_header.m_offset_len,  len_block.m_data, len_block.m_size);
    data_block_destroy(&spos_block);
    data_block_destroy(&pos_block);
    data_block_destroy(&len_block);
    return;

CannotCompress:
    pthread_join(thread, 0);
    matcher_free(&matcher);

    data_block_resize(ob, sizeof(block_header) + ib->m_size);
    memset(ob->m_data, 0, sizeof(block_header));
    for(i = 0; i < ib->m_size; i++) {
        ob->m_data[sizeof(block_header) + i] = ib->m_data[i];
    }
    data_block_destroy(&spos_block);
    data_block_destroy(&pos_block);
    data_block_destroy(&len_block);
    return;
}

/* pthread-callback wrapper */
typedef struct lzdecode_thread_param_pack_t {
    uint32_t* m_spos_queue;
    uint32_t* m_pos_queue;
    uint32_t* m_len_queue;
    uint8_t** m_input_spos;
    uint8_t** m_input_pos;
    uint8_t** m_input_len;
} lzdecode_thread_param_pack_t;

#define M_spos_queue_size   24000
#define M_pos_queue_size    12000
#define M_len_queue_size    24000

static void* lzdecode_spos_thread(lzdecode_thread_param_pack_t* args) { /* thread for decoding spos */
    decode_symbol_t decode_helper;
    uint32_t i;

    /* decode spos */
    for(i = 0; block_header.m_num_spos > 0 && i < M_spos_queue_size; i++) {
        block_header.m_num_spos--;
        args->m_spos_queue[i] = M_my_dec_(coder_spos, *args->m_input_spos, m.spos_model, 1);
    }
    return 0;
}

static void* lzdecode_pos_thread(lzdecode_thread_param_pack_t* args) { /* thread for decoding pos */
    decode_symbol_t decode_helper;
    uint32_t i;
    uint32_t j;
    uint32_t v;
    uint32_t decode_symbol;

    /* decode pos */
    for(i = 0; block_header.m_num_pos > 0 && i < M_pos_queue_size; i++) {
        block_header.m_num_pos--;
        j = 0;
        v = 0;
        while(j < 2 && (decode_symbol = M_my_dec_(coder_pos, *args->m_input_pos, m.pos_models[j], M_inc_factor(j))) >= 128) {
            v += (decode_symbol - 128) * (1 << (7 * j));
            j += 1;
        }
        if(j < 2) {
            args->m_pos_queue[i] = v + decode_symbol * (1 << (7 * j));
            args->m_pos_queue[i] /= 8;
            continue;
        }

        while(j < 5 && (decode_symbol = M_my_dec_(coder_pos, *args->m_input_pos, m.pos_models[j], M_inc_factor(j))) >= 64) {
            v += (decode_symbol - 64) * (1 << ((6 * j) + 2));
            j += 1;
        }
        args->m_pos_queue[i] = v + decode_symbol * (1 << ((6 * j) + 2));
        args->m_pos_queue[i] /= 8;
    }
    return 0;
}

static void* lzdecode_len_thread(lzdecode_thread_param_pack_t* args) { /* thread for decoding len */
    decode_symbol_t decode_helper;
    uint32_t i;

    /* decode len */
    for(i = 0; block_header.m_num_len > 0 && i < M_len_queue_size; i++) {
        block_header.m_num_len--;
        args->m_len_queue[i] = M_my_dec_(coder_len, *args->m_input_len, m.len_model, 30);
    }
    return 0;
}

void lzdecode(data_block_t* ib, data_block_t* ob, int print_information) {
    uint32_t last_match = 0;
    uint32_t i;
    uint32_t decode_symbol;
    uint8_t* input;
    uint8_t* input_spos;
    uint8_t* input_pos;
    uint8_t* input_len;

    pthread_t thread;
    lzdecode_thread_param_pack_t thread_args;
    uint32_t spos_index = 0;
    uint32_t pos_index = 0;
    uint32_t len_index = 0;
    uint32_t spos_n = 0;
    uint32_t pos_n = 0;
    uint32_t len_n = 0;
    uint32_t spos_queue[2][M_spos_queue_size];
    uint32_t pos_queue[2][M_pos_queue_size];
    uint32_t len_queue[2][M_len_queue_size];

    /* represent a decoded code
     *  len > 1: match
     *  len = 1: lit; pos = literal
     */
    uint32_t match_len = 0;
    uint32_t match_pos = 0;

    if(print_information) {
        fprintf(stderr, "%s\n", "-> running LZ77 decoding...");
    }
    memcpy(&block_header, ib->m_data, sizeof(block_header));
    data_block_resize(ob, 0);
    data_block_reserve(ob, block_header.m_original_size);

    if(!block_header.m_compressed) {
        for(i = sizeof(block_header); i < ib->m_size; i++) { /* data not compressed, no need to decompress */
            data_block_add(ob, ib->m_data[i]);
        }
        return;
    }
    input = ib->m_data + sizeof(block_header);
    input_spos = ib->m_data + block_header.m_offset_spos;
    input_pos = ib->m_data + block_header.m_offset_pos;
    input_len = ib->m_data + block_header.m_offset_len;

    /* get match_min from header */
    match_min = block_header.m_match_min;

    range_decoder_init(&coder, &input);
    range_decoder_init(&coder_spos, &input_spos);
    range_decoder_init(&coder_pos, &input_pos);
    range_decoder_init(&coder_len, &input_len);

    /* init threads */
    thread_args.m_input_spos = &input_spos;
    thread_args.m_input_pos = &input_pos;
    thread_args.m_input_len = &input_len;

    /* decode first block */
    thread_args.m_pos_queue = pos_queue[0];     pthread_create(&thread, 0, (void*)lzdecode_pos_thread, &thread_args);
    thread_args.m_len_queue = len_queue[0];     lzdecode_len_thread(&thread_args);
    thread_args.m_spos_queue = spos_queue[0];   lzdecode_spos_thread(&thread_args);
    pthread_join(thread, 0);
    thread_args.m_pos_queue = pos_queue[1];     pthread_create(&thread, 0, (void*)lzdecode_pos_thread, &thread_args);
    thread_args.m_len_queue = len_queue[1];     lzdecode_len_thread(&thread_args);
    thread_args.m_spos_queue = spos_queue[1];   lzdecode_spos_thread(&thread_args);

    /* start decoding */
    while(ob->m_size < block_header.m_original_size) {
        if(print_information) {
            update_progress(ob->m_size, block_header.m_original_size);
        }

        decode_symbol = ppm_decode(&coder, &m.ppm_model, &input);

        if(decode_symbol != block_header.m_esc) {
            match_len = 1;
            match_pos = decode_symbol;
        } else {
            if(len_index >= M_len_queue_size) { /* decode length (from queue) */
                pthread_join(thread, 0);
                thread_args.m_len_queue = len_queue[len_n];
                pthread_create(&thread, 0, (void*)lzdecode_len_thread, &thread_args);
                len_index = 0;
                len_n = 1 - len_n;
            }
            match_len = len_queue[len_n][len_index++];

            if(match_len == 0) { /* escape char literal */
                match_len = 1;
                match_pos = block_header.m_esc;

            } else if(match_len < match_min) {
                if(spos_index >= M_spos_queue_size) { /* decode shorter match position (from queue) */
                    pthread_join(thread, 0);
                    thread_args.m_spos_queue = spos_queue[spos_n];
                    pthread_create(&thread, 0, (void*)lzdecode_spos_thread, &thread_args);
                    spos_index = 0;
                    spos_n = 1 - spos_n;
                }
                i = spos_queue[spos_n][spos_index++];

            } else { /* decode position with m.pos_models */
                if(pos_index >= M_pos_queue_size) { /* decode match position (from queue) */
                    pthread_join(thread, 0);
                    thread_args.m_pos_queue = pos_queue[pos_n];
                    pthread_create(&thread, 0, (void*)lzdecode_pos_thread, &thread_args);
                    pos_index = 0;
                    pos_n = 1 - pos_n;
                }
                i = pos_queue[pos_n][pos_index++];
            }

            if(match_len > 1) {
                match_pos = ob->m_size - ((i > 0) ? i : last_match);
                last_match = ob->m_size - match_pos;
            }
        }

        /* apply a code */
        data_block_resize(ob, ob->m_size + match_len);
        if(match_len > 1) { /* match */
            for(i = 0; i < match_len; i++) {
                ob->m_data[ob->m_size - match_len + i] = ob->m_data[match_pos + i];
            }
        } else { /* literal */
            ob->m_data[ob->m_size - 1] = match_pos;
        }

        for(i = 0; i < match_len; i++) { /* update context */
            ppm_update_context(&m.ppm_model, ob->m_data[ob->m_size - match_len + i]);
        }
    }
    pthread_join(thread, 0);
    return;
}
