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
#ifndef HEADER_CR_MODEL_H
#define HEADER_CR_MODEL_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

typedef struct decode_symbol_t {
    int m_sym;
    int m_cum;
} decode_symbol_t;

typedef struct model_t {
    uint16_t m_frq_table[256];
    uint16_t m_cum_table[9];
} model_t;

void model_init(model_t* model);
void model_recalc_cum(model_t* model);

int model_update(model_t* model, int symbol, int32_t increment);

int model_cum(model_t* model, int symbol);
int model_frq(model_t* model, int symbol);
int model_sum(model_t* model);
decode_symbol_t model_get_decode_symbol(model_t* model, int cum);

/* cooperation with range coder */
#define M_my_enc_(coder, o_block, model, symbol, update) \
    (range_encoder_encode(&coder, \
                          model_cum(&(model), (symbol)), \
                          model_frq(&(model), (symbol)), \
                          model_sum(&(model)), \
                          o_block), \
     (update) && (model_update(&(model), (symbol), (update)), 0))

#define M_my_dec_(coder, input, model, update) \
    (decode_helper.m_cum = range_decoder_decode_cum(&coder, model_sum(&(model))), \
     decode_helper = model_get_decode_symbol(&model, decode_helper.m_cum), \
     range_decoder_decode(&coder, \
         decode_helper.m_cum, \
         model_frq(&(model), decode_helper.m_sym), \
         model_sum(&(model)), &input), \
     (void)((update) && (model_update(&(model), decode_helper.m_sym, (update)), 0)), \
     decode_helper.m_sym)

#endif
