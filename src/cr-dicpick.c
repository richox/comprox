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
#include "cr-dicpick.h"
#include "cr-datablock.h"
#include "cr-diccode.h"
#include "miniport-thread.h"

#define HASHMAP_MAXSIZE     (TOTAL_WORD_NUM * 13 + 1)
#define HASHMAP_CAPACITY    (TOTAL_WORD_NUM * 23 + 3)
#define WORD_MIN_FREQ       5

/* reserved words */
static const char* reserved_words[] = {
    "\x20\x20",
    "http://www.",
};
static const int reserved_wordnum = sizeof(reserved_words) / sizeof(reserved_words[0]);


/* hashmap definition */
typedef struct hashmap_element_t {
    char m_word[WORD_MAXLEN + 1];
    int  m_count;
} hashmap_element_t;

static hashmap_element_t hashmap_data[HASHMAP_CAPACITY];
static int hashmap_size;

static inline int hashmap_element_cmp_by_words(const void* pa, const void* pb) {
    hashmap_element_t* ea = (hashmap_element_t*)pa;
    hashmap_element_t* eb = (hashmap_element_t*)pb;
    return strcmp(ea->m_word, eb->m_word);
}
static inline int hashmap_element_reverse_cmp_by_count(const void* pa, const void* pb) {
    hashmap_element_t* ea = (hashmap_element_t*)pa;
    hashmap_element_t* eb = (hashmap_element_t*)pb;
    if(eb->m_count != ea->m_count) {
        return eb->m_count - ea->m_count;
    }
    return hashmap_element_cmp_by_words(pb, pa);
}

/* utility functions for hashmap */
static inline int hashword(const char* s) { /* calculate hash value of a word */
    int hash = 0;

    while(isalpha(*s)) {
        hash = hash * 131313131 + tolower(*s++);
    }
    return hash & INT_MAX;
}
static inline int cmpword(const char* a, const char* b) { /* compare two words */
    while(isalpha(*a) && isalpha(*b)) {
        if(tolower(*a) != tolower(*b)) {
            return *a - *b;
        }
        a++;
        b++;
    }
    return !isalpha(*a) - !isalpha(*b);
}
static inline void copyword(char* dst, const char* src) { /* copy a word */
    while(isalpha(*src)) {
        *dst++ = tolower(*src++);
    }
    *dst = '\0';
    return;
}
static inline void addword(const char* s) {
    int hash_pos = hashword(s) % HASHMAP_CAPACITY;
    int i;
    int min_count;
    hashmap_element_t* backup_data;

    while(hashmap_data[hash_pos].m_count > 0 && cmpword(hashmap_data[hash_pos].m_word, s) != 0) {
        hash_pos = (hash_pos + 1) % HASHMAP_CAPACITY;
    }
    if(hashmap_data[hash_pos].m_count > 0) { /* word existed */
        hashmap_data[hash_pos].m_count += 1;
        return;
    }

    /* add a new word */
    copyword(hashmap_data[hash_pos].m_word, s);
    hashmap_size += 1;
    hashmap_data[hash_pos].m_count = 1;

    /* remove less used words when hashmap is full */
    if(hashmap_size == HASHMAP_MAXSIZE) {
        backup_data = malloc(HASHMAP_MAXSIZE * sizeof(hashmap_element_t));
        min_count = INT_MAX;

        for(i = 0; i < HASHMAP_CAPACITY; i++) {
            if(hashmap_data[i].m_count > 0) {
                if(hashmap_data[i].m_count < min_count) {
                    min_count = hashmap_data[i].m_count;
                }
                strcpy(backup_data[hashmap_size - 1].m_word, hashmap_data[i].m_word);
                backup_data[hashmap_size - 1].m_count = hashmap_data[i].m_count;
                hashmap_size -= 1;
            }
            hashmap_data[i].m_count = 0;
        }

        for(i = 0; i < HASHMAP_MAXSIZE; i++) {
            if(backup_data[i].m_count > min_count + 5) { /* regard words with (count <= min_count + 5) as "less used" */
                hash_pos = hashword(backup_data[i].m_word) % HASHMAP_CAPACITY;

                while(hashmap_data[hash_pos].m_count > 0 && cmpword(hashmap_data[hash_pos].m_word, backup_data[i].m_word) != 0) {
                    hash_pos = (hash_pos + 1) % HASHMAP_CAPACITY;
                }
                strcpy(hashmap_data[hash_pos].m_word, backup_data[i].m_word);
                hashmap_data[hash_pos].m_count = backup_data[i].m_count;
                hashmap_size += 1;
            }
        }
        free(backup_data);
    }
    return;
}

/* pthread-callback wrapper */
typedef struct addword_thread_param_pack_t {
    unsigned char (*m_words)[WORD_MAXLEN + 2];
    int m_nwords;
} addword_thread_param_pack_t;

static void* addword_thread(addword_thread_param_pack_t* args) { /* thread for adding word */
    int i;
    for(i = 0; i < args->m_nwords; i++) {
        addword((char*)args->m_words[i]);
    }
    return NULL;
}

#define FDATA_BLOCK 200000

void dicpick(FILE* fp, data_block_t* dic_block) {
    static unsigned char words[2][FDATA_BLOCK / WORD_MINLEN][WORD_MAXLEN + 2];
    static unsigned char fdata[FDATA_BLOCK];
    int nwords = 0;
    int flen;
    int x;
    int y;
    int p;
    int short_word = 0;
    uint8_t accept_suffixes[256] = {0};

    pthread_t thread;
    addword_thread_param_pack_t args;
    int k = 0;

    accept_suffixes[' '] = 1;
    accept_suffixes[','] = 1;
    accept_suffixes['.'] = 1;
    accept_suffixes[':'] = 1;
    accept_suffixes[';'] = 1;

    /* for first join */
    args.m_nwords = 0;
    args.m_words = NULL;
    pthread_create(&thread, NULL, (void*)addword_thread, &args);

    /* split words */
    while((flen = fread(fdata, 1, sizeof(fdata), fp)) > 0) {
        fdata[flen - 1] = 0;
        x = 1;
        nwords = 0;

        while(x < flen) {
            if(isalpha(fdata[x]) && !isalpha(fdata[x - 1])) {
                y = x + 1;
                while(y < flen && islower(fdata[y])) {
                    y++;
                }

                if(y >= x + WORD_MINLEN && y <= x + WORD_MAXLEN && accept_suffixes[fdata[y]]) {
                    copyword((char*)words[k][nwords++], (char*)fdata + x);
                }
                x = y;
            }
            x++;
        }
        pthread_join(thread, NULL); /* wait previous pass */
        args.m_words = words[k];
        args.m_nwords = nwords;
        pthread_create(&thread, NULL, (void*)addword_thread, &args);
        k = !k;
    }
    pthread_join(thread, NULL);

    /* sort words by count */
    y = 0;
    for(x = 0; x < HASHMAP_CAPACITY; x++) {
        if(hashmap_data[x].m_count > WORD_MIN_FREQ) { /* ignore "less used" words */
            copyword(hashmap_data[y].m_word, hashmap_data[x].m_word);
            hashmap_data[y].m_count = hashmap_data[x].m_count;
            y++;
        }
    }
    qsort(hashmap_data, y, sizeof(hashmap_element_t), hashmap_element_reverse_cmp_by_count);

    /* sort level-2 words by name */
    if(y > TOTAL_WORD_NUM - reserved_wordnum) {
        y = TOTAL_WORD_NUM - reserved_wordnum;
    }
    if(y > LEVEL1_WORD_NUM(y) - reserved_wordnum) {
        x = LEVEL1_WORD_NUM(y) - reserved_wordnum;
        qsort(hashmap_data + x, y - x, sizeof(hashmap_element_t), hashmap_element_cmp_by_words);
    }

    /* output */
    data_block_reserve(dic_block, (hashmap_size + reserved_wordnum) * (WORD_MAXLEN + 3));
    for(x = 0; x < reserved_wordnum; x++) {
        for(p = 0; reserved_words[x][p] != 0; p++) {
            data_block_add(dic_block, reserved_words[x][p]);
        }
        data_block_add(dic_block, '\n');
    }

    for(x = 0; x < y; x++) {
        if(x < LEVEL1_WORD_NUM(y) || strlen(hashmap_data[x].m_word) >= WORD_MINLEN + 1) { /* ignore too short words */
            for(p = 0; hashmap_data[x].m_word[p] != 0; p++) {
                data_block_add(dic_block, hashmap_data[x].m_word[p]);
            }
            data_block_add(dic_block, '\n');
        } else {
            short_word += 1;
        }
    }
    data_block_add(dic_block, 0);
    return;
}

void dic_lcp_encode(struct data_block_t* dic_block) {
    data_block_t out_block = INITIAL_BLOCK;
    int w1 = 0;
    int w2 = 0;
    int lcp;
    int nword = 0;

    /* since dictionary is sorted, we use LCP value to replace common
     * prefix of a word, such process will reduce the size of the
     * dictionary.
     */

    /* write first word */
    while(dic_block->m_data[w2] != '\n') {
        data_block_add(&out_block, dic_block->m_data[w2++]);
    }
    w2++;
    nword++;
    data_block_add(&out_block, '\n');

    /* transform rest word */
    while(dic_block->m_data[w2] != '\0') {
        lcp = 0;
        while(dic_block->m_data[w1 + lcp] == dic_block->m_data[w2 + lcp]) {
            lcp++;
        }
        data_block_add(&out_block, lcp);
        w1 = w2;
        w2 = w2 + lcp;

        while(dic_block->m_data[w2] != '\n') {
            data_block_add(&out_block, dic_block->m_data[w2++]);
        }
        w2++;
        nword++;
        data_block_add(&out_block, '\n');
    }
    data_block_add(&out_block, 255); /* terminate flag, since LCP never get 255 */

    /* copy back to dic_block */
    data_block_resize(dic_block, out_block.m_size);
    memcpy(dic_block->m_data, out_block.m_data, out_block.m_size);
    data_block_destroy(&out_block);
    return;
}

void dic_lcp_decode(struct data_block_t* dic_block) {
    data_block_t out_block = INITIAL_BLOCK;
    int wi = 0;
    int wo = 0;
    int lcp;

    /* decode first word */
    while(dic_block->m_data[wi] != '\n') {
        data_block_add(&out_block, dic_block->m_data[wi++]);
    }
    wi++;
    data_block_add(&out_block, '\n');

    /* decode rest word */
    while(dic_block->m_data[wi] != 255) {
        lcp = dic_block->m_data[wi++];
        while(lcp > 0) {
            lcp--;
            data_block_add(&out_block, out_block.m_data[wo++]);
        }

        while(dic_block->m_data[wi] != '\n') {
            data_block_add(&out_block, dic_block->m_data[wi++]);
        }
        wi++;
        data_block_add(&out_block, '\n');

        while(out_block.m_data[wo] != '\n') {
            wo++;
        }
        wo++;
    }
    data_block_add(&out_block, 0);

    /* copy back to dic_block */
    data_block_resize(dic_block, out_block.m_size);
    memcpy(dic_block->m_data, out_block.m_data, out_block.m_size);
    data_block_destroy(&out_block);
    return;
}
