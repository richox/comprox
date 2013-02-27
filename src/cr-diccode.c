/*
 * Copyright (C) 2011 by Zhang Li <RichSelian at gmail.com>
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
#include "cr-diccode.h"
#include "cr-datablock.h"
#include "miniport-thread.h"

static char    dic[TOTAL_WORD_NUM][WORD_MAXLEN + 2]; /* most words terminated with \x20\x0 */
static uint8_t dicstrlen[TOTAL_WORD_NUM]; /* WORD_MAXLEN + 2 < 256 */
static int     dic_len;

typedef struct trie_node_t {
    int m_id;
    int m_next[128];
} trie_node_t;

static trie_node_t* mem_nodes;
static uint32_t nnode;
static uint32_t nword;
static uint32_t ncapacity;

static inline void dictionary_add_word(const char* word) {
    uint32_t node = 0;
    uint32_t i = 0;
    unsigned char ch;

    while(word[i] != 0) {
        ch = word[i];
        if(mem_nodes[node].m_next[ch] == 0) {
            if(nnode >= ncapacity) { /* allocate more nodes */
                ncapacity = nnode * 1.33 + 1;
                mem_nodes = realloc(mem_nodes, ncapacity * sizeof(trie_node_t));
            }
            memset(mem_nodes + nnode, 0, sizeof(trie_node_t));
            mem_nodes[node].m_id = -1;
            mem_nodes[node].m_next[ch] = nnode++;
        }
        if(ch != 0) {
            node = mem_nodes[node].m_next[ch];
            i += 1;
        }
    }
    mem_nodes[node].m_id = nword++;
    return;
}

static inline void atexit_free_nodes() {
    free(mem_nodes);
}

int dictionary_load(const char* dicstr, int init_trie) { /* return number of words */
    int len = strlen(dicstr);
    int i;
    int p = 0;

    /* fill dictionary */
    for(i = 0; i < len; i++) {
        if(dicstr[i] == '\n') {
            if(isalpha(dic[dic_len][p - 1])) { /* terminate a normal word by \x20\x00 */
                dic[dic_len][p++] = '\x20';
                dic[dic_len][p++] = '\x00';
            }
            p = 0;
            dic_len++;
        } else {
            dic[dic_len][p++] = dicstr[i];
        }
    }

    /* init dictionary trie */
    if(init_trie) {
        mem_nodes = calloc(4096, sizeof(trie_node_t));
        nnode = 1; /* for root */
        nword = 0;
        ncapacity = 4096;
        atexit(atexit_free_nodes);

        for(i = 0; i < dic_len; i++) { /* init with static dicionary */
            dictionary_add_word(dic[i]);
        }

        for(i = 'A'; i < 'Z'; i++) { /* link uppercase leading words */
            mem_nodes[0].m_next[i] = mem_nodes[0].m_next[tolower(i)];
        }
        for(i = 0; i < nnode; i++) { /* link words ended with [';' ':' ',' '.'] */
            if(mem_nodes[i].m_next[' '] > 0) {
                if(!mem_nodes[i].m_next['.']) mem_nodes[i].m_next['.'] = mem_nodes[i].m_next[' '];
                if(!mem_nodes[i].m_next[',']) mem_nodes[i].m_next[','] = mem_nodes[i].m_next[' '];
                if(!mem_nodes[i].m_next[':']) mem_nodes[i].m_next[':'] = mem_nodes[i].m_next[' '];
                if(!mem_nodes[i].m_next[';']) mem_nodes[i].m_next[';'] = mem_nodes[i].m_next[' '];
            }
        }
    }
    return nword;
}

/* pthread-callback wrapper */
typedef struct dictionary_encode_param_pack_t {
    unsigned char* m_data;
    uint32_t m_size;
    uint8_t  m_esc[10];
    data_block_t* m_oblock;
} dictionary_encode_param_pack_t;

static void dictionary_encode_imp(unsigned char* data, uint32_t size, uint8_t esc[10], data_block_t* ob);
static void dictionary_decode_imp(unsigned char* data, uint32_t size, uint8_t esc[10], data_block_t* ob);

static void* dictionary_encode_imp_thread(dictionary_encode_param_pack_t* args) {
    dictionary_encode_imp(args->m_data, args->m_size, args->m_esc, args->m_oblock);
    return 0;
}
static void* dictionary_decode_imp_thread(dictionary_encode_param_pack_t* args) {
    dictionary_decode_imp(args->m_data, args->m_size, args->m_esc, args->m_oblock);
    return 0;
}

void dictionary_encode(data_block_t* ib, data_block_t* ob) {
    uint32_t size1 = ib->m_size / 2;
    uint32_t size2 = ib->m_size - size1;
    data_block_t ob1 = INITIAL_BLOCK;
    data_block_t ob2 = INITIAL_BLOCK;
    pthread_t thread1;
    pthread_t thread2;
    dictionary_encode_param_pack_t args1;
    dictionary_encode_param_pack_t args2;
    uint32_t counter[256] = {0};
    uint32_t i;
    uint32_t j;
    uint32_t pos = 0;
    uint8_t  esc[10] = {0};

    fprintf(stderr, "%s\n", "-> running static dictionary encoding...");
    data_block_resize(ob, 0);

    /* find esc symbol */
    for(i = 0; i < ib->m_size; i++) {
        counter[ib->m_data[i]] += 1;
    }
    for(i = 0; i < sizeof(esc); i++) {
        for(j = 0; j < 256; j++) {
            if(counter[j] < counter[esc[i]]) {
                esc[i] = j;
            }
        }
        counter[esc[i]] = -1;
    }

    while(pos < ib->m_size) {
        data_block_resize(&ob1, 0);
        data_block_resize(&ob2, 0);

        size1 = (pos + 1000000 < ib->m_size) ? 1000000 : (ib->m_size - pos);
        pos += size1;
        size2 = (pos + 1000000 < ib->m_size) ? 1000000 : (ib->m_size - pos);
        pos += size2;

        /* thread 1 for first half */
        args1.m_data = ib->m_data + pos - size2 - size1;
        args1.m_size = size1;
        args1.m_oblock = &ob1;
        memcpy(args1.m_esc, esc, sizeof(args1.m_esc));
        pthread_create(&thread1, 0, (void*)dictionary_encode_imp_thread, &args1);

        /* thread 2 for second half */
        args2.m_data = ib->m_data + pos - size2;
        args2.m_size = size2;
        args2.m_oblock = &ob2;
        memcpy(args2.m_esc, esc, sizeof(args2.m_esc));
        pthread_create(&thread2, 0, (void*)dictionary_encode_imp_thread, &args2);

        pthread_join(thread1, 0);
        pthread_join(thread2, 0);

        /* merge two output blocks */
        size1 = ob1.m_size;
        size2 = ob2.m_size;
        data_block_resize(ob, ob->m_size + size1 + size2 + 8);
        memcpy(ob->m_data + ob->m_size - 8 - size1 - size2,   &size1, 4);
        memcpy(ob->m_data + ob->m_size - 4 - size1 - size2,   &size2, 4);
        memcpy(ob->m_data + ob->m_size - size1 - size2,       ob1.m_data, size1);
        memcpy(ob->m_data + ob->m_size - size2,               ob2.m_data, size2);
    }
    for(i = 0; i < sizeof(esc); i++) { /* write esc chars */
        data_block_add(ob, esc[i]);
    }
    data_block_add(ob, 1); /* write compressible flag */

    if(ob->m_size >= ib->m_size) { /* cannot compress */
        data_block_resize(ob, ib->m_size);
        memcpy(ob->m_data, ib->m_data, ib->m_size);
        data_block_add(ob, 0);
    }
    data_block_destroy(&ob1);
    data_block_destroy(&ob2);
    return;
}

void dictionary_decode(data_block_t* ib, data_block_t* ob, FILE* fpout_sync) {
    uint32_t size1;
    uint32_t size2;
    uint8_t  esc[10];
    data_block_t ob1 = INITIAL_BLOCK;
    data_block_t ob2 = INITIAL_BLOCK;
    pthread_t thread1;
    pthread_t thread2;
    dictionary_encode_param_pack_t args1;
    dictionary_encode_param_pack_t args2;
    uint32_t pos = 0;

    fprintf(stderr, "%s\n", "-> running static dictionary decoding...");

    if(ib->m_data[ib->m_size - 1] == 0) { /* not compressed */
        data_block_resize(ob, ib->m_size - 1);
        memcpy(ob->m_data, ib->m_data, ib->m_size - 1);
        return;
    }

    /* extract esc chars */
    memcpy(esc, ib->m_data + ib->m_size - (sizeof(esc) + 1), sizeof(esc));

    while(pos + sizeof(esc) + 1 < ib->m_size) { /* last chars are <esc[], compressible> */
        data_block_resize(&ob1, 0);
        data_block_resize(&ob2, 0);

        size1 = *(uint32_t*)(ib->m_data + pos);
        size2 = *(uint32_t*)(ib->m_data + pos + 4);
        pos += 8 + size1 + size2;

        args1.m_data = ib->m_data + pos - size2 - size1;
        args1.m_size = size1;
        args1.m_oblock = &ob1;
        memcpy(args1.m_esc, esc, sizeof(args1.m_esc));
        pthread_create(&thread1, 0, (void*)dictionary_decode_imp_thread, &args1);

        args2.m_data = ib->m_data + pos - size2;
        args2.m_size = size2;
        args2.m_oblock = &ob2;
        memcpy(args2.m_esc, esc, sizeof(args2.m_esc));
        pthread_create(&thread2, 0, (void*)dictionary_decode_imp_thread, &args2);

        pthread_join(thread1, 0);
        pthread_join(thread2, 0);

        size1 = ob1.m_size;
        size2 = ob2.m_size;
        data_block_resize(ob, ob->m_size + size1 + size2);
        memcpy(ob->m_data + ob->m_size - size2 - size1,   ob1.m_data, size1);
        memcpy(ob->m_data + ob->m_size - size2,           ob2.m_data, size2);

        if(fpout_sync) { /* move decoded data from memory to file, so can we reduce memory usage */
            fwrite(ob->m_data, 1, ob->m_size, fpout_sync);
            data_block_resize(ob, 0);
        }
    }
    data_block_destroy(&ob1);
    data_block_destroy(&ob2);
    return;
}

static void dictionary_encode_imp(unsigned char* data, uint32_t size, uint8_t esc[10], data_block_t* ob) {
    trie_node_t* node;
    int i;
    int j;
    int reverse_case;
    int enddot;
    int endcomma;
    int endsemic;
    int endcolon;
    int escchar;
    uint8_t escmap[256] = {0};

    for(i = 0; i < 10; i++) { /* init escape map */
        escmap[esc[i]] = i + 1;
    }
    for(i = 0; i + WORD_MAXLEN * 2 < size; i++) { /* avoid overflow */
        j = i;
        node = mem_nodes;

        /* match word in trie */
        if(i > 0 && isalpha(data[i]) && !isalpha(data[i - 1])) {
            while(data[j] < 128 && (node = mem_nodes + node->m_next[data[j]]) != mem_nodes && node->m_id == -1) {
                j += 1;
            }
        } else {
            node = mem_nodes; /* skip non-words */
        }

#define M_check_reverse_case(s,i) ((i)>=3 && (s)[(i)-1]==' ' && ((s)[(i)-2]=='.' || ((s)[(i)-2]==' ' && (s)[(i)-3]=='.')))

        reverse_case = (isupper(data[i]) != 0) ^ M_check_reverse_case(data, i);
        enddot =    data[j] == '.';
        endcomma =  data[j] == ',';
        endsemic =  data[j] == ';';
        endcolon =  data[j] == ':';
        escchar =   esc[reverse_case * 5 + (
                endcolon?   4 :
                endsemic?   3 :
                endcomma?   2 :
                enddot?     1 : 0)];

        /* output code for a word */
        if(data[j] < 128 && node != mem_nodes) {
            if(node->m_id < LEVEL1_WORD_NUM(dic_len)) { /* 1-byte code */
                data_block_add(ob, node->m_id);
                data_block_add(ob, escchar);
            } else {                                    /* 2-byte code */
                data_block_add(ob, node->m_id / (256 - LEVEL1_WORD_NUM(dic_len)));
                data_block_add(ob, node->m_id % (256 - LEVEL1_WORD_NUM(dic_len)) + LEVEL1_WORD_NUM(dic_len));
                data_block_add(ob, escchar);
            }
            i = j;

        } else { /* output literal character */
            if(!escmap[data[i]]) {
                data_block_add(ob, data[i]);
            } else {
                data_block_add(ob, dic_len / (256 - LEVEL1_WORD_NUM(dic_len)));
                data_block_add(ob, dic_len % (256 - LEVEL1_WORD_NUM(dic_len)) + LEVEL1_WORD_NUM(dic_len));
                data_block_add(ob, data[i]);
            }
        }
    }

    while(i < size) { /* output last bytes as literal characters */
        if(!escmap[data[i]]) {
            data_block_add(ob, data[i]);
        } else {
            data_block_add(ob, dic_len / (256 - LEVEL1_WORD_NUM(dic_len)));
            data_block_add(ob, dic_len % (256 - LEVEL1_WORD_NUM(dic_len)) + LEVEL1_WORD_NUM(dic_len));
            data_block_add(ob, data[i]);
        }
        i += 1;
    }
    data_block_resize(ob, ob->m_size + 4);
    memcpy(ob->m_data + ob->m_size - 4, &size, 4);
    return;
}

static void dictionary_decode_imp(unsigned char* data, uint32_t size, uint8_t esc[10], data_block_t* ob) {
    int dstpos;
    int ch;
    int id;
    int i;
    uint8_t  escmap[256] = {0};
    uint32_t srcpos;
    uint32_t reverse_pos = -1;

    for(i = 0; i < dic_len; i++) { /* calculate length of each word */
        dicstrlen[i] = strlen(dic[i]);
    }
    for(i = 0; i < 10; i++) { /* init escape map */
        escmap[esc[i]] = i + 1;
    }

    srcpos = *(uint32_t*)(data + size - 4);
    dstpos = size - 4;
    data_block_resize(ob, srcpos);

    while(srcpos > 0) {
        if(!escmap[ch = data[--dstpos]]) {
            ob->m_data[--srcpos] = ch;
        } else {
            if((id = data[--dstpos]) >= LEVEL1_WORD_NUM(dic_len)) {
                id = data[--dstpos] * (256 - LEVEL1_WORD_NUM(dic_len)) + (id - LEVEL1_WORD_NUM(dic_len)); /* 2-byte code */
                if(id == dic_len) {
                    ob->m_data[--srcpos] = ch; /* esc char */
                    continue;
                }
            }

#define M_reverse_case(c) ((c)^0x20) /* (islower(c)? toupper(c) : tolower(c)) */

            /* recover word */
            srcpos -= dicstrlen[id];
            memcpy(ob->m_data + srcpos, dic[id], dicstrlen[id]);

            switch(escmap[ch]) {
                case 2: case 7:  ob->m_data[srcpos + dicstrlen[id] - 1] = '.'; break;
                case 3: case 8:  ob->m_data[srcpos + dicstrlen[id] - 1] = ','; break;
                case 4: case 9:  ob->m_data[srcpos + dicstrlen[id] - 1] = ';'; break;
                case 5: case 10: ob->m_data[srcpos + dicstrlen[id] - 1] = ':'; break;
            }
            if(escmap[ch] >= 6) { /* reverse case */
                ob->m_data[srcpos] = M_reverse_case(ob->m_data[srcpos]);
            }

            /* process last reverse case pos */
            if(reverse_pos != -1 && M_check_reverse_case(ob->m_data, reverse_pos)) {
                ob->m_data[reverse_pos] = M_reverse_case(ob->m_data[reverse_pos]);
            }
            reverse_pos = srcpos;
        }
    }

    /* finish first reverse case pos */
    if(reverse_pos != -1 && M_check_reverse_case(ob->m_data, reverse_pos)) {
        ob->m_data[reverse_pos] = M_reverse_case(ob->m_data[reverse_pos]);
    }
    return;
}
