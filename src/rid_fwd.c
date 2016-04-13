/*
 * rid_fwd.c
 *
 * tests 2 different RID FIB schemes: Linear Search of Hash Tables (LSHT),
 * organized by prefix size and Patricia Tries (PTs), organized by either (a)
 * prefix size or (2) Hamming weight (i.e. number of '1s' in XIDs):
 *
 * -# it reads URLs from a file (passed as an argument to the program e.g.
 *         'rid_fwd -f url.tx'), builds LSHT and PT RID FIBs out of all URLs in the
 *         file.
 * -# it generates random request names out of the same URLs by
 *         adding them a random number of suffixes.
 * -# it then passes the requests through the FIBs, collecting statistics on
 *         True Positives (TPs), False Positives (FPs), True Negatives (TNs) and
 *         total number of matching tests.
 *
 * Antonio Rodrigues <adamiaonr@cmu.edu>
 *
 * Copyright 2015 Carnegie Mellon University
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *         http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * limitations under the License.
 */

#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>

// XXX: as a substitute for <click/hashtable.hh>
#include "uthash.h"

#include "pt.h"
#include "lookup_stats.h"
#include "rid_utils.h"

#ifdef __linux
#include <sys/time.h>
#include <time.h>
#endif

const char * SUFFIXES[] = {"/ph", "/ghc", "/cylab", "/9001", "/yt1300", "/r2d2", "/c3po"};
const int SUFFIXES_SIZE = 7;

const char * DEFAULT_URL_FILE = "url.txt";

// constants for different ways of grouping forwarding entries in FIBs: (1)
// by prefix size (PREFIX_SIZE_MODE = 0x01) or (2) Hamming weight
// (HAMMING_WEIGHT_MODE = 0x02), i.e. number of '1s' in the XID of the
// entry
typedef enum {PREFIX_SIZE, HAMMING_WEIGHT} ht_mode;
static const char * ht_mode_strs[] = {"p", "h"};

static __inline int rand_int(int min, int max) {
    return min + rand() / (RAND_MAX / (max - min + 1) + 1);
}

int update_tp_cond(uint32_t fp_sizes[], uint32_t tp_sizes[], uint32_t tp_cond[][BF_MAX_ELEMENTS]) {

    int i = 0;

    // printf("fp_sizes = ");
    // for (i = 0; i < BF_MAX_ELEMENTS; i++)
    //     printf("[%-3d]", fp_sizes[i]);

    // printf("\ntp_sizes = ");
    // for (i = 0; i < BF_MAX_ELEMENTS; i++)
    //     printf("[%-3d]", tp_sizes[i]);

    // printf("\n");

    int tp = 0, fp = 0;
    for (tp = 0; tp < BF_MAX_ELEMENTS; tp++) {

        if (tp_sizes[tp] > 0) {

            for (fp = tp; fp < BF_MAX_ELEMENTS; fp++)
                tp_cond[tp][fp] += fp_sizes[fp];
        }
    }

    // reset the FP and TP sizes arrays
    for (tp = 0; tp < BF_MAX_ELEMENTS; tp++) {
        fp_sizes[tp] = 0;
        tp_sizes[tp] = 0;
    }

    return 0;
}

void print_tp_cond(uint32_t tp_cond[][BF_MAX_ELEMENTS]) {

    FILE * output_file = fopen(DEFAULT_TP_SIZE_FILE, "wb");

    printf(
            "\n-------------------------------------------------------------------------------\n"\
            "%-8s\t| %-20s\t| %-20s\t\n"\
            "-------------------------------------------------------------------------------\n",
            "|TP|", "# FP : |F| = |TP|", "# FP : |F| > |TP|");

    int tp = 0, fp = 0, fps_equal_total = 0, fps_larger_total = 0, tp_cond_fps_equal = 0, tp_cond_fps_larger = 0;

    for (tp = 0; tp < BF_MAX_ELEMENTS; tp++) {

        fps_equal_total = tp_cond[tp][tp];
        fps_larger_total = 0;

        for (fp = tp + 1; fp < BF_MAX_ELEMENTS; fp++) {
           fps_larger_total += tp_cond[tp][fp];
        }

        printf(
                "%-8d\t| %-20d\t| %-20d\t\n", tp + 1, fps_equal_total, fps_larger_total);

        fprintf(output_file, 
            "%d,%d,%d\n", 
            tp + 1,
            fps_equal_total,
            fps_larger_total);

        tp_cond_fps_larger += fps_larger_total;
        tp_cond_fps_equal += fps_equal_total;
    }

    printf(
            "-------------------------------------------------------------------------------\n"\
            "%-8s\t| %-20s\t| %-20s\t\n"\
            "-------------------------------------------------------------------------------\n"\
            "%-8d\t| %-20d\t| %-20d\t\n",
            "TOTAL |TP|", "TOTAL # |F| = |TP|", "TOTAL # |F| > |TP|",
            BF_MAX_ELEMENTS, tp_cond_fps_equal, tp_cond_fps_larger);

    printf("\n");

    fclose(output_file);
}

int generate_request_name(char ** request_name, char * request_prefix, int request_size) {

//    int suffixes_num = rand_int(1, SUFFIXES_SIZE);
    int suffixes_num = request_size - count_prefixes(request_prefix);

    if (suffixes_num < 0)
        return -1;

    strncpy(*request_name, request_prefix, PREFIX_MAX_LENGTH);

    if (suffixes_num == 0)
        printf("generate_request_name() : already at |R| = %d : %s\n", request_size, *request_name);

    int request_name_lenght = strlen(*request_name);

    for ( ; suffixes_num > 0; suffixes_num--) {

        strncat(*request_name, SUFFIXES[rand_int(0, SUFFIXES_SIZE - 1)], (PREFIX_MAX_LENGTH - request_name_lenght));
    }

    return 0;
}

void print_namespace_stats(int * url_sizes, int max_prefix_size) {

    printf(
            "\n-------------------------------------------------------------------------------\n"\
            "%-12s\t| %-12s\t\n"\
            "-------------------------------------------------------------------------------\n",
            "|F|", "# ENTRIES");

    int u_size = 0, nr_u_size = 0, nr_entries = 0;

    for (u_size = 0; u_size < max_prefix_size; u_size++) {

//        if (url_sizes[u_size] > 0) {

            printf(
                    "%-12d\t| %-12d\t\n", u_size + 1, url_sizes[u_size]);

            nr_u_size += (url_sizes[u_size] > 0 ? 1 : 0);
            nr_entries += url_sizes[u_size];
//        }
    }

    printf(
            "-------------------------------------------------------------------------------\n"\
            "%-12s\t| %-12s\t\n"\
            "-------------------------------------------------------------------------------\n"\
            "%-12d\t| %-12d\t\n",
            "TOTAL |F|", "TOTAL # ENTRIES",
            nr_u_size, nr_entries);

    printf("\n");
}

int main(int argc, char **argv) {

    // XXX: default mode for FIB organization is by prefix size (in nr. of URL
    // elements)
    ht_mode mode = PREFIX_SIZE;

    printf("Patricia Trie (PT) as in Papalini et al. 2014\n");

    // ************************************************************************
    // A) build forwarding tables
    // ************************************************************************

    // A.1) the base data structures which will hold the FIBs. we try to use
    // the data types which will be used by the Click modular router

    // XXX: Patricia Trie (PT) FIB: also an HT indexed by prefix size +
    // ptree pointer (we do NOT index by Hamming weight (HW) here as in
    // Papalini et al. 2014: note that regardless of the pair {|F|, k},
    // the most probable HW to get is always ~70, when m = 160 bit).
    struct pt_ht * pt_fib = NULL;

    // A.2) open an URL file
    printf("[fwd table build]: reading .tbl file\n");

    char * url_file_name = (char *) calloc(128, sizeof(char));

    if (argc > 2 && !(strncmp(argv[1], "-f", 2))) {

        strncpy(url_file_name, argv[2], strlen(argv[2]));

    } else {

        // A.2.1) use a default one if argument not given...
        printf("[fwd table build]: ERROR : no .tbl file supplied. aborting.\n");

        return -1;
    }

    // if (argc > 3 && !(strncmp(argv[3], "-m", 2))) {

    //     if (strcmp(argv[4], ht_mode_strs[1]) == 0) {

    //         mode = HAMMING_WEIGHT;
    //         printf("[fwd table build]: choosing HAMMING_WEIGHT mode\n");

    //     } else if (strcmp(argv[4], ht_mode_strs[0]) != 0) {

    //         printf("[fwd table build]: unknown mode %s choosing PREFIX_SIZE as default\n", argv[4]);
    //     }

    // } else {

    //     // A.2.1) use a default one if argument not given...
    //     printf("[fwd table build]: using default URL file: %s\n", DEFAULT_URL_FILE);
    //     strncpy(url_file_name, DEFAULT_URL_FILE, strlen(DEFAULT_URL_FILE));
    // }

    FILE * fr = fopen(url_file_name, "rt");

    // A.3) start reading the URLs and add forwarding entries
    // to each FIB
    char prefix[PREFIX_MAX_LENGTH];
    char * newline_pos;

    // // A.3.1) hts with direct references to `per prefix' lookup statistics
    // struct lookup_stats * pt_stats_ht = NULL;

    // A.3.1.1) auxiliary variables for stats gathering...
    // FIXME: this is ugly and you should feel bad...
    // struct pt_fwd * p = NULL;

    // A.3.1.2) just an aux array to keep stats on URL sizes
    int url_sizes[PREFIX_MAX_COUNT] = {0}, max_prefix_size = 0;

    // we will keep track of max, min and avg. route add/lookup times
    clock_t begin, end;

    double cur_time = 0.0;
    double max_time = 0.0, min_time = DBL_MAX, avg_time = 0.0, tot_time = 0.0;

    // create an RID out of the prefix
    struct click_xia_xid * rid = (struct click_xia_xid *) malloc(sizeof(struct click_xia_xid));
    char * _prefix = (char *) calloc(PREFIX_MAX_LENGTH, sizeof(char));

    // keep track of the number of encoded prefixes
    uint32_t prefix_count = 0;

    while (fgets(prefix, PREFIX_MAX_LENGTH, fr) != NULL && prefix_count < 1000000) {

        // A.3.2) remove any trailing newline ('\n') character
        if ((newline_pos = strchr(prefix, '\n')) != NULL)
            *(newline_pos) = '\0';

        // create an RID out of the prefix
        memset(rid, 0, sizeof(struct click_xia_xid));
        // struct click_xia_xid * rid = (struct click_xia_xid *) malloc(sizeof(struct click_xia_xid));
        int prefix_size = name_to_rid(&rid, prefix);

        // // FIXME: based on the mode argument, we may need to change the value
        // // of prefix_size to the Hamming weight (nr. of '1s' in RID)
        // if (mode == HAMMING_WEIGHT)
        //     prefix_size = rid_hamming_weight(rid);

        // XXX: update the URL size stats
        url_sizes[prefix_size - 1]++;

        if (prefix_size > max_prefix_size)
            max_prefix_size = prefix_size;

        // A.3.3) allocate memory in the heap for this prefix, which will be
        // pointed to by lookup_stats struct *'s in fwd entries, used for
        // control and data gathering
        memset(_prefix, 0, PREFIX_MAX_LENGTH);
        // char * _prefix = (char *) calloc(PREFIX_MAX_LENGTH, sizeof(char));
        strncpy(_prefix, prefix, PREFIX_MAX_LENGTH);

        // printf("[fwd table build]: adding URL %s (size %d)\n", _prefix, prefix_size);

        // A.3.5) PT FIB
        begin = clock();
        pt_ht_add(&pt_fib, rid, _prefix, prefix_size);
        end = clock();

        // time keeping
        cur_time += (double) (end - begin) / CLOCKS_PER_SEC;
        tot_time += cur_time;

        if (min_time > cur_time)
            min_time = cur_time;
        else if (max_time < cur_time)
            max_time = cur_time;

        //lookup_stats_add(&pt_stats_ht, p->stats);
        prefix_count++;
    }

    printf("[fwd table build]: done. added %d prefixes to FIB: "\
        "\n\t[TOT_TIME]: %-.8f"\
        "\n\t[MAX_TIME]: %-.8f"\
        "\n\t[MIN_TIME]: %-.8f"\
        "\n\t[AVG_TIME]: %-.8f\n", 
        //HASH_COUNT(pt_stats_ht),
        prefix_count,
        tot_time, max_time, min_time,
        //(tot_time / (double) HASH_COUNT(pt_stats_ht)));
        (tot_time / (double) prefix_count));

    // printf("[fwd table build]: *** FWD TABLE *** :\n");

    // struct pt_ht * itr;

    // for (itr = pt_fib; itr != NULL; itr = (struct pt_ht *) itr->hh.next) {
    //    printf("\n[PREFIX SIZE: %d]:\n", itr->prefix_size);
    //    pt_fwd_print(itr->trie, PRE_ORDER);
    // }

    fclose(fr);

    // ************************************************************************
    // B) start testing requests against the forwarding tables just built
    // ************************************************************************

    printf("[rid fwd simulation]: reading .req file\n");

    // B.1) re-open the test data file
    fr = fopen(url_file_name, "rt");

    // B.2) create useful vars for the matching tests:

    // B.2.1) request prefix and name:
    //    -#     request_prefix: directly retrieved from the URL file `as is'
    //    -#     request_name: the actual name used to generate an
    //         RID, created by adding a few more URL elements to the prefix
    char * request_prefix = (char *) calloc(PREFIX_MAX_LENGTH, sizeof(char));
    char * request_name = (char *) calloc(PREFIX_MAX_LENGTH, sizeof(char));

    // B.2.2) the RID `holder'
    struct click_xia_xid * request_rid = (struct click_xia_xid *) malloc(sizeof(struct click_xia_xid));

    // B.2.3) request size (guides the lookup procedures)
    int request_size = 0;

    printf("[rid fwd simulation]: generating random requests out of prefixes in URL file\n");

    // keep track of the number of requested names
    uint32_t request_cnt = 0;
    // keep track of the sizes of FPs and TPs for each lookup
    uint32_t fp_sizes[BF_MAX_ELEMENTS] = {0};
    uint32_t tp_sizes[BF_MAX_ELEMENTS] = {0};
    // keep track of FP sizes which are larger than a max. TP size for any lookup
    uint32_t tp_cond[BF_MAX_ELEMENTS][BF_MAX_ELEMENTS] = {0};

    cur_time = 0.0;
    max_time = 0.0, min_time = DBL_MAX, avg_time = 0.0, tot_time = 0.0;

    // B.3) start reading the URLs in the test data file
    while (fgets(request_prefix, PREFIX_MAX_LENGTH, fr) != NULL && (request_cnt < 5000)) {

        // B.3.1) remove any trailing newline ('\n') character
        if ((newline_pos = strchr(request_prefix, '\n')) != NULL)
            *(newline_pos) = '\0';

        // B.3.1) remove any trailing PREFIX_DELIM ('/') character
        if (request_prefix[strlen(request_prefix) - 1] == '/')
            request_prefix[strlen(request_prefix) - 1] = '\0';

        // B.3.1) generate some random name out of the request prefix by adding
        // a random number of elements to it
        if (generate_request_name(&request_name, request_prefix, BF_MAX_ELEMENTS) < 0)
            continue;

        // B.3.2) generate RIDs out of the request names
        request_size = name_to_rid(&request_rid, request_name);

        // // FIXME: based on the mode argument, we may need to change the value
        // // of prefix_size to the Hamming weight (nr. of '1s' in RID)
        // if (mode == HAMMING_WEIGHT)
        //     request_size = rid_hamming_weight(request_rid);

        // B.3.3) pass the request RID through the FIBs, gather the
        // lookup stats
        // printf("[rid fwd simulation]: lookup for %s started\n", request_name);
        request_cnt++;

        if (request_cnt % 1000 == 0)
            printf("[rid fwd simulation]: ran %d requests (time elapsed : %-.8f)\n", request_cnt, tot_time);

        begin = clock();
        pt_ht_lookup(pt_fib, request_name, request_size, request_rid, fp_sizes, tp_sizes);
        end = clock();

        update_tp_cond(fp_sizes, tp_sizes, tp_cond);

        // time keeping
        cur_time += (double) (end - begin) / CLOCKS_PER_SEC;
        tot_time += cur_time;

        if (min_time > cur_time)
            min_time = cur_time;
        else if (max_time < cur_time)
            max_time = cur_time;

        memset(request_name, 0, PREFIX_MAX_LENGTH);
        memset(request_prefix, 0, PREFIX_MAX_LENGTH);
    }

    printf("[rid fwd simulation]: done. looked up %ld requests: "\
        "\n\t[TOT_TIME]: %-.8f"\
        "\n\t[MAX_TIME]: %-.8f"\
        "\n\t[MIN_TIME]: %-.8f"\
        "\n\t[AVG_TIME]: %-.8f\n", 
        request_cnt,
        tot_time, max_time, min_time,
        (tot_time / (double) request_cnt));

    // A.3.6) print some stats about the namespace
    printf("[rid fwd simulation]: URL size distribution:\n");
    print_namespace_stats(url_sizes, max_prefix_size);

    printf("[rid fwd simulation]: simulation stats:\n");
    pt_ht_print_stats(pt_fib);
    pt_ht_erase(pt_fib);
    print_tp_cond(tp_cond);

    // struct lookup_stats * itr;

    // for (itr = pt_stats_ht; itr != NULL; itr = (struct lookup_stats *) itr->hh.next) {

    //     HASH_DEL(pt_stats_ht, itr);
    // }

    fclose(fr);

    free(request_rid);
    free(url_file_name);
    free(rid);
    free(_prefix);
    free(request_prefix);
    free(request_name);

    return 0;
}
