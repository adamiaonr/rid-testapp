import pandas as pd
import numpy as np
import matplotlib
import matplotlib.pyplot as plt
import os
import argparse
import sys
import glob

from datetime import date
from datetime import datetime
from collections import defaultdict
from collections import OrderedDict

matplotlib.rcParams.update({'font.size': 18})

FONTSIZE_LEGEND = 14

def extract_data(data_dir):

    data = defaultdict(OrderedDict)

    for file_name in sorted(glob.glob(os.path.join(data_dir, '*.tsv'))):

        file_type = file_name.split(".")[0].split("/")[-1]
        file_label = file_name.split(".")[1]

        print("type = %s, label = %s" % (file_type, file_label))

        data[file_type][file_label] = pd.read_csv(file_name, sep = "\t")
        data[file_type][file_label] = data[file_type][file_label].convert_objects(convert_numeric = True)

    return data

def plot_tp_sizes(data):    

    entry_sizes = defaultdict(list)
    tp_sizes    = defaultdict(defaultdict)
    fp_totals   = defaultdict(int)

    entry_size_labels = ['$|F|_{min} = 1$', '$|F|_{min} = 3$']
    entry_size_colors = ['#bebebe', '#000000']
    tp_size_labels = ['$|F|_{min} = 1$, $|F|=|TP|$', '$|F|_{min} = 1$, $|F|>|TP|$', '$|F|_{min} = 3$, $|F|=|TP|$', '$|F|_{min} = 3$, $|F|>|TP|$']
    tp_size_colors = ['grey', '#000000']
    tp_size_styles = ['-', '', '-', '']
    tp_size_markers = ['v', 's', 'o', '^']
    tp_size_markrs_size = [10, 10, 8, 10]

    for value_type in data:

        for f_min in data[value_type]:

            f = int(f_min.lstrip("f").lstrip("0"))

            if value_type == "entry":

                entry_sizes[f] = data[value_type][f_min]["NUM_ENTRIES"].tolist()

                print(value_type)
                print(f_min)
                print(entry_sizes[f])

            elif value_type == "tp-size":

                print(value_type)
                print(f_min)

                tp_sizes[f]["FP_EQUAL"] = data[value_type][f_min]["FP_EQUAL"].tolist()
                tp_sizes[f]["FP_LARGER"] = data[value_type][f_min]["FP_LARGER"].tolist()
                fp_totals[f] = sum(tp_sizes[f]["FP_EQUAL"]) + sum(tp_sizes[f]["FP_LARGER"])

                print(tp_sizes[f]["FP_EQUAL"])
                print(tp_sizes[f]["FP_LARGER"])
                print(fp_totals[f])

    # #matplotlib.style.use('ggplot')
    fig = plt.figure(figsize=(10,4))
    # 2 subplots : entry size distribution (left), tp sizes (right)
    ax1 = fig.add_subplot(121)
    ax1.grid(True)

    bar_group_size = len(entry_size_labels)
    bar_group_num = 15

    # assumes the inter bar group space is 0 bar. also, for n bar groups 
    # we have n - 1 inter bar group spaces
    m = -(float(bar_group_num * bar_group_size) / 2.0)
    bar_width = 0.50

    x_min = 0.0

    bar_group_offset = np.arange(1, (2 * 15), step = 2)[0] + (m * bar_width)
    bar_group_width = (bar_group_num * bar_group_size + 1) * bar_width

    x_max = np.arange(1, (2 * 15), step = 2)[0] + (m * bar_width)
    x_max += np.arange(1, (2 * 15), step = 2)[-1] + ((bar_group_num * bar_group_size + 1) / 2.0) * bar_width

    print(bar_group_size)
    print(bar_group_num)
    print(bar_group_offset)
    print(bar_group_width)
    print("[%d, %d]" % (x_min, x_max))

    # print the bars for each |L|, for both cache distances
    for k, v in {0:1, 1:3}.iteritems():

        ax1.bar(np.arange(1, (15 + 1), step = 1) + (m * bar_width), np.array(entry_sizes[v]), color = entry_size_colors[k], linewidth = 1.5, alpha = 0.75, width = bar_width, label = entry_size_labels[k])
        m += 1.0

    ax1.set_yscale('log')
    ax1.set_xlim(-7.0, 9)
    ax1.set_ylim(1.0, 100000000)

    ax1.set_xlabel("Entry size |F|")
    ax1.set_ylabel("Nr. of fwd. entries")
    ax1.set_xticks(np.arange(-6, 8 + 1, step = 2))
    ax1.set_xticklabels(['1', '3', '5', '7', '9', '11', '13', '15'])
    ax1.set_yticks([1.0, 100, 10000, 1000000, 100000000])
    ax1.legend(fontsize=FONTSIZE_LEGEND, ncol=1, loc='upper right')

    # 2 subplots : entry size distribution (left), tp sizes (right)
    ax2 = fig.add_subplot(122)
    ax2.grid(True)

    # print the bars for each |L|, for both cache distances
    l = 0
    for k, v in {0:1, 1:3}.iteritems():
        for tp_type in tp_sizes[v]:

            ax2.plot(np.arange(1 + (v - 1), (15 + 1), step = 1), np.array(tp_sizes[v][tp_type][(v-1):]), linewidth = 1.5, color = tp_size_colors[k], linestyle = tp_size_styles[l], marker = tp_size_markers[l], markersize = tp_size_markrs_size[l], label = tp_size_labels[l])
            l += 1

    ax2.set_ylim(0.1, 1000000)
    ax2.set_yscale('log')

    ax2.set_xlabel("True positive size |TP|")
    ax2.set_ylabel("Nr. of FPs larger/equal to TP")
    ax2.set_xticks(np.arange(1, 15 + 1, step = 2))
    ax2.set_xticklabels(['1', '3', '5', '7', '9', '11', '13', '15'])
    ax2.legend(fontsize=FONTSIZE_LEGEND, ncol=1, loc='upper right')

    fig.subplots_adjust(left=None, bottom=None, right=None, top=None, wspace=0.3, hspace=None)

    plt.savefig("fp-stats.pdf", bbox_inches='tight', format = 'pdf')

if __name__ == "__main__":

    # use an ArgumentParser for a nice CLI
    parser = argparse.ArgumentParser()

    # options (self-explanatory)
    parser.add_argument(
        "--data-dir", 
         help = """dir w/ .tsv files.""")
    parser.add_argument(
        "--output-dir", 
         help = """dir on which to print graphs.""")
    parser.add_argument(
        "--case", 
         help = """the case you want to output. e.g. 'base', 'bf-sizes', etc.""")
    parser.add_argument(
        "--subcase", 
         help = """the sub-case you want to output. e.g. 'flood' or 'random' for 'base'.""")

    args = parser.parse_args()

    # quit if a dir w/ causality files hasn't been provided
    if not args.data_dir:
        sys.stderr.write("""%s: [ERROR] please supply a data dir!\n""" % sys.argv[0]) 
        parser.print_help()
        sys.exit(1)

    # if an output dir is not specified, use data-dir
    if not args.output_dir:
        args.output_dir = args.data_dir

    # extract the data from all files in the data dir
    data = extract_data(args.data_dir)

    if args.case == 'tp-sizes':
        plot_tp_sizes(data)
    else:
        sys.stderr.write("""%s: [ERROR] please supply a valid case\n""" % sys.argv[0]) 
        parser.print_help()
        sys.exit(1)