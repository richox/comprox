/*
 * Author: Zhang Li
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../cr-datablock.h"
#include "../cr-ppm.h"
#include "../cr-rangecoder.h"

int main(int argc, char** argv) {
    data_block_t ib = INITIAL_BLOCK;
    data_block_t ob = INITIAL_BLOCK;
    range_coder_t coder;
    static ppm_model_t model;
    int i;
    int c = 0;
    unsigned char* p;

    data_block_reserve(&ib, 1048576);
    data_block_reserve(&ob, 1048576);
    ppm_model_init(&model);

    if(argc == 2 && strcmp(argv[1], "e") == 0) {
        while((ib.m_size = fread(ib.m_data, 1, ib.m_capacity, stdin)) > 0) {
            ob.m_size = 0;
            range_encoder_init(&coder);

            for(i = 0; i < ib.m_size; i++) {
                ppm_encode(&coder, &model, ib.m_data[i], &ob);
                ppm_update_context(&model, ib.m_data[i]);
            }
            range_encoder_flush(&coder, &ob);
            fwrite(&ib.m_size, 1, sizeof(ib.m_size), stdout);
            fwrite(&ob.m_size, 1, sizeof(ob.m_size), stdout);
            fwrite( ob.m_data, 1, ob.m_size, stdout);
        }

        for(i = 0; i < 65536; i++) {
            c += (model.o2_models[i] != NULL);
        }
        fprintf(stderr, "ppm-o2 %d of 65536 nodes used.\n", c);
    }
    if(argc == 2 && strcmp(argv[1], "d") == 0) {
        while(fread(&ib.m_size, 1, sizeof(ib.m_size), stdin) > 0 && fread(&ob.m_size, 1, sizeof(ob.m_size), stdin) > 0) {
            fread(ob.m_data, 1, ob.m_size, stdin);
            p = ob.m_data;
            range_decoder_init(&coder, &p);

            for(i = 0; i < ib.m_size; i++) {
                ib.m_data[i] = ppm_decode(&coder, &model, &p);
                ppm_update_context(&model, ib.m_data[i]);
            }
            fwrite(ib.m_data, 1, ib.m_size, stdout);
        }
    }
    data_block_destroy(&ib);
    data_block_destroy(&ob);
    ppm_model_free(&model);
    return 0;
}
