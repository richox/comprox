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
#include "cr-model.h"

void model_init(model_t* model) {
    int i;

    for(i = 0; i < 256; i++) {
        model->m_frq_table[i] = 1;
    }
    model_recalc_cum(model);
    return;
}

void model_recalc_cum(model_t* model) {
    int cum = 0;
    int i;

    for(i = 0; i < 256; i++) {
        if(i % 32 == 0) {
            model->m_cum_table[i / 32] = cum;
        }
        cum += model->m_frq_table[i];
    }
    model->m_cum_table[8] = cum;
    return;
}

int model_update(model_t* model, int symbol, int increment) {
    int cum_base = symbol / 32;
    int cum = 0;
    int i;

    model->m_frq_table[symbol] += increment;
    for(i = cum_base + 1; i <= 8; i++) {
        model->m_cum_table[i] += increment;
    }

    if(model_sum(model) > 32000) {
        for(i = 0; i < 256; i++) {
            model->m_frq_table[i] = (model->m_frq_table[i] + 1) / 2;
            if(i % 32 == 0) {
                model->m_cum_table[i / 32] = cum;
            }
            cum += model->m_frq_table[i];
        }
        model->m_cum_table[8] = cum;
        return 1;
    }
    return 0;
}

int model_cum(model_t* model, int symbol) {
    int cum_base = symbol / 32;
    int i;
    int cum = model->m_cum_table[cum_base];

    for(i = cum_base * 32; i < symbol; i++) {
        cum += model->m_frq_table[i];
    }
    return cum;
}

int model_frq(model_t* model, int symbol) {
    return model->m_frq_table[symbol];
}

int model_sum(model_t* model) {
    return model->m_cum_table[8];
}

decode_symbol_t model_get_decode_symbol(model_t* model, int cum) {
    decode_symbol_t ret;
#define M(s, a, b) (model->m_cum_table[s] <= cum ? (a):(b))

    ret.m_sym = 32 * M(4,
            M(6,M(7,  7,  6),
                M(5,  5,  4)),
            M(2,M(3,  3,  2),
                M(1,  1,  0)));
    ret.m_cum = model->m_cum_table[ret.m_sym / 32];
#undef M

    while(ret.m_cum + model->m_frq_table[ret.m_sym] <= cum) {
        ret.m_cum += model->m_frq_table[ret.m_sym];
        ret.m_sym += 1;
    }
    return ret;
}
