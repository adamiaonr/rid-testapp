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

matplotlib.rcParams.update({'font.size': 24})

FONTSIZE_LEGEND = 21

def extract_data(data_dir):

    data = defaultdict(OrderedDict)

    for file_name in sorted(glob.glob(os.path.join(data_dir, '*.tsv'))):

        file_type = file_name.split("/")[-1].split(".")[0]
        file_label = file_name.split("/")[-1].split(".")[1]

        print("type = %s, label = %s" % (file_type, file_label))

        data[file_type][file_label] = pd.read_csv(file_name, sep = "\t")
        data[file_type][file_label] = data[file_type][file_label].convert_objects(convert_numeric = True)

    return data

def plot_tp_sizes(data):    

    entry_sizes = defaultdict(list)
    tp_sizes    = defaultdict(defaultdict)
    fp_totals   = defaultdict(int)

    # entry_size_labels = ['$2\,10^6$ entries, $|F|_{min} = 1$', '$3\,10^6$ entries, $|F|_{min} = 3$']
    # entry_size_colors = ['#bebebe', '#000000']
    # tp_size_labels = ['$|F|_{min} = 1$, $|F|=|TP|$', '$|F|_{min} = 1$, $|F|>|TP|$', '$|F|_{min} = 3$, $|F|=|TP|$', '$|F|_{min} = 3$, $|F|>|TP|$']
    entry_size_labels = ['2M entries']
    entry_size_colors = ['#000000', '#bebebe']

    # for value_type in data:

    #     for f_min in data[value_type]:

    #         print(f_min)
    #         f = int(f_min.lstrip("f").lstrip("0"))  

    #         if value_type == "entry":

    #             entry_sizes[f] = data[value_type][f_min]["NUM_ENTRIES"].tolist()

    #             print(value_type)
    #             print(f_min)
    #             print(entry_sizes[f])

    #         elif value_type == "tp-size":

    #             print(value_type)
    #             print(f_min)

    #             tp_sizes[f]["FP_EQUAL"] = data[value_type][f_min]["FP_EQUAL"].tolist()
    #             tp_sizes[f]["FP_LARGER"] = data[value_type][f_min]["FP_LARGER"].tolist()
    #             fp_totals[f] = sum(tp_sizes[f]["FP_EQUAL"]) + sum(tp_sizes[f]["FP_LARGER"])

    #             print(tp_sizes[f]["FP_EQUAL"])
    #             print(tp_sizes[f]["FP_LARGER"])
    #             print(fp_totals[f])

    # (a) cdf of entry sizes
    # #matplotlib.style.use('ggplot')
    fig = plt.figure(figsize=(10,3.5))
    # 2 subplots : entry size distribution (left), tp sizes (right)
    ax1 = fig.add_subplot(121)
    ax1.xaxis.grid(True)
    ax1.yaxis.grid(True)

    # list entry sizes from highest to lowest
    a = data['entry']['f01']['NUM_ENTRIES']
    print(a)
    # apply a cumulative sum over the values
    # last element contains the sum of all values in the array
    acc = np.array(a.cumsum(), dtype = float)
    # normalize the values (last value becomes 1.0)
    # now we have the cdf
    acc = acc / acc[-1]

    ax1.plot(np.arange(1, 11, 1), acc, 
        alpha = 0.75, linewidth = 1.5, color = '#000000', linestyle = '-')

    ax1.set_title("(a) # of fwd. entries")
    ax1.set_xlim(0.5, 10.5)
    ax1.set_ylim(0.0, 1.05)
    ax1.set_yticks([0.0, 0.25, 0.50, 0.75, 1.0])

    ax1.set_xlabel("Fwd. entry size, $|F|$")
    ax1.set_ylabel("CDF")
    ax1.set_xticks(np.arange(1, (10 + 1), step = 1))
    # ax1.set_xticklabels(['1', '3', '5', '7', '9', '11', '13', '15'])
    # ax1.set_yticks([1.0, 10, 100, 1000, 10000, 100000, 1000000, 1000000])
    # ax1.legend(fontsize=FONTSIZE_LEGEND, ncol=1, loc='upper center')

    # # 2 subplots : entry size distribution (left), tp sizes (right)
    # ax2 = fig.add_subplot(122)
    # ax2.grid(True)

    # l = 0
    # for k, v in {0:1}.iteritems():
    #     for tp_type in tp_sizes[v]:

    #         ax2.plot(np.arange(1 + (v - 1), (10 + 1), step = 1), np.array(tp_sizes[v][tp_type][(v-1):]), linewidth = 1.5, color = tp_size_colors[k], linestyle = tp_size_styles[l], marker = tp_size_markers[l], markersize = tp_size_markrs_size[l], label = tp_size_labels[l])
    #         l += 1

    # ax2.set_title("(b)")
    # ax2.set_xlim((bar_width / 2.0), 11 - (bar_width / 2.0))
    # ax2.set_ylim(1, 1000000)
    # ax2.set_yscale('log')

    # ax2.set_xlabel("Size of TP entries $|TP|$")
    # ax2.set_ylabel("# of FPs")
    # ax2.set_yticks([1.0, 10, 100, 1000, 10000, 100000, 1000000])
    # ax2.set_xticks(np.arange(1, 10 + 1, step = 1))
    # ax2.set_xticklabels(['1', '2', '3', '4', '5', '6', '7', '8', '9', '10'])tp_size_styles[l]
    # ax2.legend(fontsize=FONTSIZE_LEGEND, ncol=1, loc='upper right')

    ax2 = fig.add_subplot(122)
    ax2.xaxis.grid(True)
    ax2.yaxis.grid(True)

    labels = ['$|FP|=|TP|$', '$|FP|>|TP|$']
    colors = ['#000000', 'grey']
    styles = ['-', '--']
    markers = ['v', 'o']
    markers_size = [10, 10, 8, 10]

    # list entry sizes from highest to lowest
    for l, label in enumerate(['FP_EQUAL', 'FP_LARGER']):
        a = data['tp-size']['f01'][label]
        print(a)
        acc = np.array(a.cumsum(), dtype = float)
        acc = acc / acc[-1]

        ax2.plot(np.arange(1, 11, 1), acc, 
            alpha = 0.75, linewidth = 1.5, 
            label = labels[l], 
            color = colors[l], 
            linestyle = styles[l], 
            marker = markers[l], markersize = markers_size[l])

    ax2.set_title("(b) # of FPs")
    ax2.set_xlim(0.5, 10.5)
    ax2.set_ylim(0.90, 1.005)
    ax2.set_yticks([0.90, 0.925, 0.950, 0.975, 1.0])

    ax2.set_xlabel("FP size, $|FP|$")
    ax2.set_ylabel("CDF")
    ax2.set_xticks(np.arange(1, (10 + 1), step = 1))

    ax2.legend(fontsize = FONTSIZE_LEGEND, ncol = 1, loc = 'lower right')

    fig.subplots_adjust(left=None, bottom=None, right=None, top=None, wspace=0.5, hspace=None)

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

    args = parser.parse_args()

    if not args.data_dir:
        sys.stderr.write("""%s: [ERROR] please supply a data dir!\n""" % sys.argv[0]) 
        parser.print_help()
        sys.exit(1)

    if not args.output_dir:
        args.output_dir = args.data_dir

    data = extract_data(args.data_dir)

    if args.case == 'tp-sizes':
        plot_tp_sizes(data)
    else:
        sys.stderr.write("""%s: [ERROR] please supply a valid case\n""" % sys.argv[0]) 
        parser.print_help()
        sys.exit(1)