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
#include "cr-o2model.h"
#include "cr-datablock.h"
#include "cr-ppm.h"

void ppm_model_init(ppm_model_t* model) {
    int i;
    int j;

    memset(model->o2_models, 0, sizeof(model->o2_models));
    memset(model->o1_models, 0, sizeof(model->o1_models));
    for(i = 0; i < 256; i++) {
        for(j = 0; j < 256; j++) {
            model->o1_models[i][j] = 1;
        }
    }
    model->context = 0;
    memset(model->o3_predict, 0, sizeof(model->o3_predict));
    return;
}

void ppm_model_free(ppm_model_t* model) {
    int i;

    for(i = 0; i < 65536; i++) {
        free(model->o2_models[i]);
    }
    memset(model->o2_models, 0, sizeof(model->o2_models));
    return;
}

void ppm_update_context(ppm_model_t* model, int c) {
    model->context <<= 8;
    model->context |= c;
    return;
}

#define M_predctx3_ ((model->context ^ (model->context >> 2)) & 0x3fffff)
#define M_predbase_ (M_predctx3_ + (M_predctx3_ >> 1))

static inline void ppm_update_o3(ppm_model_t* model, int c) {
    int tn = ~M_predctx3_ & 1;
    int pn =  M_predbase_ + 1 + tn;
    int freq = (model->o3_predict[pn] >> (4 * tn)) & 0x0f;

#define M_predbyte_ (model->o3_predict[M_predbase_])

    if(c >= 0) {
        freq = (freq > 1) + (freq > 2) + (freq > 4) + (freq > 8);
        if(freq == 0) {
            M_predbyte_ = c;
            freq = 1;
        }
    } else {
        freq += (freq < 15);
    }
    model->o3_predict[pn] &= (0xf0 >> (4 * tn));
    model->o3_predict[pn] |= (freq << (4 * tn));
    return;
}

static inline void ppm_update_o1(uint8_t* o1, int c) {
    if(++o1[c] >= 255) {
        for(c = 0; c < 256; c++) {
            o1[c] -= o1[c] / 2;
        }
    }
    return;
}
#define M_freq_o1(i) (o1[i]*8-7)

#define M_lastword_ (model->context & 0xffff)
#define M_lastbyte_ (model->context & 0xff)

int ppm_encode(range_coder_t* coder, ppm_model_t* model, int encode_ch, data_block_t* o_block) {
    if(!model->o2_models[M_lastword_]) { /* alloc o2-model */
        model->o2_models[M_lastword_] = malloc(sizeof(o2_model_t));
        o2_model_init(model->o2_models[M_lastword_]);
    }

    int i;
    int cum = 0;
    int sum = 0;
    o2_model_t* o2 = model->o2_models[M_lastword_];
    uint8_t*    o1 = model->o1_models[M_lastbyte_];
    int predict_ch = M_predbyte_;
    int predict_frq = o2_model_frq(o2, predict_ch);
    int rescaled;

    if(encode_ch == predict_ch) { /* short predictor matched */
        range_encoder_encode(coder,
                o2_model_cum(o2, 256) - predict_frq,
                o2_model_frq(o2, 256),
                o2_model_sum(o2) - predict_frq,
                o_block);
        o2_model_update(o2, 256, 1);
        ppm_update_o3(model, -1);

    } else { /* encode with o2-o1 models */
        if(o2_model_frq(o2, encode_ch) > 0) {
            range_encoder_encode(coder,
                    o2_model_cum(o2, encode_ch) - ((encode_ch >= predict_ch) ? predict_frq : 0),
                    o2_model_frq(o2, encode_ch),
                    o2_model_sum(o2) - predict_frq,
                    o_block);
            rescaled = o2_model_update(o2, encode_ch, 1);

            if(!rescaled && o2_model_frq(o2, encode_ch) == 2) { /* ppmx escape estimator */
                o2_model_update(o2, 257, -1);
            }

        } else {
            range_encoder_encode(coder,
                    o2_model_cum(o2, 257) - predict_frq,
                    o2_model_frq(o2, 257),
                    o2_model_sum(o2) - predict_frq,
                    o_block);
            rescaled = o2_model_update(o2, 257, 1);

            if(o1[encode_ch] > 0) {
                /* encode with o1_model, exclude predict_ch and all bytes those appeared in model->o2_models */
                for(i = 0; i < 256; i++) {
                    if(o2_model_frq(o2, i) == 0 && i != predict_ch) {
                        cum += M_freq_o1(i) & -(i < encode_ch);
                        sum += M_freq_o1(i);
                    }
                }
                range_encoder_encode(coder, cum, M_freq_o1(encode_ch), sum, o_block);
                ppm_update_o1(o1, encode_ch);
            }

            if(!rescaled) {
                o2_model_update(o2, encode_ch, 1);
            }
        }
        ppm_update_o3(model, encode_ch);
    }
    return 0;
}

int ppm_decode(range_coder_t* coder, ppm_model_t* model, uint8_t** input) {
    if(!model->o2_models[M_lastword_]) { /* alloc o2-model */
        model->o2_models[M_lastword_] = malloc(sizeof(o2_model_t));
        o2_model_init(model->o2_models[M_lastword_]);
    }

    decode_symbol_t decode_helper;
    int decode_cum;
    int i;
    int decode_symbol;
    int sum = 0;
    int cum = 0;
    o2_model_t* o2 = model->o2_models[M_lastword_];
    uint8_t*    o1 = model->o1_models[M_lastbyte_];
    int predict_ch = M_predbyte_;
    int predict_frq = o2_model_frq(o2, predict_ch);
    int rescaled;

    /* decode with o2_model */
    decode_cum = range_decoder_decode_cum(coder, o2_model_sum(o2) - predict_frq);
    decode_helper = o2_model_get_decode_symbol(o2, decode_cum, predict_ch);
    range_decoder_decode(coder,
            decode_helper.m_cum,
            o2_model_frq(o2, decode_helper.m_sym),
            o2_model_sum(o2),
            input);
    rescaled = o2_model_update(o2, decode_helper.m_sym, 1);
    decode_symbol = decode_helper.m_sym;

    if(decode_symbol == 256) { /* short predictor matched */
        decode_symbol = predict_ch;
        ppm_update_o3(model, -1);

    } else if(decode_symbol < 256) {
        if(!rescaled && o2_model_frq(o2, decode_symbol) == 2) { /* ppmx escape elimator */
            o2_model_update(o2, 257, -1);
        }
        ppm_update_o3(model, decode_symbol);

    } else if(decode_symbol == 257) { /* decode with o1_model, exclude predict_ch and all bytes in model->o2_models */
        for(i = 0; i < 256; i++) {
            if(o2_model_frq(o2, i) == 0 && i != predict_ch) {
                sum += M_freq_o1(i);
            }
        }
        decode_cum = range_decoder_decode_cum(coder, sum);

        /* decode with o1 model */
        for(i = 0; i < 256; i++) {
            if(o2_model_frq(o2, i) == 0 && i != predict_ch) {
                if(cum + M_freq_o1(i) > decode_cum) {
                    decode_symbol = i;
                    break;
                }
                cum += M_freq_o1(i);
            }
        }
        range_decoder_decode(coder, cum, M_freq_o1(decode_symbol), sum, input);
        ppm_update_o1(o1, decode_symbol);

        if(!rescaled) {
            o2_model_update(o2, decode_symbol, 1);
        }
        ppm_update_o3(model, decode_symbol);
    }
    return decode_symbol;
}
