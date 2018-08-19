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

FONTSIZE_LEGEND = 16

def extract_data(data_dir):

    data = defaultdict(OrderedDict)

    for file_name in sorted(glob.glob(os.path.join(data_dir, '*.csv'))):

        file_type = file_name.split("/")[-1].split(".")[0]
        file_label = file_name.split("/")[-1].split(".")[1]

        print("type = %s, label = %s" % (file_type, file_label))

        data[file_type][file_label] = pd.read_csv(file_name)
        data[file_type][file_label] = data[file_type][file_label].convert_objects(convert_numeric = True)

    return data

def plot_entry_sizes(data, output_dir):

    router_fp_rates = defaultdict(list)
    bf_fp_rates = defaultdict(list)
    fp_rates_diff = defaultdict(list)

    # labels and colors for bars (events)
    diff_labels = ['Small req-entry diff ($|F \\backslash R|$)', 'Uniform $|F \\backslash R|$', 'Large $|F \\backslash R|$']
    # diff_labels_alt = ['Small $|F\\backslash R|$', 'Unif. $|F \\backslash R|$', 'Large $|F \\backslash R|$']
    diff_labels_alt = ['Small $|F\\backslash R|$', 'Unif.', 'Large']
    diff_colors = ['#bebebe', '#708090', 'black']

    fp_rates_diff_labels = ['Short $|F|$,\nsmall $|F \\backslash R|$', 'Short $|F|$,\nlarge $|F \\backslash R|$', 'Long $|F|$,\nsmall $|F \\backslash R|$', 'Long $|F|$,\nlarge $|F \\backslash R|$']
    fp_rates_diff_colors = ['black', 'black', 'black', 'black']
    fp_rates_diff_styles = ['-', '--', '-.', ':']
    fp_rates_diff_markrs = ['o', 'v', '^', 'o']
    fp_rates_diff_markrs_size = [10, 10, 10, 8]

    aggr_types = []
    f_r_diffs = []
    bf_sizes = []

    for key in data:

        print(key)

        if key == "FPTAGGR":

            for sub_key in data[key]:

                print(sub_key)

                aggr_type = sub_key.split("-", 1)[1]
                if aggr_type not in aggr_types:
                    aggr_types.append(aggr_type)

                for c in data[key][sub_key].columns:
                    router_fp_rates[c].append(data[key][sub_key][c][0])

        elif key == "FPTAGGRM":

            for sub_key in data[key]:

                print(sub_key)

                bf_size = int(sub_key.split("-", 1)[0].lstrip("BF").lstrip("0"))

                if bf_size == 1024:
                    continue

                if bf_size not in bf_sizes:
                    bf_sizes.append(bf_size)

                for c in data[key][sub_key].columns:
                    bf_fp_rates[c].append(data[key][sub_key][c][0])

        # elif key == "FPTFR":

        #     for sub_key in data[key]:

        #         for c in data[key][sub_key].columns:
        #             fp_rates_diff[sub_key].append(data[key][sub_key][c][0])

        else:

            print("unknown key: %s" % (key))

    # print(fp_rates_diff)
    # print(bf_fp_rates)
    # print(router_fp_rates)
    # print(aggr_types)
    print(bf_sizes)

    #matplotlib.style.use('ggplot')
    plt.style.use('classic')
    matplotlib.rcParams.update({'font.size': 15})
    matplotlib.rcParams['mathtext.fontset'] = 'dejavusans'
    matplotlib.rcParams['mathtext.rm'] = 'sans'
    matplotlib.rcParams['mathtext.it'] = 'sans:italic'
    matplotlib.rcParams['mathtext.bf'] = 'sans:bold'
    
    # avoid Type 3 fonts
    matplotlib.rcParams['pdf.fonttype'] = 42
    matplotlib.rcParams['ps.fonttype'] = 42

    fig = plt.figure(figsize = (5, 3.5))
    ax1 = fig.add_subplot(121)
    ax1.xaxis.grid(False)
    ax1.yaxis.grid(True)
    
    # we have 3 modes and 2 correctness values per mode. hence 3 groups of 
    # bars, each group with 2 bars.
    bar_group_size = len(diff_labels)
    bar_group_num = 1

    # assumes the inter bar group space is half a bar. also, for n bar groups 
    # we have n - 1 inter bar group spaces
    m = -(float(bar_group_num * bar_group_size) / 2.0) - ((bar_group_num - 1) / 2.0)
    bar_width = 0.40

    show_legend = True
    for k, f_r_diff in {0:'SMALL', 1:'UNIF', 2:'LARGE'}.iteritems():

        print(np.arange(1, (2 * len(aggr_types)), step = 2) + (m * bar_width))
        print(np.array(router_fp_rates[f_r_diff]))

        ax1.bar(np.arange(1, (2 * len(aggr_types)), step = 2) + (m * bar_width), np.array(router_fp_rates[f_r_diff]), color = diff_colors[k], linewidth = 1.5, alpha = 0.75, width = bar_width, label = diff_labels_alt[k])
        m += 1.0

    ax1.set_title("(a)")
    ax1.set_yscale('log')
    ax1.set_ylim(0.00001, 10000.0)

    # ax1.set_xlabel("Distribution of entry sizes ($|F|$)\n in fwd. table w/ $10^7$ entries")
    ax1.set_xlabel("Distr. of entry sizes ($|F|$)")
    ax1.set_ylabel("Router-level FP rate")
    # ax1.set_xticks(np.arange(1, (2 * len(bf_sizes)), step = 2), bf_sizes)
    ax1.set_yticks([0.00001, 0.0001, 0.001, 0.01, 0.1, 1.0])
    # ax1.legend(fontsize = FONTSIZE_LEGEND, ncol = 3, loc = 'upper center', title = '$|F\\backslash R|$ distr. skew',
    #     handletextpad = 0.2, handlelength = 1.0, labelspacing = 0.2, columnspacing = 0.5)

    xticks = [01.0, 3.0, 5.0]
    xtick_labels = ['Short', 'Unif.', 'Long']
    ax1.set_xticks(xticks)
    ax1.set_xticklabels(xtick_labels)

    # -------

    ax2 = fig.add_subplot(122)
    ax2.xaxis.grid(False)
    ax2.yaxis.grid(True)
    
    # we have 3 modes and 2 correctness values per mode. hence 3 groups of 
    # bars, each group with 2 bars.
    bar_group_size = len(diff_labels)
    bar_group_num = 1

    # assumes the inter bar group space is half a bar. also, for n bar groups 
    # we have n - 1 inter bar group spaces
    m = -(float(bar_group_num * bar_group_size) / 2.0) - ((bar_group_num - 1) / 2.0)
    bar_width = 0.40

    show_legend = True
    for k, f_r_diff in {0:'SMALL', 1:'UNIF', 2:'LARGE'}.iteritems():

        print(np.arange(1, (2 * len(bf_sizes)), step = 2) + (m * bar_width))
        print(np.array(router_fp_rates[f_r_diff]))

        ax2.bar(np.arange(1, (2 * len(bf_sizes)), step = 2) + (m * bar_width), np.array(bf_fp_rates[f_r_diff]), color = diff_colors[k], linewidth = 1.5, alpha = 0.75, width = bar_width, label = diff_labels_alt[k])
        m += 1.0

    ax2.set_title("(b)")
    ax2.set_yscale('log')
    ax2.set_ylim(0.0000000001, 100000000.0)

    ax2.set_xlabel("BF size (bit)")
    # ax2.set_ylabel("Router-level FP rate")
    # ax2.set_xticks(np.arange(1, (2 * len(bf_sizes)), step = 2), bf_sizes)
    ax2.set_yticks([0.0000000001, 0.00000001, 0.000001, 0.0001, 0.01, 1.0])
    ax2.legend(fontsize = FONTSIZE_LEGEND, ncol = 3, title = '$|F\\backslash R|$ distribution skew',
        handletextpad = 0.2, handlelength = 1.0, labelspacing = 0.2, columnspacing = 0.5,
        bbox_to_anchor=(1.025, 1.000))

    xticks = [1.0, 3.0, 5.0]
    xtick_labels = ['192', '256', '512']
    ax2.set_xticks(xticks)
    ax2.set_xticklabels(xtick_labels)

    # ax3 = fig.add_subplot(133)
    # ax3.grid(True)

    # k = 0
    # for mode in fp_rates_diff:
    #     ax3.plot(np.arange(1, 16), np.array(fp_rates_diff[mode]), linewidth = 1.5, color = fp_rates_diff_colors[k], linestyle = fp_rates_diff_styles[k], markersize = fp_rates_diff_markrs_size[k], marker = fp_rates_diff_markrs[k], label = fp_rates_diff_labels[k])
    #     k += 1

    # ax3.set_title("(c)")
    # ax3.set_yscale('log')
    # ax3.set_ylim(1E-16, 1.0)
    # # ax3.set_ylabel("Avg. nr. of deliveries")

    # ax3.set_xlabel("Request size |R|")
    # xticks = [0.6, 1.0, 1.4, 2.6, 3.0, 3.4, 4.6, 5.0, 5.4, 6.6, 7.0, 7.4]
    # xtick_labels = ['10k', '\n256', '1M', '', '\n512', '', '', '\n1024', '', '', '\nIdeal', '']
    # ax3.set_xticks(np.arange(1, 16, step = 2))
    # # ax2.set_xticklabels(xtick_labels)
    # # ax2.set_yticks([0.0, 0.5, 1.0, 1.5])
    # ax3.legend(fontsize=11, ncol=1, loc='lower right')

    fig.tight_layout()
    fig.subplots_adjust(left = None, bottom = None, right = None, top = None, wspace = 0.45, hspace = None)
    plt.savefig(os.path.join(output_dir, "fp-rate-table.pdf"), bbox_inches = 'tight', format = 'pdf')

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

    if not args.output_dir:
        sys.stderr.write("""%s: [ERROR] please supply an output dir!\n""" % sys.argv[0]) 
        parser.print_help()
        sys.exit(1)

    # extract the data from all files in the data dir
    data = extract_data(args.data_dir)

    if args.case == 'entry-sizes':
        plot_entry_sizes(data, args.output_dir)
    else:
        sys.stderr.write("""%s: [ERROR] please supply a valid case\n""" % sys.argv[0]) 
        parser.print_help()
        sys.exit(1)