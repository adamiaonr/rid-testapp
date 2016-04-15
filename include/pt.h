/*
 * pt.h
 *
 * Patricia trie implementation.
 *
 * Functions for inserting nodes, removing nodes, and searching in
 * a Patricia trie designed for IP addresses and netmasks.  A
 * head node must be created with (key,mask) = (0,0).
 *
 * NOTE: The fact that we keep multiple masks per node makes this
 *       more complicated/computationally expensive then a standard
 *       trie.  This is because we need to do longest prefix matching,
 *       which is useful for computer networks, but not as useful
 *       elsewhere.
 *
 * Matthew Smart <mcsmart@eecs.umich.edu>
 *
 * Copyright (c) 2000
 * The Regents of the University of Michigan
 * All rights reserved
 *
 * Altered by Antonio Rodrigues <antonior@andrew.cmu.edu> for test with RIDs.
 *
 * $Id$
 */

#ifndef _PT_H_
#define _PT_H_

#include <stdio.h>
#include <stdlib.h>    /* free(), malloc() */
#include <string.h>    /* bcopy() */
#include <stdint.h>
#include <unistd.h>
#include <assert.h>
#include <pthread.h>

#include "uthash.h"
// playing with fire... i mean threads now...
#include "threadpool.h"
#include "rid_utils.h"
#include "lookup_stats.h"

// XXX: modes for printing a patricia trie
#define PRE_ORDER       0x00
#define IN_ORDER        0x01
#define POST_ORDER      0x02

struct pt_ht {

    // prefix size (in number of encoded elements)
    int prefix_size;

    // nr. of forwarding entries
    uint32_t num_entries;

    // pointer to the fwd entry list
    struct pt_fwd * trie;

    // FIXME: just a aux parameter for keeping track of 'forwarding entry
    // avoidance' percentage per RID size
    double fea;
    unsigned long fea_n;

    // general statistics for the RID FIB 
    struct lookup_stats * general_stats;

    // makes this structure hashable
    UT_hash_handle hh;
};

/*
 * \brief patricia trie fwd table node for an RID FIB.
 */
struct pt_fwd {

    // pointer to root node of RID subtree
    struct pt_ht * fib_root;

    // node's XID (RID in our case)
    struct click_xia_xid * prefix_rid;

    // number of prefixes encoded in the node's XID
    // XXX: probably not necessary (?)
    int prefix_size;

    // bit to check (aka `key bit')
    int key_bit;

    // left and right pointers
    struct pt_fwd * p_left;
    struct pt_fwd * p_right;

    struct prefix_info * prefix_i;
};

struct pt_fwd_lookup_tdata {

    int thread_id;
    int condition_var;

    struct pt_fwd * node;

    char * request;
    int request_size;
    
    struct click_xia_xid * request_rid;

    int prev_key_bit;
    
    uint32_t * fp_sizes;
    uint32_t * tp_sizes;
};

extern void pt_ht_erase(struct pt_ht * fib);
extern void pt_ht_print_stats(struct pt_ht * fib);
extern struct pt_ht * pt_ht_search(struct pt_ht * ht, int prefix_size);
extern int pt_ht_add(
        struct pt_ht ** ht,
        struct click_xia_xid * rid,
        char * prefix,
        int prefix_size);

extern void pt_ht_lookup(
        struct pt_ht * pt_fib,
        char * request,
        int request_size,
        struct click_xia_xid * request_rid,
        uint32_t * fp_sizes,
        uint32_t * tp_sizes);

extern void pt_fwd_print(struct pt_fwd * node, uint8_t mode);

#endif /* _PT_H_ */
