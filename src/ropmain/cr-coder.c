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
    ppm_model_t ppm_model;
} m;

/* global coders -- for lzencode() and lzdecode() */
range_coder_t coder;

/* block header fields */
struct {
    uint8_t  m_compressed;
    uint32_t m_original_size;
    uint8_t  m_esc;
    uint8_t  m_firstbytes[9];
}
block_header;

/* common model initializer */
static inline void atexit_free_models() {
    ppm_model_free(&m.ppm_model);
    return;
}
void reset_models() {
    int register_atexit = 0;

    if(!register_atexit) {
        atexit(atexit_free_models);
        register_atexit = 1;
    }
    ppm_model_free(&m.ppm_model);
    ppm_model_init(&m.ppm_model);
    return;
}

/* pthread-callback wrapper */
typedef struct lzmatch_thread_param_pack_t {
    matcher_t*      m_matcher;
    data_block_t*   m_iblock;
    uint32_t*       m_lens;
    uint32_t*       m_pos;
} lzmatch_thread_param_pack_t;

#define M_match_rets_size 32000

static void* lzmatch_thread(lzmatch_thread_param_pack_t* args) { /* thread for finding matches */
    uint32_t match_len;
    uint32_t pos = args->m_pos[0];
    uint32_t retindex = 0;
    uint32_t i;

    while(pos < args->m_iblock->m_size && retindex < M_match_rets_size) {
        match_len = 1;
        if(pos + 1024 < args->m_iblock->m_size) { /* find a match -- avoid overflow */
            match_len = matcher_lookup(args->m_matcher, args->m_iblock->m_data, pos);
            for(i = 0; i < match_len; i++) {
                matcher_update(args->m_matcher, args->m_iblock->m_data, pos + i);
            }
        }
        pos += match_len;
        args->m_lens[retindex++] = match_len;
    }

    if(retindex < M_match_rets_size) { /* put an END flag */
        args->m_lens[retindex++] = 0;
    }
    args->m_pos[0] = pos;
    return NULL;
}
void lzencode(data_block_t* ib, data_block_t* ob, int print_information) {
    matcher_t matcher;
    uint32_t  match_len;
    uint32_t  i;
    uint32_t  pos;
    uint32_t  counter[256] = {0};
    int       esc = 0;

    lzmatch_thread_param_pack_t thread_args;
    pthread_t thread;
    uint32_t  match_retindex = 0;
    uint32_t  match_retn = 0;
    uint32_t  match_nextpos = 0;
    uint32_t  match_lens[2][M_match_rets_size];

    if(print_information) {
        fprintf(stderr, "%s\n", "-> running LZP/ARI encoding...");
    }

    /* reserve space for block header */
    data_block_resize(ob, sizeof(block_header));
    if(ib->m_size < 16) {
        goto CannotCompress_nojoin_nofree;
    }
    for(pos = 0; pos < 9; pos++) {
        block_header.m_firstbytes[pos] = ib->m_data[pos];
    }

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

    matcher_init(&matcher);
    range_encoder_init(&coder);

    /* start thread (matching first block) */
    match_nextpos = pos;
    thread_args.m_pos = &match_nextpos;
    thread_args.m_iblock = ib;
    thread_args.m_matcher = &matcher;
    thread_args.m_lens = match_lens[0]; lzmatch_thread(&thread_args);
    thread_args.m_lens = match_lens[1]; pthread_create(&thread, 0, (void*)lzmatch_thread, &thread_args);

    while(pos < ib->m_size) {
        if(print_information) {
            update_progress(pos, ib->m_size);
        }

        /* find match */
        if(match_retindex >= M_match_rets_size) { /* start the next matching thread */
            pthread_join(thread, 0);
            thread_args.m_lens = match_lens[match_retn];
            pthread_create(&thread, 0, (void*)lzmatch_thread, &thread_args);
            match_retindex = 0;
            match_retn = 1 - match_retn;
        }
        match_len = match_lens[match_retn][match_retindex++];

        /* encode a (esc+len) or a single literal */
        if(match_len > 1) {
            ppm_encode(&coder, &m.ppm_model, esc, ob);
            ppm_update_context(&m.ppm_model, esc);
            ppm_encode(&coder, &m.ppm_model, match_len, ob);

        } else {
            ppm_encode(&coder, &m.ppm_model, ib->m_data[pos], ob);
            if(ib->m_data[pos] == esc) {
                ppm_update_context(&m.ppm_model, esc);
                ppm_encode(&coder, &m.ppm_model, 0, ob);
            }
        }

        while(match_len > 0) { /* update context */
            ppm_update_context(&m.ppm_model, ib->m_data[pos]);
            pos++;
            match_len--;
        }

        if(ob->m_size >= ib->m_size) { /* cannot compress */
            goto CannotCompress;
        }
    }
    pthread_join(thread, 0);
    matcher_free(&matcher);
    range_encoder_flush(&coder, ob);

    /* set block header */
    block_header.m_compressed = 1;
    block_header.m_original_size = ib->m_size;
    memcpy(ob->m_data, &block_header, sizeof(block_header));
    return;

CannotCompress:
    pthread_join(thread, NULL);
    matcher_free(&matcher);

CannotCompress_nojoin_nofree:
    data_block_resize(ob, sizeof(block_header) + ib->m_size);
    memset(ob->m_data, 0, sizeof(block_header));
    for(i = 0; i < ib->m_size; i++) {
        ob->m_data[sizeof(block_header) + i] = ib->m_data[i];
    }
    return;
}

void lzdecode(data_block_t* ib, data_block_t* ob, int print_information) {
    uint32_t        match_pos;
    uint32_t        match_len;
    uint32_t        i;
    unsigned char*  input;
    matcher_t       matcher;
    uint32_t        decode_symbol;

    if(print_information) {
        fprintf(stderr, "%s\n", "-> running LZP/ARI decoding...");
    }

    memcpy(&block_header, ib->m_data, sizeof(block_header));
    if(!block_header.m_compressed) {
        for(i = sizeof(block_header); i < ib->m_size; i++) { /* data not compressed, no need to decompress */
            data_block_add(ob, ib->m_data[i]);
        }
        return;
    }
    data_block_reserve(ob, block_header.m_original_size);

    data_block_resize(ob, 9);
    for(i = 0; i < 9; i++) {
        ob->m_data[i] = block_header.m_firstbytes[i];
    }
    input = ib->m_data + sizeof(block_header);
    matcher_init(&matcher);
    range_decoder_init(&coder, &input);

    while(ob->m_size < block_header.m_original_size) {
        if(print_information) {
            update_progress(ob->m_size, block_header.m_original_size);
        }

        match_len = 1;
        decode_symbol = ppm_decode(&coder, &m.ppm_model, &input);

        if(decode_symbol != block_header.m_esc) { /* literal */
            data_block_add(ob, decode_symbol);
        } else {
            ppm_update_context(&m.ppm_model, decode_symbol);
            match_len = ppm_decode(&coder, &m.ppm_model, &input);

            if(match_len == 0) { /* escape? */
                match_len = 1;
                data_block_add(ob, block_header.m_esc);
            } else { /* match */
                match_pos = matcher_getpos(&matcher, ob->m_data, ob->m_size);
                for(i = 0; i < match_len; i++) {
                    data_block_add(ob, ob->m_data[match_pos + i]);
                }
            }
        }

        while(match_len > 0) {
            ppm_update_context(&m.ppm_model, ob->m_data[ob->m_size - match_len]);
            matcher_update(&matcher, ob->m_data, ob->m_size - match_len);
            match_len--;
        }
    }
    return;
}
