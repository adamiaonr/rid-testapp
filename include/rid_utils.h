/*
 * rid_utils.h
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

#ifndef _RID_UTILS_H_
#define _RID_UTILS_H_

#include <stdio.h>
#include <stdlib.h>     /* free(), malloc() */
#include <string.h>     /* bcopy() */
#include <stdint.h>
#include <float.h>

#include "bloom.h"

#define CLICK_XIA_XID_TYPE_RID      (0x50)
#define CLICK_XIA_XID_ID_LEN        20

#define CLICK_XIA_XID_ID_STR_LEN    (2*20 + 2)

#define PREFIX_DELIM                (char * ) "/"
#define PREFIX_MAX_COUNT            256
#define PREFIX_MAX_LENGTH           256

#define DEFAULT_ENTRY_FILE              "test/entry.dat"
#define DEFAULT_GEN_STATS_FILE          "test/gen-stats.dat"
#define DEFAULT_REQ_ENTRY_DIFF_FILE     "test/req-entry-diff.dat"
#define DEFAULT_TP_SIZE_FILE            "test/tp-size.dat"

#define MAX_PREFIX_SIZE             15

struct click_xia_xid {
    uint32_t type;
    uint8_t id[CLICK_XIA_XID_ID_LEN];
};

extern char * extract_prefix_bytes(char ** to, struct click_xia_xid * rid, int trailing_bit);
extern unsigned int req_entry_diff(char * request, char * entry, unsigned int entry_size);
extern int count_prefixes(char * prefix);
extern int name_to_rid(struct click_xia_xid ** rid, char * _prefix);
extern int rid_compare(struct click_xia_xid * a, struct click_xia_xid * b);
extern int rid_match(struct click_xia_xid * req, struct click_xia_xid * fwd);
extern int rid_match_mask(
        struct click_xia_xid * req,
        struct click_xia_xid * fwd,
        int trailing_bit);

extern int rid_hamming_weight(struct click_xia_xid * rid);

#endif /* _RID_UTILS_H_ */
