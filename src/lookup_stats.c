/*
 * lookup_stats.c
 *
 * statistics gathering for RID FIB tests.
 *
 * structs and functions to keep track of forwarding stats of the RID
 * forwarding process, e.g. false positives (fps), tps, tns.
 *
 * Antonio Rodrigues <antonior@andrew.cmu.edu>
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

#include "lookup_stats.h"

void lookup_stats_print(struct lookup_stats * stats) {

    printf(
            "\n-------------------------------------------------------------------------------\n"\
            "%-12s\t| %-12s\t| %-12s\t| %-12s\t| %-12s\n"\
            "-------------------------------------------------------------------------------\n"\
            "%-12d\t| %-12d\t| %-12d\t| %-12d\t| %-.5LE\n",
            "# TPs", "# FPs", "# TNs", "# LOOKUPS", "FP RATE",
            stats->tps,
            stats->fps,
            stats->tns,
            stats->total_matches,
            (long double) ((long double) stats->fps / ((long double) stats->total_matches)));

    printf("\n");
}

void prefix_info_init(
        struct prefix_info ** prefix_info,
        char * prefix,
        uint8_t prefix_size) {

    (*prefix_info)->prefix = (char *) calloc(strlen(prefix) + 1, sizeof(char));
    strncpy((*prefix_info)->prefix, prefix, strlen(prefix) + 1);
    (*prefix_info)->prefix_size = prefix_size;
}

void prefix_info_erase(struct prefix_info ** prefix_info) {

    free((*prefix_info)->prefix);
}

void lookup_stats_init(
        struct lookup_stats ** stats,
        char * prefix,
        uint8_t prefix_size) {

    // the prefix info will allow to distinguish true positives from false 
    // positives. this is good for stats gathering...
    (*stats)->prefix_info = (struct prefix_info *) malloc(sizeof(struct prefix_info));
    prefix_info_init(&(*stats)->prefix_info, prefix, prefix_size);

    (*stats)->req_entry_diffs_fps = (uint32_t *) calloc(prefix_size + 1, sizeof(uint32_t));
    (*stats)->req_entry_diffs = (uint32_t *) calloc(prefix_size + 1, sizeof(uint32_t));

    // everything else is initialized to 0
    (*stats)->tps = 0;
    (*stats)->fps = 0;
    (*stats)->tns = 0;
    (*stats)->total_matches = 0;
}

void lookup_stats_erase(struct lookup_stats ** stats) {

    // erase the prefix string and |F\R| array
    prefix_info_erase(&(*stats)->prefix_info);
    free((*stats)->prefix_info);
    free((*stats)->req_entry_diffs_fps);
    free((*stats)->req_entry_diffs);

    // printf("lookup_stats_erase():"\ 
    //     "\n\t[PREFIX] : %s", 
    //     (*stats)->prefix);

    // int i = 0;
    // for (i; i < (*stats)->prefix_size; i++)
    //     printf("\n\t|F\\R|[%d] : %d", 
    //         i, (*stats)->req_entry_diffs_fps[i]);

    // printf("\n");   
}

void lookup_stats_update(
        struct lookup_stats ** stats,
        uint8_t req_entry_diff,
        uint32_t tps,
        uint32_t fps,
        uint32_t tns,
        uint32_t total_matches) {

    // only update when a false positive triggers lookup_stats_update()
    if (fps > 0) {

        (*stats)->req_entry_diffs_fps[req_entry_diff] += fps; 

        // if (req_entry_diff == 0) {
        //     printf("lookup_stats_update():"\ 
        //         "\n\t[PREFIX] : %s"\
        //         "\n\t|F\\R|[%d] = %d\n", 
        //         (*stats)->prefix,
        //         req_entry_diff, (*stats)->req_entry_diffs_fps[req_entry_diff]);
        // }
    }

    if ((*stats)->req_entry_diffs != NULL)
        (*stats)->req_entry_diffs[req_entry_diff] += 1;

    (*stats)->tps += tps;
    (*stats)->fps += fps;
    (*stats)->tns += tns;
    (*stats)->total_matches += total_matches;
}

struct lookup_stats * lookup_stats_add(
        struct lookup_stats ** ht,
        struct lookup_stats * node) {

    struct lookup_stats * s;

    HASH_FIND_STR(*ht, node->prefix_info->prefix, s);

    if (s == NULL) {
        HASH_ADD_KEYPTR(hh, *ht, node->prefix_info->prefix, strlen(node->prefix_info->prefix), node);
    }

    return (*ht);
}
