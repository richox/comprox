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
#include "cr-rangecoder.h"
#include "cr-datablock.h"

static const uint32_t top = 1 << 24;
static const uint32_t thresold = 255u * (1 << 24);

void range_encoder_init(range_coder_t* coder) {
    coder->m_low = 0;
    coder->m_range = -1;
    coder->m_follow = 0;
    coder->m_cache = 0;
    coder->m_carry = 0;
    return;
}

static inline void renormalize(range_coder_t* coder, data_block_t* o_block) {
    if(coder->m_low < thresold || coder->m_carry) {
        data_block_add(o_block, coder->m_cache + coder->m_carry);
        while(coder->m_follow > 0) {
            data_block_add(o_block, coder->m_carry - 1);
            coder->m_follow -= 1;
        }
        coder->m_cache = coder->m_low >> 24;
        coder->m_carry = 0;
    } else {
        coder->m_follow += 1;
    }
    coder->m_low *= 256;
    return;
}

void range_encoder_encode(range_coder_t* coder, uint32_t cum, uint32_t frq, uint32_t sum, data_block_t* o_block) {
    coder->m_range /= sum;
    coder->m_carry += (coder->m_low + cum * coder->m_range) < coder->m_low;
    coder->m_low += cum * coder->m_range;
    coder->m_range *= frq;
    while(coder->m_range < top) {
        coder->m_range *= 256;
        renormalize(coder, o_block);
    }
    return;
}

void range_encoder_flush(range_coder_t* coder, data_block_t* o_block) {
    renormalize(coder, o_block);
    renormalize(coder, o_block);
    renormalize(coder, o_block);
    renormalize(coder, o_block);
    renormalize(coder, o_block);
    return;
}

void range_decoder_init(range_coder_t* coder, uint8_t** input) {
    range_encoder_init(coder);
    coder->m_cache = (coder->m_cache * 256) + **input, *input += 1;
    coder->m_cache = (coder->m_cache * 256) + **input, *input += 1;
    coder->m_cache = (coder->m_cache * 256) + **input, *input += 1;
    coder->m_cache = (coder->m_cache * 256) + **input, *input += 1;
    coder->m_cache = (coder->m_cache * 256) + **input, *input += 1;
    return;
}

void range_decoder_decode(range_coder_t* coder, uint32_t cum, uint32_t frq, uint32_t sum, uint8_t** input) {
    coder->m_cache -= cum * coder->m_range;
    coder->m_range *= frq;
    while(coder->m_range < top) {
        coder->m_cache = (coder->m_cache * 256) + **input, *input += 1;
        coder->m_range *= 256;
    }
    return;
}

uint32_t range_decoder_decode_cum(range_coder_t* coder, uint32_t sum) {
    coder->m_range /= sum;
    return coder->m_cache / coder->m_range;
}
