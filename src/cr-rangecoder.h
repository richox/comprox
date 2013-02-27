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
#ifndef HEADER_CR_RANGECODE_H
#define HEADER_CR_RANGECODE_H

#include <stdlib.h>
#include <stdint.h>

struct data_block_t;

typedef struct range_coder_struct {
    uint32_t m_low;
    uint32_t m_range;
    uint32_t m_follow;
    uint32_t m_carry;
    uint32_t m_cache;
} range_coder_t;

void range_encoder_init(range_coder_t* coder);
void range_encoder_encode(range_coder_t* coder, uint32_t cum, uint32_t frq, uint32_t sum, struct data_block_t* o_block);
void range_encoder_flush(range_coder_t* coder, struct data_block_t* o_block);

void range_decoder_init(range_coder_t* coder, uint8_t** input);
void range_decoder_decode(range_coder_t* coder, uint32_t cum, uint32_t frq, uint32_t sum, uint8_t** input);
uint32_t range_decoder_decode_cum(range_coder_t* coder, uint32_t sum);

#endif
