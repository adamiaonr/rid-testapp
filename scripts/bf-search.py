import os
import sys
import argparse
import urllib
import requests 
import math

from pdfminer.pdfinterp import PDFResourceManager, PDFPageInterpreter
from pdfminer.converter import TextConverter
from pdfminer.layout import LAParams
from pdfminer.pdfpage import PDFPage
from cStringIO import StringIO
from pybloom import BloomFilter
from random import randint
from collections import defaultdict

BF_ELEMENT_NUM = 10
MAX_NUM_KEYS = 1000

class KeySet:

    def __init__(self, key_list):
        # create a BloomFilter object
        self.bf = BloomFilter(capacity = BF_ELEMENT_NUM, error_rate = 0.0001)
        # add elements in key_list to the bf
        [self.bf.add(key) for key in key_list]
        # keep track of the real list of keys
        self.key_list = key_list

    def __str__(self):
        return ("""m = %d, ns = %d, bps = %d, n = %d, k = %d""" 
            % (self.bf.num_bits, self.bf.num_slices, self.bf.bits_per_slice, self.bf.capacity, 
                int(math.ceil(math.log(2) * (self.bf.num_bits / self.bf.capacity)))))

    def query(self, query_set):
        # calculate nr. of bits set to '1'
        common_bits = self.bf.intersection(query_set.bf).bitarray.count()
        # determine which keys are common
        common_keys = list(set(query_set.key_list) & set(self.key_list))

        if len(common_keys) > 0:
            print("KeySet::query() : # of common bits : %d, common keys (%d / %d) : %s" % (common_bits, len(common_keys), len(self.key_list), common_keys))
        return common_bits, common_keys

def pdf_to_txt(pdf_filename):

    # don't go ahead if the .txt file for path already exists
    if os.path.exists(pdf_filename.replace(".pdf", ".txt")):
        print("%s::pdf_to_txt() : [INFO] .txt file already exists : %s" % (sys.exc_info()[0], pdf_filename.replace(".pdf", ".txt")))
        return 0

    rsrcmgr = PDFResourceManager()
    retstr = StringIO()
    codec = 'utf-8'
    laparams = LAParams()
    device = TextConverter(rsrcmgr, retstr, codec=codec, laparams=laparams)
    fp = file(pdf_filename, 'rb')
    interpreter = PDFPageInterpreter(rsrcmgr, device)
    password = ""
    maxpages = 0
    caching = True
    pagenos=set()

    try:
        for page in PDFPage.get_pages(fp, pagenos, maxpages=maxpages, password=password, caching=caching, check_extractable=True):
            interpreter.process_page(page)
    except:
        print("%s::pdf_to_txt() : [ERROR] exception reading .pdf file : %s" % (sys.exc_info()[0], pdf_filename))
        return -1

    # extract the text from the read pages
    text = retstr.getvalue()
    # save it as <filename>.txt
    text_file = open(pdf_filename.replace(".pdf", ".txt"), "w")
    text_file.write(text)
    text_file.close()

    fp.close()
    device.close()
    retstr.close()

    return 0

def extract_keys(txt_filename):

    with open(txt_filename) as f:
        text = f.read()
        keys = list(set(text.split()))

    return keys

def build_sets(keys, max_set_size):

    num_keys = len(keys)
    print("%s::build_sets() : [INFO] num of unique keys : %d" % (sys.exc_info()[0], num_keys))
    num_sets = int(math.floor(MAX_NUM_KEYS / max_set_size))
    # for the case the key universe is too small
    if num_sets == 0:
        num_sets = 1

    set_db = defaultdict(list)
    queries = defaultdict()

    # build the key sets, size by size, starting with 1 key to max_set_size keys
    for set_size in range(1, max_set_size + 1, 1):
        for s in range(num_sets):
            # pick n key at random, add it to key_list
            key_list = []
            rand_k = randint(0, num_keys - 1)
            print("%s::build_sets() : [INFO] num of unique keys : %d" % (sys.exc_info()[0], num_keys))
            [key_list.append(keys[randint(0, num_keys - 1)]) for n in range(set_size)]

            # add the set to the database
            new_set = KeySet(key_list)
            set_db[set_size].append(new_set)

        # use the last added database set as the query for size set_size
        queries[set_size] = new_set

    return set_db, queries

if __name__ == "__main__":

    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--pdf-file", 
         help = """.pdf file""")
    parser.add_argument(
        "--txt-file", 
         help = """.txt file""")

    args = parser.parse_args()

    if not (args.pdf_file or args.txt_file):
        sys.stderr.write("""%s: [ERROR] please supply an input file\n""" % (sys.argv[0])) 
        parser.print_help()
        sys.exit(1)

    keys = []
    txt_filename = ""

    if args.txt_file:
        txt_filename = args.txt_file
    else:

        if pdf_to_txt(args.pdf_file) == 0:
            txt_filename = args.pdf_file.replace(".pdf", ".txt")

    if txt_filename:
        keys = extract_keys(txt_filename)
    else:
        sys.stderr.write("""%s: [ERROR] could not extract keys\n""" % (sys.argv[0])) 
        parser.print_help()
        sys.exit(1)

    set_db, queries = build_sets(keys, BF_ELEMENT_NUM)

    results = defaultdict(defaultdict)

    for query in queries:
        _query = queries[query]
        print("query : %d elements (%s), bf info : %s" % (len(_query.key_list), _query.key_list, _query))
        for _set in set_db:
            for x in set_db[_set]:
                c_bits, c_keys = x.query(_query)

                if len(x.key_list) not in results[len(_query.key_list)]:
                    results[len(_query.key_list)][len(x.key_list)] = defaultdict(list)

                results[len(_query.key_list)][len(x.key_list)]['c_bits'].append(c_bits)
                results[len(_query.key_list)][len(x.key_list)]['c_keys'].append(c_keys)
