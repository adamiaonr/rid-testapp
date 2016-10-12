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
#include <assert.h>

#include <algorithm>
#include <string>
#include <map>
#include <list>

#include "argvparser.h"
// XXX: as a substitute for <click/hashtable.hh>
#include "uthash.h"
// playing with fire... i mean threads now...
#include "threadpool.h"

#include "pt.h"
#include "lookup_stats.h"
#include "rid_utils.h"

#ifdef __linux
#include <sys/time.h>
#include <time.h>
#endif

// max. request size and table size limits
#define REQUEST_LIMIT       5000
#define TABLE_SIZE_LIMIT    1000000

#define OPTION_URL_FILE             (char *) "url-file"
#define OPTION_SUFFIX_FILE          (char *) "suffix-file"
#define OPTION_OUTPUT_DIR           (char *) "output-dir"
#define OPTION_MIN_PREFIX_SIZE      (char *) "min-prefix-size"
#define OPTION_TABLE_SIZE           (char *) "table-size"
#define OPTION_RANDOM               (char *) "random"

using namespace std;
using namespace CommandLineProcessing;

const char * SUFFIXES[] = {"/ph", "/ghc", "/cylab", "/9001", "/yt1300", "/r2d2",
    "/c3po", "/xpto12x1", "/8t88", "/cuc", "/cic", "/nsh", "/bb8", "/wh", "/bh", "/mmh"};
const int SUFFIXES_SIZE = 16;

static __inline int rand_int(int min, int max) {
    return min + rand() / (RAND_MAX / (max - min + 1) + 1);
}

typedef std::map<int, std::vector<std::string>> PrefixHist;

ArgvParser * create_argv_parser() {

    ArgvParser * cmds = new ArgvParser();

    cmds->setIntroductoryDescription("\n\nrid-testapp v1.0\n\napplication to "\
        "test custom RID forwarding tables, built out of input URL datasets."\
        "\nby adamiaonr@cmu.edu");

    cmds->setHelpOption("h", "help", "help page.");

    cmds->defineOption(
            OPTION_URL_FILE,
            "path to .txt file w/ list of URLs",
            ArgvParser::OptionRequiresValue);

    cmds->defineOption(
            OPTION_SUFFIX_FILE,
            "path to .txt file w/ list of suffixes. these are used to generate"\
                "requests and extended prefixes.",
            ArgvParser::OptionRequiresValue);

    cmds->defineOption(
            OPTION_OUTPUT_DIR,
            "directory on which to dump output data.",
            ArgvParser::OptionRequiresValue);

    cmds->defineOption(
            OPTION_TABLE_SIZE,
            "size of fwd table, in nr. of entries. default is 10^6.",
            ArgvParser::OptionRequiresValue);

    cmds->defineOption(
            OPTION_MIN_PREFIX_SIZE,
            "the sizes of forwarding entries (in terms of encoded URL "\
                "components) is set to a MINIMUM size. the size of the table "\
                "will be as specified (see --table-size option), but composed by larger "\
                "entries. default is 1.",
            ArgvParser::OptionRequiresValue);

    cmds->defineOption(
            OPTION_RANDOM,
            "prefixes just added to the forwarding table are added as 'request prefixes' at random, with"\ 
                "a probability of 50\%. by default, it's false.",
            ArgvParser::NoOptionAttribute);

    return cmds;
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
    for (tp = BF_MAX_ELEMENTS - 1; tp > -1; tp--) {

        if (tp_sizes[tp] > 0) {

            for (fp = tp; fp < BF_MAX_ELEMENTS; fp++)
                tp_cond[tp][fp] += fp_sizes[fp];

            break;
        }
    }

    // reset the FP and TP sizes arrays
    for (tp = 0; tp < BF_MAX_ELEMENTS; tp++) {
        fp_sizes[tp] = 0;
        tp_sizes[tp] = 0;
    }

    return 0;
}

void print_tp_cond(uint32_t tp_cond[][BF_MAX_ELEMENTS], std::string output_dir) {

    std::string filename = output_dir + std::string("/") + std::string(DEFAULT_TP_SIZE_FILE);
    FILE * output_file = fopen(filename.c_str(), "wb");
    // write the first line
    fprintf(output_file, "TP_SIZE\tFP_EQUAL\tFP_LARGER\n");

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
            "%d\t%d\t%d\n", 
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

    int prefix_count = count_prefixes(request_prefix);

    // if (prefix_count < MIN_PREFIX_SIZE)
    //     return -1;

//    int suffixes_num = rand_int(1, SUFFIXES_SIZE);
    int suffixes_num = request_size - prefix_count;

    if (suffixes_num < 0) {

        fprintf(stderr, "generate_request_name() : counted %d prefixes in %s\n",
            prefix_count,
            request_prefix);
        return -1;
    }

    strncpy(*request_name, request_prefix, PREFIX_MAX_LENGTH);

    // if (suffixes_num == 0)
    //     printf("generate_request_name() : already at |R| = %d : %s\n", request_size, *request_name);

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

int adjust_prefix_size(
    char ** prefix, 
    int min_prefix_size, 
    const char ** suffix_list, int suffix_list_size) {

    // C++ is so convenient for this stuff... so why use C?
    int prefix_length = strlen(*prefix);
    std::string _prefix = std::string(*prefix);

    // count the number of URL elements in prefix
    int num_prefixes = std::count(_prefix.begin(), _prefix.end(), '/') + 1;

    // the resulting prefix cannot be larger than MAX_PREFIX_SIZE
    int num_prefixes_add = 0;
    if (num_prefixes + (min_prefix_size - 1) > MAX_PREFIX_SIZE)
        num_prefixes_add = MAX_PREFIX_SIZE - num_prefixes;
    else
        num_prefixes_add = (min_prefix_size - 1);

    for ( ; num_prefixes_add > 0; num_prefixes_add--) {

        strncat(*prefix, suffix_list[rand_int(0, suffix_list_size - 1)], (PREFIX_MAX_LENGTH - prefix_length));
        num_prefixes++;
    }

    return num_prefixes;
}

int main(int argc, char **argv) {

    printf("Patricia Trie (PT) as in Papalini et al. 2014\n");

    // ************************************************************************
    // 1) build forwarding tables
    // ************************************************************************

    // we use a Patricia Trie (PT) FIB. basically an HT indexed by prefix size +
    // pt_fwd pointer. we do NOT index by Hamming weight (HW) here as in
    // Papalini et al. 2014: note that regardless of the pair {|F|, k},
    // the most probable HW to get is always ~70, when m = 160 bit). check 
    // the results on evenrnote which back this up.
    struct pt_ht * pt_fib = NULL;

    // parse the arguments with an ArgvParser
    ArgvParser * cmds = create_argv_parser();

    // arg placeholders
    char url_file_name[128] = {0};
    char suffix_file_name[128] = {0};
    char output_dir[128] = {0};

    int min_prefix_size = 1;
    int table_size = TABLE_SIZE_LIMIT;
    
    bool random = false;

    // parse() takes the arguments to main() and parses them according to 
    // ArgvParser rules
    int result = cmds->parse(argc, argv);

    // if something went wrong: show help option
    if (result != ArgvParser::NoParserError) {

        fprintf(stderr, "%s\n", cmds->parseErrorDescription(result).c_str());

        if (result != ArgvParser::ParserHelpRequested) {
            fprintf(stderr, "use option -h for help.\n");
        }

        delete cmds;
        return -1;

    } else {

        // otherwise, check for the different OPTION_
        if (cmds->foundOption(OPTION_URL_FILE)) {

            strncpy(url_file_name, (char *) cmds->optionValue(OPTION_URL_FILE).c_str(), 128);
            
        } else {

            fprintf(stderr, "no URL file path specified. use "\
                "option -h for help.\n");

            delete cmds;
            return -1;
        }

        if (cmds->foundOption(OPTION_OUTPUT_DIR)) {

            strncpy(output_dir, (char *) cmds->optionValue(OPTION_OUTPUT_DIR).c_str(), 128);
            
        } else {

            fprintf(stderr, "no output dir specified. use "\
                "option -h for help.\n");

            delete cmds;
            return -1;
        }

        if (cmds->foundOption(OPTION_SUFFIX_FILE)) {

            strncpy(suffix_file_name, (char *) cmds->optionValue(OPTION_SUFFIX_FILE).c_str(), 128);
            
        }

        if (cmds->foundOption(OPTION_MIN_PREFIX_SIZE)) {

            min_prefix_size = std::stoi(cmds->optionValue(OPTION_MIN_PREFIX_SIZE));   
        }

        if (cmds->foundOption(OPTION_TABLE_SIZE)) {

            table_size = std::stoi(cmds->optionValue(OPTION_TABLE_SIZE));   
        }

        if (cmds->foundOption(OPTION_RANDOM)) {
            random = true;
        }
    }

    if (result == ArgvParser::ParserHelpRequested) {
        delete cmds;
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

    //     // use a default one if argument not given...
    //     printf("[fwd table build]: using default URL file: %s\n", DEFAULT_URL_FILE);
    //     strncpy(url_file_name, DEFAULT_URL_FILE, strlen(DEFAULT_URL_FILE));
    // }

    FILE * fr = fopen(url_file_name, "rt");

    // start reading the URLs and add forwarding entries to each FIB
    char * prefix = (char *) calloc(PREFIX_MAX_LENGTH, sizeof(char));
    char * newline_pos;

    // just an aux array to keep stats on URL sizes. note that the distribution 
    // of URLs which are READ from the file may be different from the 
    // distribution of URLs WRITTEN to the FIB (e.g. due to repeated URLs, 
    // irregular URLs, etc.)
    int url_sizes[PREFIX_MAX_COUNT] = {0};
    int max_prefix_size = 0;
    // we will keep track of max, min, avg. and total route add/lookup times
    clock_t begin, end;
    double cur_time = 0.0;
    double max_time = 0.0, min_time = DBL_MAX, avg_time = 0.0, tot_time = 0.0;
    // create an RID out of the prefix
    struct click_xia_xid * rid = (struct click_xia_xid *) malloc(sizeof(struct click_xia_xid));
    // keep track of the number of encoded prefixes
    uint32_t prefix_count = 0;
    int prefix_size = 0;

    // collect the prefixes which will be used to build requests afterwards, 
    // up to a max of REQUEST_LIMIT. the prefixes should be evenly 
    // distributed over the range [min_prefix_size, MAX_PREFIX_SIZE].
    int request_prefixes_num = 0, p_size = 0, p_length = 0;
    PrefixHist request_prefixes;

    while (fgets(prefix, PREFIX_MAX_LENGTH, fr) != NULL && prefix_count < table_size) {

        // if the prefix is too large (string size), don't consider it
        p_length = strlen(prefix);
        if ((float) p_length > ((float) PREFIX_MAX_LENGTH * 0.75))
            continue;

        // remove any trailing newline ('\n') character
        if ((newline_pos = strchr(prefix, '\n')) != NULL) {

            // void the character which used to hold '\n'
            *(newline_pos) = '\0';

            // reduce the length accordingly
            // FIXME: why not p_length-- ?
            p_length = strlen(prefix);
        }

        if (prefix[0] == '/') {
            // prefix = prefix + 1;
            // p_length--;
            // printf("[fwd table build]: removed '/' at start of prefix %s (before) vs. %s (after)\n", 
            //     prefix - 1, 
            //     prefix);
            continue;
        }

        // remove trailing '/'
        if (prefix[p_length - 1] == '/') {

            // printf("[fwd table build]: last character is '/': %s\n", prefix);
            prefix[p_length - 1] = '\0';
            p_length--;
        }

        // don't add it if the prefix size is larger than MAX_PREFIX_SIZE
        std::string p = std::string(prefix);
        p_size = (std::count(p.begin(), p.end(), '/') + 1);
        if (p_size > MAX_PREFIX_SIZE)
            continue;

        // adjust the size of the prefix size, given the min. prefix size 
        // and a list of suffixes
        // std::string p = std::string(prefix);
        // p_size = std::count(p.begin(), p.end(), '/');
        // printf("[fwd table build]: adjusting prefix %s (size: %d, target: > %d)\n", 
        //     prefix, 
        //     p_size + 1, min_prefix_size);
        //p_size = adjust_prefix_size(&prefix, min_prefix_size, SUFFIXES, SUFFIXES_SIZE);
        // printf("[fwd table build]: adjusted prefix to %s (size: %d, target: > %d)\n", 
        //     prefix, 
        //     p_size, min_prefix_size);

        // create an RID out of the prefix
        memset(rid, 0, sizeof(struct click_xia_xid));
        prefix_size = name_to_rid(&rid, prefix);

        if (prefix_size < min_prefix_size)
            continue;

        // // FIXME: based on the mode argument, we may need to change the value
        // // of prefix_size to the Hamming weight (nr. of '1s' in RID)
        // if (mode == HAMMING_WEIGHT)
        //     prefix_size = rid_hamming_weight(rid);

        // update the URL size stats
        url_sizes[prefix_size - 1]++;

        // add the prefix to the list which will be used to generate requests
        if (request_prefixes[prefix_size - 1].size() < (REQUEST_LIMIT / (MAX_PREFIX_SIZE - min_prefix_size + 1))) {

            // if OPTION_RANDOM is selected, add the prefix with a probability 
            // of 0.5
            if (random && (rand_int(0, 1) < 1)) {

                continue;
            
            } else {

                request_prefixes[prefix_size - 1].push_back(std::string(prefix));
                request_prefixes_num++;
            }
        }

        if (prefix_size > max_prefix_size)
            max_prefix_size = prefix_size;

        // printf("[fwd table build]: adding URL %s (size %d)\n", prefix, prefix_size);

        // add rid (and prefix for TP stats tracking) to RID FIB
        begin = clock();
        pt_ht_add(&pt_fib, rid, prefix, prefix_size);
        end = clock();

        if (++prefix_count % 100000 == 0)
            printf("[fwd table build]: added %d prefixes (time elapsed : %-.8f)\n", prefix_count, tot_time);

        // time keeping
        cur_time += (double) (end - begin) / CLOCKS_PER_SEC;
        tot_time += cur_time;

        if (min_time > cur_time)
            min_time = cur_time;
        else if (max_time < cur_time)
            max_time = cur_time;
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

    // statistics about requests
    printf("[fwd table build]: request prefix histogram has %d prefixes: ", request_prefixes_num);
    PrefixHist::iterator itr;
    for (itr = request_prefixes.begin(); itr != request_prefixes.end(); itr++)
        printf("\n\t[SIZE: %d]: %d", (itr)->first, (itr)->second.size());
    printf("\n"); 

    fclose(fr);

    // ************************************************************************
    // 2) start testing requests against the forwarding tables just built
    // ************************************************************************

    // create useful vars for the matching tests:

    // request prefix and name:
    //    -# request_prefix: directly retrieved from the URL file `as is'
    //    -# request_name: the actual name used to generate an
    //          RID, created by adding a few more URL elements to the prefix
    char request_prefix[PREFIX_MAX_LENGTH];
    char * request_name = (char *) calloc(PREFIX_MAX_LENGTH, sizeof(char));
    // the RID `holder'
    struct click_xia_xid * request_rid = (struct click_xia_xid *) malloc(sizeof(struct click_xia_xid));
    // request size (guides the lookup procedures)
    int request_size = 0;
    // keep track of the number of requested names
    uint32_t request_cnt = 0;
    // keep track of the sizes of FPs and TPs for each lookup
    uint32_t fp_sizes[BF_MAX_ELEMENTS] = {0};
    uint32_t tp_sizes[BF_MAX_ELEMENTS] = {0};
    // keep track of FP sizes which are larger than a max. TP size for any lookup
    uint32_t tp_cond[BF_MAX_ELEMENTS][BF_MAX_ELEMENTS] = {0};

    // re-initialize time keeping variables
    cur_time = 0.0;
    max_time = 0.0, min_time = DBL_MAX, avg_time = 0.0, tot_time = 0.0;

    printf("[rid fwd simulation]: generating random requests out of prefixes in request prefix histogram\n");

    for (itr = request_prefixes.begin(); itr != request_prefixes.end(); itr++) {

        for (int i = 0; i < (itr)->second.size(); i++) {

            // extract the request prefix from the request_prefix map
            strncpy(request_prefix, (itr)->second[i].c_str(), PREFIX_MAX_LENGTH);

            // generate some random name out of the request prefix by adding
            // a random number of elements to it
            if (generate_request_name(&request_name, request_prefix, BF_MAX_ELEMENTS) < 0) {

                fprintf(
                    stderr, 
                    "[rid fwd simulation]: [ERROR] aborted request generation for prefix %s (size %d)\n", 
                    request_prefix, 
                    (itr)->first + 1);

                continue;
            }

            // generate RIDs out of the request names
            request_size = name_to_rid(&request_rid, request_name);

            // // FIXME: based on the mode argument, we may need to change the value
            // // of prefix_size to the Hamming weight (nr. of '1s' in RID)
            // if (mode == HAMMING_WEIGHT)
            //     request_size = rid_hamming_weight(request_rid);

            if (++request_cnt % 100 == 0)
                printf("[rid fwd simulation]: ran %d requests (time elapsed : %-.8f)\n", request_cnt, tot_time);

            // printf("[rid fwd simulation]: lookup for %s started\n", request_name);

            // pass the request RID through the FIBs, gather the
            // lookup stats
            begin = clock();
            pt_ht_lookup(pt_fib, request_name, request_size, request_rid, fp_sizes, tp_sizes);
            end = clock();

            // update the TP stats
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
    pt_ht_print_stats(pt_fib, output_dir);
    pt_ht_erase(pt_fib);
    print_tp_cond(tp_cond, output_dir);

    // struct lookup_stats * itr;

    // for (itr = pt_stats_ht; itr != NULL; itr = (struct lookup_stats *) itr->hh.next) {

    //     HASH_DEL(pt_stats_ht, itr);
    // }

    // strings
    free(prefix);
    free(request_name);
    // click_xia_xid structs
    free(request_rid);
    free(rid);

    return 0;
}
