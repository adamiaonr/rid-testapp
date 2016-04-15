/*
 * pt.c
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

#include "pt.h"

// 1 thread per core?
#define NUM_THREADS 6

int condition_var = 0;
pthread_mutex_t lock;
threadpool_t * pool;

/*
 * \brief   private function used to return whether or not bit i (starting
 *          from the most significant bit) is set in an RID.
 */
static __inline unsigned long bit(int i, struct click_xia_xid * rid) {

    // FIXME: this needs to be changed `a bit(s)' (pun intended): we now have
    // a CLICK_XIA_XID_ID_LEN byte-sized key (the RID) and so i can be in the
    // interval [0, CLICK_XIA_XID_ID_LEN * 8].
    //return key & (1 << (31-i));

    // CLICK_XIA_XID_ID_LEN - ((i / 8) - 1) identifies the byte of the RID
    // where bit i should be
    // FIXME: be careful with endianess (particularly big endianess aka
    // network order, which may be used for XIDs)
    //printf("(%d) %02X vs. %02X = %02X\n", i, rid->id[CLICK_XIA_XID_ID_LEN - (i / 8) - 1], (1 << (8 - i - 1)), rid->id[CLICK_XIA_XID_ID_LEN - (i / 8) - 1] & (1 << (8 - i - 1)));

    return rid->id[CLICK_XIA_XID_ID_LEN - (i / 8) - 1] & (1 << (8 - (i % 8) - 1));
}

/*
 * \brief recursively counts the number of entries in a patricia trie.
 */
static int pt_fwd_count(struct pt_fwd * t, int key_bit) {

    int count;

    if (t->key_bit <= key_bit) return 0;

    count = 1;

    // printf("pt_fwd_get_fp_stats():"\ 
    //     "\n\t[PREFIX] : %s"\
    //     "\n\t[PREFIX SIZE] : %d vs. %d"\
    //     "\n\t[|F\\R|] : %s\n", 
    //     t->stats->prefix,
    //     t->prefix_size, t->stats->prefix_size,
    //     ((t->stats->req_entry_diffs_fps == NULL ? "NULL" : "OK")));

    count += pt_fwd_count(t->p_left,  t->key_bit);
    count += pt_fwd_count(t->p_right, t->key_bit);

    return count;
}

static int pt_ht_erase_rec(
    struct pt_fwd * t, 
    int key_bit) {

    int count;

    if (t->key_bit <= key_bit) {
        return 0;
    }

    count = 1;

    // carfully free the memory of the pt_fwd struct, step by step

    // 1) the prefix_info struct first
    prefix_info_erase(&(t->prefix_i));
    free(t->prefix_i);

    // 2) prefix_rid
    free(t->prefix_rid);

    count += pt_ht_erase_rec(t->p_left,  t->key_bit);
    count += pt_ht_erase_rec(t->p_right, t->key_bit);

    // finally, the pt_fwd * itself
    //printf("pt_ht_erase_rec() : free(t)\n");
    free(t);

    return count;
}

void pt_ht_erase(struct pt_ht * fib) {

    struct pt_ht * itr;
    int count;

    for (itr = fib; itr != NULL; itr = (struct pt_ht *) itr->hh.next) {

        // printf("pt_ht_erase() : erasing for |F| = %d\n", 
        //     itr->prefix_size);
        count += pt_ht_erase_rec(itr->trie, -1);

        // printf("pt_ht_erase() : erasing for |F| = %d (%d)\n", 
        //     itr->prefix_size,
        //     pt_fwd_dummy(itr->trie, -1));

        // printf("pt_ht_erase() : table for |F| = %d : \n", itr->prefix_size);
        // pt_fwd_print(itr->trie, PRE_ORDER);

        // erase the general stats struct from pt_ht
        lookup_stats_erase(&(itr->general_stats));
        free(itr->general_stats);
        // special deletion operation related to uthash
        HASH_DEL(fib, itr);
        // finally free the structure
        free(itr);
    }

    printf("pt_ht_erase() : gone through %d prefixes\n", count);
}

void pt_ht_print_stats(struct pt_ht * fib) {

    // general fwd table stats
    uint32_t num_entries = 0, total_entries = 0, total_sizes = 0;
    // false positive stats
    uint32_t fps_total = 0, tps_total = 0, tns_total = 0, gen_total = 0;

    uint32_t fps_f[MAX_PREFIX_SIZE + 1] = {0};
    uint32_t tps_f[MAX_PREFIX_SIZE + 1] = {0};
    uint32_t tns_f[MAX_PREFIX_SIZE + 1] = {0};
    uint32_t gen_f[MAX_PREFIX_SIZE + 1] = {0};

    uint32_t fps_fr[MAX_PREFIX_SIZE + 1] = {0};
    uint32_t gen_fr[MAX_PREFIX_SIZE + 1] = {0};
    // other stats
    struct lookup_stats lookup = {
        .prefix_info = NULL,
        .req_entry_diffs_fps = NULL,
        .req_entry_diffs = NULL,
        .tps = 0,
        .fps = 0,
        .tns = 0,
        .total_matches = 0
    };

    // output files for each one of the tables
    FILE * output_file = fopen(DEFAULT_ENTRY_FILE, "wb");

    // collect the stats (and print the fwd table stats while doing it)
    struct pt_ht * itr;

    printf(
            "\n-------------------------------------------------------------------------------\n"\
            "%-12s\t| %-12s\t| %-12s\n"\
            "-------------------------------------------------------------------------------\n",
            "|F|", "# ENTRIES", "AVG. FEA");

    for (itr = fib; itr != NULL; itr = (struct pt_ht *) itr->hh.next) {

        // num_entries = pt_fwd_get_stats(itr->trie, -1, fps_f, fps_fr, gen_f, gen_fr, lookup);
        // printf("pt_ht_print_fp_stats() : gathering for |F| = %d\n", itr->prefix_size);

        fps_f[itr->prefix_size] += itr->general_stats->fps;
        tps_f[itr->prefix_size] += itr->general_stats->tps;
        tns_f[itr->prefix_size] += itr->general_stats->tns;
        gen_f[itr->prefix_size] += itr->general_stats->total_matches;

        int _size = 0; 

        if (itr->general_stats->req_entry_diffs_fps != NULL) {

            for (_size; _size <= itr->prefix_size; _size++) {

                fps_fr[_size] += itr->general_stats->req_entry_diffs_fps[_size];
                gen_fr[_size] += itr->general_stats->req_entry_diffs[_size];
            }
        }

        lookup.tps += itr->general_stats->tps;
        lookup.fps += itr->general_stats->fps;
        lookup.tns += itr->general_stats->tns;
        lookup.total_matches += itr->general_stats->total_matches;

        total_entries += itr->num_entries;

        if (itr->prefix_size < 1)
            continue;

        total_sizes++;

        printf(
            "%-12d\t| %-12d\t| %-.6f\n",
            // ((itr->trie->p_left != NULL) ? itr->trie->p_left->prefix_size : itr->trie->p_right->prefix_size),
            itr->prefix_size,
            itr->num_entries,
            itr->fea);

        fprintf(output_file, 
            "%d,%d,%-.6f\n", 
            itr->prefix_size,
            itr->num_entries,
            itr->fea);
    }

    printf(
            "-------------------------------------------------------------------------------\n"\
            "%-12s\t| %-12s\t\n"\
            "-------------------------------------------------------------------------------\n"\
            "%-12d\t| %-12d\t\n",
            "TOTAL |F|", "TOTAL # ENTRIES",
            total_sizes, total_entries);

    printf("\n");

    fclose(output_file);

    // # of FPs vs. |F| (this is mostly for debugging)
    printf(
        "\n-------------------------------------------------------------------------------\n"\
        "%-12s\t| %-12s\t| %-12s\t| %-12s\t| %-12s\t\n"\
        "-------------------------------------------------------------------------------\n",
        "|F|", "# FPs", "# TPs", "# TNs", "# LOOKUPS");

    output_file = fopen(DEFAULT_GEN_STATS_FILE, "wb");

    int _size = 1;
    for (_size; _size < MAX_PREFIX_SIZE + 1; _size++) {

        fps_total += fps_f[_size];
        tps_total += tps_f[_size];
        tns_total += tns_f[_size];
        gen_total += gen_f[_size];

        printf(
                    "%-12d\t| %-12d\t| %-12d\t| %-12d\t| %-12d\t\n",
                    _size, fps_f[_size], tps_f[_size], tns_f[_size], gen_f[_size]);

        fprintf(output_file, 
            "%d,%d,%d,%d,%d\n", 
            _size,
            fps_f[_size],
            tps_f[_size],
            tns_f[_size],
            gen_f[_size]);
    }

    printf(
            "-------------------------------------------------------------------------------\n"\
            "%-12s\t| %-12d\t| %-12d\t| %-12d\t| %-12d\t\n",
            "TOTAL", fps_total, tps_total, tns_total, gen_total);

    fclose(output_file);

    // # of FPs vs. |F\R|
    printf(
        "\n-------------------------------------------------------------------------------\n"\
        "%-20s\t| %-12s\t| %-12s\t\n"\
        "-------------------------------------------------------------------------------\n",
        "REQ-ENTRY DIFF. |F\\R|", "# FPs", "# LOOKUPS");

    output_file = fopen(DEFAULT_REQ_ENTRY_DIFF_FILE, "wb");

    _size = 0; fps_total = 0; gen_total = 0;

    for (_size; _size < MAX_PREFIX_SIZE + 1; _size++) {

        fps_total += fps_fr[_size];
        gen_total += gen_fr[_size];

        printf(
                    "%-20d\t| %-12d\t| %-12d\t\n",
                    _size, fps_fr[_size], gen_fr[_size]);

        fprintf(output_file, 
            "%d,%d,%d\n", 
            _size,
            fps_fr[_size],
            gen_fr[_size]);
    }

    printf(
            "-------------------------------------------------------------------------------\n"\
            "%-20s\t| %-12d\t| %-12d\t\n",
            "TOTAL", fps_total, gen_total);

    printf("\n");

    fclose(output_file);

    lookup_stats_print(&lookup);
}

/*
 * \brief private function used for inserting a node recursively.
 *
 * XXX: note how this method will either return a reference to (1) a
 * `candidate starting node' h; or (2) the node to be inserted itself, n.
 *
 * 2 happens when the current `candidate' parent isn't suitable: we can either
 * find that the decision bit of the parent is either (1) larger than the
 * decision bit of n (which means that n should be a parent of h); or (2)
 * less than that of the grandparent's (which means that ...).
 *
 * \arg h    node were insertion starts (guess it's the insert 'h'ere node)
 * \arg n    new node to insert
 * \arg d    decision bit (?)
 * \arg p    parent of the h node
 *
 *
 */
static struct pt_fwd * insertR(
        struct pt_fwd * h,
        struct pt_fwd * n,
        int d,
        struct pt_fwd * p) {

    if ((h->key_bit >= d) || (h->key_bit <= p->key_bit)) {

        n->key_bit = d;
        n->p_left = bit(d, n->prefix_rid) ? h : n;
        n->p_right = bit(d, n->prefix_rid) ? n : h;

        return n;
    }

    if (bit(h->key_bit, n->prefix_rid)) {

        h->p_right = insertR(h->p_right, n, d, h);

    } else {

        h->p_left = insertR(h->p_left, n, d, h);
    }

    return h;
}

/*
 * \brief inserts a new node into a patricia trie
 *
 * \arg n        new node to insert
 * \arg head    head of the patricia (sub-)trie
 *
 * \return        reference to node, correctly inserted in the trie
 *
 */
struct pt_fwd * pt_fwd_insert(struct pt_fwd * n, struct pt_fwd * head) {

    struct pt_fwd * t;
    int i;

    if (!head || !n)
        return NULL;

    /*
     * Find closest matching leaf node.
     */
    t = head;

    do {

        i = t->key_bit;

        t = bit(t->key_bit, n->prefix_rid) ? t->p_right : t->p_left;

        // XXX: Q: how will t->key_bit be ever less than or equal to i?
        // XXX: A: this is the typical stopping condition for a search in
        // in Patricia tries. since there are no explicit NULL links, we verify
        // if any of the links 'points up' the trie (if you
        // inspect insertR(), you'll see that sometimes t->p_right or
        // t->p_left point to the head of the tree, parent node or
        // itself). this can be done because (by definition) the `key bit' in
        // the nodes increase as one traverses down the trie.

    } while (i < t->key_bit);

    /*
     * Find the first bit that differs.
     */
    for (i = 1; i < ((8 * CLICK_XIA_XID_ID_LEN) - 1) && (bit(i, n->prefix_rid) == bit(i, t->prefix_rid)); i++);

    /*
     * Recursive step.
     * XXX: this is where the actual insertion happens...
     */
    if (bit(head->key_bit, n->prefix_rid)) {

        head->p_right = insertR(head->p_right, n, i, head);

    } else {

        head->p_left = insertR(head->p_left, n, i, head);
    }

    return n;
}


/*
 * remove an entry given a key in a Patricia trie.
 */
int pt_fwd_remove(struct pt_fwd * n, struct pt_fwd * head) {

    // parent, grandparent, ...
    struct pt_fwd *p, *g, *pt, *pp, *t = NULL;
    int i;

    if (!n || !t)
        return 0;

    /*
     * Search for the target node, while keeping track of the
     * parent and grandparent nodes.
     */
    g = p = t = head;

    do {

        i = t->key_bit;
        g = p;
        p = t;
        t = bit(t->key_bit, n->prefix_rid) ? t->p_right : t->p_left;

    } while (i < t->key_bit);

    /*
     * For removal, we need an exact match.
     */
    if (!(rid_compare(t->prefix_rid, n->prefix_rid)))
        return 0;

    /*
     * Don't allow removal of the default entry.
     */
    if (t->key_bit == 0)
        return 0;

    /*
     * Search for the node that points to the parent, so
     * we can make sure it doesn't get lost.
     */
    pp = pt = p;

    do {

        i = pt->key_bit;
        pp = pt;
        pt = bit(pt->key_bit, p->prefix_rid) ? pt->p_right : pt->p_left;

    } while (i < pt->key_bit);

    if (bit(pp->key_bit, p->prefix_rid))
        pp->p_right = t;
    else
        pp->p_left = t;

    /*
     * Point the grandparent to the proper node.
     */
    if (bit(g->key_bit, n->prefix_rid))
        g->p_right = bit(p->key_bit, n->prefix_rid) ?
            p->p_left : p->p_right;
    else
        g->p_left = bit(p->key_bit, n->prefix_rid) ?
            p->p_left : p->p_right;

    /*
     * Delete the target's data and copy in its parent's
     * data, but not the bit value.
     */
    if (t != p) {
        t->prefix_rid = p->prefix_rid;
    }

    free(p);

    return 1;
}

struct pt_fwd * pt_fwd_init(struct pt_ht * ht) {

    // allocate memory for a new patricia trie (in this case a single node, or
    // fwd entry).
    struct pt_fwd * root = (struct pt_fwd *) malloc(sizeof(struct pt_fwd));

    int i = 0;

    // pointer to root node of RID subtree
    root->fib_root = ht;

    // we must now allocate memory a root RID with all bits set
    // to 0
    struct click_xia_xid * root_rid = (struct click_xia_xid *) malloc(sizeof(struct click_xia_xid));

    for (i = 0; i < CLICK_XIA_XID_ID_LEN; i++)
        root_rid->id[i] = 0x00;

    root_rid->type = CLICK_XIA_XID_TYPE_RID;

    // assign that RID with the root's prefix RID
    root->prefix_rid = root_rid;

    // initialize the struct lookup_stats * attribute w/ an empty
    // prefix string
    struct prefix_info * pi = (struct prefix_info *) malloc(sizeof(struct prefix_info));
    char * prefix = (char *) calloc(PREFIX_MAX_LENGTH, sizeof(char));

    pi->prefix = prefix;
    pi->prefix_size = 0;

    root->prefix_i = pi;

    // initialize the rest of the root's attributes and set it's pointers to
    // point to the root itself
    root->prefix_size = 0;
    root->key_bit = 0;
    root->p_left = root->p_right = root;

    return root;
}

struct pt_ht * pt_ht_search(struct pt_ht * ht, int prefix_size) {

    struct pt_ht * s;

    HASH_FIND_INT(ht, &prefix_size, s);

    return s;
}

int pt_ht_sort(struct pt_ht * a, struct pt_ht * b) {

    return (a->prefix_size - b->prefix_size);
}

/*
 * find an entry given a key in a patricia trie.
 */
struct pt_fwd * pt_fwd_search(struct click_xia_xid * rid, struct pt_fwd * head) {

    struct pt_fwd * t = head;
    int i;

    if (!t)
        return 0;

    /*
     * find closest matching leaf node.
     */
    do {

        i = t->key_bit;
        t = bit(t->key_bit, rid) ? t->p_right : t->p_left;

    } while (i < t->key_bit);

    /*
     * Compare keys to see if this
     * is really the node we want.
     */
    return (rid_compare(rid, t->prefix_rid) ? t : NULL);
}

int pt_ht_add(
        struct pt_ht ** ht,
        struct click_xia_xid * rid,
        char * prefix,
        int prefix_size) {

    struct pt_ht * s;
    struct pt_fwd * f = (struct pt_fwd *) malloc(sizeof(struct pt_fwd));

    // FIXME: why do you always need to complicate things?
    f->prefix_rid = (struct click_xia_xid *) malloc(sizeof(struct click_xia_xid));
    memcpy(f->prefix_rid, rid, sizeof(struct click_xia_xid));
    // f->prefix_rid = rid;

    f->prefix_size = prefix_size;
    f->p_right = NULL;
    f->p_left = NULL;

    // for control purposes, we also create and fill a prefix_info struct
    // and set f->prefix_i to point to it
    f->prefix_i = (struct prefix_info *) malloc(sizeof(struct prefix_info));
    prefix_info_init(&(f->prefix_i), prefix, prefix_size);

    HASH_FIND_INT(*ht, &prefix_size, s);

    if (s == NULL) {

        s = (struct pt_ht *) malloc(sizeof(struct pt_ht));

        s->prefix_size = prefix_size;
        s->num_entries = 0;

        // initialize the trie with a `all-zero' root
        s->trie = pt_fwd_init(s);

        // FIXME: temporary hack to keep track of one more stat
        s->fea = 0.0;
        s->fea_n = 0;

        s->general_stats = (struct lookup_stats *) malloc(sizeof(struct lookup_stats));
        lookup_stats_init(&(s->general_stats), prefix, prefix_size);

        if (!(s->trie)){

            //fprintf(stderr, "[fwd table build]: pt_fwd_init() failed\n");

        } else {

            HASH_ADD_INT(*ht, prefix_size, s);

            // when inserting new elements in the HT, sort by prefix size
            HASH_SORT(*ht, pt_ht_sort);

            //printf("[fwd table build]: pt_fwd_init() successful\n");
        }
    }

    // set the pointer to root node of RID subtree
    f->fib_root = s;

    struct pt_fwd * srch = NULL;

    if ((srch = pt_fwd_search(f->prefix_rid, s->trie)) != NULL) {

        //printf("[fwd table build]: node with %s (vs. %s) exists!\n", f->stats->prefix, srch->stats->prefix);
        prefix_info_erase(&(f->prefix_i));
        free(f->prefix_i);
        free(f->prefix_rid);
        free(f);

    } else {

        if (!(pt_fwd_insert(f, s->trie))) {

            printf("pt_fwd_insert() : ERROR pt_fwd_insert() failed\n");
            
            prefix_info_erase(&(f->prefix_i));
            free(f->prefix_i);
            free(f->prefix_rid);
            free(f);

            return -1;
        }

        s->num_entries++;
    }

    return 0;
}

/*
 * \brief     longest prefix matching on a patricia trie, accounting for
 *             false positives
 *
 * the advantage of this scheme is a potential reduction in lookup time: while
 * in the LSHT scheme we have O(|R| * avg(|HT|)) expected time (we have to
 * lookup all HTs for which |F| \le |R|), which includes all cases - TPs, FPs
 * and TNs - with a PT of RIDs we avoid looking up entire sub-tries made up of
 * TNs.
 *
 * XXX: it would be interesting to quantify how much do we gain, but i don't
 * know how to estimate the number of TNs in this way...
 *
 * organizing the FIB in multiple PTs indexed by Hamming weight (HW) as in
 * Papalini et al. 2014 does not translate into longer prefix matches for
 * larger HWs: since bits in BFs can be overwritten during the encoding
 * operation, we can have situations in which HW(R1) > HW(R2) and |R1| < |R2|
 * (theoretical results in (...)). Therefore, an indication of the number of
 * prefixes (similar to that of a `mask') must always be present in the trie
 * nodes.
 *
 * \return
 */
int pt_fwd_lookup(
        struct pt_fwd * node,
        char * request,
        int request_size,
        struct click_xia_xid * request_rid,
        int prev_key_bit,
        uint32_t * fp_sizes,
        uint32_t * tp_sizes) {

    if (node->key_bit <= prev_key_bit) {

       // printf("pt_fwd_lookup(): going upwards for (R) %s (%d vs. %d)\n",
       //         request,
       //         node->key_bit,
       //         prev_key_bit);

        return 0;
    }

    uint32_t _req_entry_diff = req_entry_diff(request, node->prefix_i->prefix, node->prefix_size);

    int tps = 0, fps = 0, tns = 0;
    int matches = 0;

    // XXX: when looking up a PT with FPs, we always follow the left branch,
    // and selectively follow the right branch.

    // "why?", you ask...

    // going left means there's
    // a '0' bit at position key_bit: it doesn't matter if it maps to a '0' or '1'
    // in request, the matching test ((R & F) == F) won't be false because of
    // this fact. going right means there's a '1' at position key_bit: if it maps
    // to a '0' in request, then ((R & F) == F) will fail, and so we avoid
    // traversing the right sub-trie.

    // XXX: apply a mask to the key_bit leftmost bits of node and request, check if
    // ((mask(request) & mask(prefix)) == mask(prefix)): if yes, follow the
    // p_right branch, if not, avoid it.
    if (rid_match_mask(request_rid, node->prefix_rid, node->key_bit)) {

        // XXX: check for a match, now without masks on
        // FIXME: note we're avoiding matches with the default route (or root
        // node) by checking if node->prefix_size > 0
        if (rid_match(request_rid, node->prefix_rid) && (node->prefix_size > 0)) {

            // TP or FP check: this requires the consultation of the backup
            // char * on f->stats for substrings of request
            //printf("%s vs. %s\n", node->prefix_i->prefix, request);

            if (strstr(request, node->prefix_i->prefix) != NULL) {

                // printf("pt_fwd_lookup(): TP %s\n", node->prefix_i->prefix);

                tps = 1;
                matches += tps;

            } else {

                fps = 1;
                matches += fps;
            }

        } else {

            // TN check: directly maps to a simple pass or fail of a normal
            // RID matching operation
            tns = 1;
            matches += tns;
        }

        // if (fps > 0) {

        //        char * p = (char *) calloc(CLICK_XIA_XID_ID_STR_LEN, sizeof(char));
        //        char * r = (char *) calloc(CLICK_XIA_XID_ID_STR_LEN, sizeof(char));

        //        printf("pt_fwd_lookup(): (R) %s vs. (P) %s\n", request, node->prefix_i->prefix);
        //        printf("pt_fwd_lookup(): (R) %s vs. (P) %s\n",
        //                extract_prefix_bytes(&r, request_rid, node->key_bit),
        //                extract_prefix_bytes(&p, node->prefix_rid, node->key_bit));

        //        // free string memory
        //        free(r);
        //        free(p);

        //        printf("pt_fwd_lookup(): FP %s\n", node->prefix_i->prefix);
        // }

        // FIXME: update the general statistics in the FIB root node. note that 
        // this way the entry-specific stats are sort of useless.
        lookup_stats_update(
            &(node->fib_root->general_stats), 
            _req_entry_diff, 
            tps, fps, tns, 1);

        if (node->prefix_size > 0) {
            fp_sizes[node->prefix_size - 1] += fps;
            tp_sizes[node->prefix_size - 1] += tps;
        }

        if (tps > 1) {

            return matches;
        }

        matches += pt_fwd_lookup(node->p_right, request, request_size, request_rid, node->key_bit, fp_sizes, tp_sizes);

    } else {

        tns = 1;
        matches += tns;

        lookup_stats_update(
            &(node->fib_root->general_stats), 
            _req_entry_diff, 
            tps, fps, tns, 1);

        if (node->prefix_size > 0) {
            fp_sizes[node->prefix_size - 1] += fps;
            tp_sizes[node->prefix_size - 1] += tps;
        }
    }

    matches += pt_fwd_lookup(node->p_left, request, request_size, request_rid, node->key_bit, fp_sizes, tp_sizes);

    return matches;
}

void pt_fwd_lookup_thread(void * t_args) {

    struct pt_fwd_lookup_tdata * t_data = (pt_fwd_lookup_tdata *) t_args;

    pt_fwd_lookup(
        t_data->node, 
        t_data->request, 
        t_data->request_size, 
        t_data->request_rid, 
        -1, 
        t_data->fp_sizes, 
        t_data->tp_sizes);

    pthread_mutex_lock(&lock);
    condition_var--;
    pthread_mutex_unlock(&lock);

    // printf("pt_fwd_lookup_thread(): finished lookup for prefix size %d. condition_var = %d\n",
    //     t_data->node->fib_root->prefix_size,
    //     condition_var);
}

void pt_ht_lookup(
        struct pt_ht * pt_fib,
        char * request,
        int request_size,
        struct click_xia_xid * request_rid,
        uint32_t * fp_sizes,
        uint32_t * tp_sizes) {

    // find the largest prefix size which is less than or equal than the
    // request size
    struct pt_ht * s = NULL;
    struct pt_ht * itr = NULL;
    int prefix_size = request_size;
    uint32_t total_matches = 0;

    assert((pool = threadpool_create(NUM_THREADS, MAX_PREFIX_SIZE * 2, 0)) != NULL);
    // printf("pt_ht_lookup(): thread pool started with %d threads and %d jobs\n", 
    //         NUM_THREADS, MAX_PREFIX_SIZE + 1);

    while ((s == NULL) && prefix_size > 0)
        s = pt_ht_search(pt_fib, prefix_size--);

    // iterate the HT back from prefix_size to 1 to get all possible matching
    // prefix sizes
    // XXX: we are guaranteed (?) to follow a decreasing order because we
    // sort the pt_ht (by prefix size) every time we add a new entry
    struct pt_fwd * f = NULL;
    
    struct pt_fwd_lookup_tdata * t_args = (struct pt_fwd_lookup_tdata *) calloc(MAX_PREFIX_SIZE + 1, sizeof(struct pt_fwd_lookup_tdata));
    // using global variables is sad, and you should feel bad...
    condition_var = request_size + 1;

    for (itr = s; itr != NULL; itr = (struct pt_ht *) itr->hh.prev) {

        // linear search-match on the entry's patricia trie...
        f = itr->trie;

        if (!f) {

            fprintf(stderr, "pt_ht_lookup(): no trie for (%s, %d)\n", request, request_size);

            continue;
        }

        // just start the whole recursive lookup on the patricia trie
        // FIXME: the '-1' is an hack and an ugly one
        //total_matches = pt_fwd_lookup(f, request, request_size, request_rid, -1, fp_sizes, tp_sizes);

        // FIXME: we will be using threads and a threadpool 
        // prepare the pt_fwd_lookup_tdata struct to pass as argument to 
        // threadpool_add()
        t_args[itr->prefix_size].thread_id = itr->prefix_size;
        t_args[itr->prefix_size].node = f;
        t_args[itr->prefix_size].request = request;
        t_args[itr->prefix_size].request_size = request_size;
        t_args[itr->prefix_size].request_rid = request_rid;
        t_args[itr->prefix_size].prev_key_bit = -1;
        t_args[itr->prefix_size].fp_sizes = fp_sizes;
        t_args[itr->prefix_size].tp_sizes = tp_sizes;

        // we use 1 thread per PT subtree. we use a pool of NUM_THREADS and 
        // run MAX_PREFIX_SIZE jobs per each request. we do not use locks 
        // on fp_sizes and tp_sizes, as these are just accumulators, only read 
        // at the end of a lookup (note that the parallelization only happens 
        // per PT subtree, i.e. we don't lookup requests in parallel)
        assert(threadpool_add(pool, &pt_fwd_lookup_thread, &t_args[itr->prefix_size], 0) == 0);

        // if (tp_sizes[s->prefix_size] > 0)
        //     break;
    }

    while (condition_var > 0) {
        usleep(1000);
    }

    // destroy them threads in them pool...
    assert(threadpool_destroy(pool, 0) == 0);

    free(t_args);
}

void pt_fwd_print(
        struct pt_fwd * node,
        uint8_t mode) {

    // FIXME: only one mode is supported as of now...
    if (mode == PRE_ORDER) {

        if (node) {

            // string which will hold the representations of the patricia trie nodes
            char * prefix_bytes = (char *) calloc(CLICK_XIA_XID_ID_STR_LEN, sizeof(char));

            // we only print the `prefix bytes', i.e. those which contain the
            // key_bit leftmost bits

            // left child
            printf("[%s (%d)] <-- ",
                    extract_prefix_bytes(&prefix_bytes, node->p_left->prefix_rid, node->p_left->key_bit),
                    node->p_left->key_bit);

            memset(prefix_bytes, 0, strlen(prefix_bytes));

            // node itself
            printf("[%s (%d)]",
                    extract_prefix_bytes(&prefix_bytes, node->prefix_rid, node->key_bit),
                    node->key_bit);

            memset(prefix_bytes, 0, strlen(prefix_bytes));

            // right child
            printf(" --> [%s (%d)]\n",
                    extract_prefix_bytes(&prefix_bytes, node->p_right->prefix_rid, node->p_right->key_bit),
                    node->p_right->key_bit);

            free(prefix_bytes);

            if (node->p_left->key_bit > node->key_bit)
                pt_fwd_print(node->p_left, PRE_ORDER);

            if (node->p_right->key_bit > node->key_bit)
                pt_fwd_print(node->p_right, PRE_ORDER);
        }

    } else {

        fprintf(stderr, "pt_fwd_print() : UNKNOWN MODE (%d)\n", mode);
    }
}
