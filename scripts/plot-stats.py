from datetime import date
from datetime import datetime
import matplotlib.pyplot as plt
import matplotlib.mlab as mlab
from matplotlib.pyplot import figure, show
from matplotlib import *
import matplotlib
import time
import sys
import os
import numpy as np
import collections
import math

# in honor of "stack'd", the pittsburgh restaurant next to my shadyside 
# place

LABEL_FONT_SIZE=12
LEGEND_FONT_SIZE=10  
BF_MAX_SIZE=15
BAR_WIDTH=1.0

# pre-determined input file names
file_names = ['entry.dat', 'gen-stats.dat', 'req-entry-diff.dat', 'tp-size.dat']

def custom_ceil(x, base=5):
#    return int(base * math.ceil(float(x)/base))
    return math.ceil(float(x)) + 1

def main():

    if len(sys.argv) < 2:
        print "usage: python plot-stats.py <input-file-dir> <output-file-dir>"
        return

    # keep track of reading time... for research purposes of course
    start_time = time.time()
    # fetch the dir where the .dat files are
    input_file_dir = sys.argv[1]

    # add a placeholder for each possible |F|, |F\R| or |TP|
    _entry_data = []
    _gen_stats_data = []
    _req_entry_diff_data = []
    _tp_size_data = []

    for i in xrange(BF_MAX_SIZE + 1):
        _entry_data.append([])
        _gen_stats_data.append([])
        _req_entry_diff_data.append([])
        _tp_size_data.append([])

    # read each .dat file in input_file_dir
    for file_name in file_names:

        _file = open(input_file_dir + "/" + file_name, 'rb')

        for line in _file.readlines():
            line_splitted = line.split(",") 

            try:

                index = int(line_splitted[0])

                if (file_name == 'entry.dat'):
                    _entry_data[index].append([int(line_splitted[1]), float(line_splitted[2])])
                elif (file_name == 'gen-stats.dat'):
                    _gen_stats_data[index].append([int(line_splitted[1]), int(line_splitted[2]), int(line_splitted[3]), int(line_splitted[4])])
                elif (file_name == 'req-entry-diff.dat'):
                    _req_entry_diff_data[index].append([int(line_splitted[1]), int(line_splitted[2])])
                elif (file_name == 'tp-size.dat'):
                    # also include the nr. of TPs in _tp_size_data
                    _tp_size_data[index].append([_gen_stats_data[index][0][1], int(line_splitted[1]), int(line_splitted[2])])                    
                else:
                    print "ERROR : unknown input file name : " + str(file_name)
            
            except IndexError:

                print "index : " + str(index)
                print "file name : " + str(file_name)
                print "line : " + line

                return

    elapsed_time = time.time() - start_time
    print "[READ FILES IN " + str(elapsed_time) + " sec]"

    # now lets generate a 2 x 2 plot, i.e. with 4 subplots, one for each data series
    fig = plt.figure()

    subplot_code = (2 * 100) + (2 * 10)

    x = np.arange(15)
    _x = np.arange(0, 15, 2)

    y = []

    # onto the graphing...

    # ************************
    # |F| distr.
    # ************************

    subplot_code += 1
    _entry_plot = fig.add_subplot(subplot_code)
    _entry_plot.set_title("# Entries per entry size |F|", fontsize=LABEL_FONT_SIZE)

    for i in xrange(BF_MAX_SIZE):
        y.append(_entry_data[i + 1][0][0])

    _entry_plot.grid(True)
    _entry_plot.bar(x - (BAR_WIDTH / 2.0), y, BAR_WIDTH, color='blue')

    _entry_plot.set_xlabel("Entry size |F|", fontsize=LABEL_FONT_SIZE)
    _entry_plot.set_ylabel('# of entries', fontsize=LABEL_FONT_SIZE)

    # x axis is the most complicated
    _entry_plot.set_xlim(min(x) - 1, max(x) + 1)
    _entry_plot.set_xticks(_x)
    _entry_plot.set_xticklabels(_x + 1)
    x_labels = _entry_plot.get_xticklabels()
    plt.setp(x_labels, rotation=0, fontsize=LABEL_FONT_SIZE)

    # set y axis limits after drawing the rest of the graph
    _log_max_y = int(math.log10(max(y)))
    _ceil_max_y = math.ceil(max(y) / math.pow(10, int(math.log10(max(y)))))
    _ceil_max_y = _ceil_max_y * math.pow(10, _log_max_y)

    _entry_plot.set_ylim(0, _ceil_max_y)
    y_ticks = np.arange(0, _ceil_max_y + 1, _ceil_max_y / 5)
    _entry_plot.set_yticks(y_ticks)

    y_ticks_labels = []
    y_ticks_labels.append('0')
    for i in xrange(1, len(y_ticks)):
        y_ticks_labels.append(str(int(y_ticks[i] / 1000)) + "k")
    _entry_plot.set_yticklabels(y_ticks_labels)
    y_labels = _entry_plot.get_yticklabels()
    plt.setp(y_labels, rotation=0, fontsize=LABEL_FONT_SIZE)

    # ************************
    # GENERAL STATS
    # ************************

    subplot_code += 1
    _gen_stats_plot = fig.add_subplot(subplot_code)
    _gen_stats_plot.set_title('# FPs, # TPs and # TNs per entry size', fontsize=LABEL_FONT_SIZE)

    y_max = 0

    y = []
    for i in xrange(BF_MAX_SIZE):
        y.append(_gen_stats_data[i + 1][0][0])

    if max(y) > y_max:
        y_max = max(y)

    a = _gen_stats_plot.plot(x, y, '-v', linewidth=2, color='red')

    y = []
    for i in xrange(BF_MAX_SIZE):
        y.append(_gen_stats_data[i + 1][0][1])

    if max(y) > y_max:
        y_max = max(y)

    b = _gen_stats_plot.plot(x, y, '-o', linewidth=2, color='green')

    y = []
    for i in xrange(BF_MAX_SIZE):
        y.append(_gen_stats_data[i + 1][0][2])

    if max(y) > y_max:
        y_max = max(y)

    c = _gen_stats_plot.plot(x, y, '-^', linewidth=2, color='blue')

    _gen_stats_plot.set_xlabel("Entry size |F|", fontsize=LABEL_FONT_SIZE)
    _gen_stats_plot.set_ylabel('# of events', fontsize=LABEL_FONT_SIZE)

    _gen_stats_plot.set_xlim(min(x) - 1, max(x) + 1)
    _gen_stats_plot.set_xticks(_x)
    _gen_stats_plot.set_xticklabels(_x + 1)
    x_labels = _gen_stats_plot.get_xticklabels()
    plt.setp(x_labels, rotation=0, fontsize=LABEL_FONT_SIZE)

    # set y axis limits after drawing the rest of the graph
    _log_max_y = int(math.ceil(math.log10(y_max)))
    _ceil_max_y = math.pow(10, _log_max_y)

    _gen_stats_plot.set_yscale('log')
    _gen_stats_plot.set_ylim(0, _ceil_max_y)
    y_labels = _gen_stats_plot.get_yticklabels()
    plt.setp(y_labels, rotation=0, fontsize=LABEL_FONT_SIZE)

    _gen_stats_plot.legend((a[0], b[0], c[0]), ('#FPs', '#TPs', '#TNs'), loc='center right', fontsize=LEGEND_FONT_SIZE)

    # ************************
    # REQ. ENTRY DIFF |F\R|
    # ************************

    subplot_code += 1
    _req_entry_diff_plot = fig.add_subplot(subplot_code)
    _req_entry_diff_plot.set_title('# FPs per req.-entry diff. |F\R|', fontsize=LABEL_FONT_SIZE)

    y_max = 0

    y = []
    for i in xrange(BF_MAX_SIZE):
        y.append(_req_entry_diff_data[i + 1][0][0])

    if max(y) > y_max:
        y_max = max(y)

    a = _req_entry_diff_plot.plot(x, y, '-v', linewidth=2, color='red')

    _req_entry_diff_plot.set_xlabel("Request-Entry diff. |F\R|", fontsize=LABEL_FONT_SIZE)
    _req_entry_diff_plot.set_ylabel('# FPs', fontsize=LABEL_FONT_SIZE)

    _req_entry_diff_plot.set_xlim(min(x) - 1, max(x) + 1)
    _req_entry_diff_plot.set_xticks(_x)
    _req_entry_diff_plot.set_xticklabels(_x + 1)
    x_labels = _req_entry_diff_plot.get_xticklabels()
    plt.setp(x_labels, rotation=0, fontsize=LABEL_FONT_SIZE)

    # set y axis limits after drawing the rest of the graph
    _log_max_y = int(math.ceil(math.log10(y_max)))
    _ceil_max_y = math.pow(10, _log_max_y)

    _req_entry_diff_plot.set_yscale('log')
    _req_entry_diff_plot.set_ylim(0, _ceil_max_y)
    y_labels = _req_entry_diff_plot.get_yticklabels()
    plt.setp(y_labels, rotation=0, fontsize=LABEL_FONT_SIZE)

    #_req_entry_diff_plot.legend((a[0]), ('#FPs'), loc='lower right', fontsize=LEGEND_FONT_SIZE)

    # ************************
    # TP SIZES
    # ************************

    subplot_code += 1
    _tp_size_plot = fig.add_subplot(subplot_code)
    _tp_size_plot.set_title('# FPs larger/equal than TP size', fontsize=LABEL_FONT_SIZE)

    y_max = 0

    y = []
    for i in xrange(BF_MAX_SIZE):
        y.append(_tp_size_data[i + 1][0][0])

    if max(y) > y_max:
        y_max = max(y)

    a = _tp_size_plot.plot(x, y, '-o', linewidth=2, color='green')

    y = []
    for i in xrange(BF_MAX_SIZE):
        y.append(_tp_size_data[i + 1][0][1])

    if max(y) > y_max:
        y_max = max(y)

    b = _tp_size_plot.plot(x, y, '-v', linewidth=2, color='red')

    y = []
    for i in xrange(BF_MAX_SIZE):
        y.append(_tp_size_data[i + 1][0][2])

    if max(y) > y_max:
        y_max = max(y)

    c = _tp_size_plot.plot(x, y, '-^', linewidth=2, color='orange')

    _tp_size_plot.set_xlabel("True positive size |TP|", fontsize=LABEL_FONT_SIZE)
    _tp_size_plot.set_ylabel('# of events', fontsize=LABEL_FONT_SIZE)

    _tp_size_plot.set_xlim(min(x) - 1, max(x) + 1)
    _tp_size_plot.set_xticks(_x)
    _tp_size_plot.set_xticklabels(_x + 1)
    x_labels = _tp_size_plot.get_xticklabels()
    plt.setp(x_labels, rotation=0, fontsize=LABEL_FONT_SIZE)

    # set y axis limits after drawing the rest of the graph
    _log_max_y = int(math.ceil(math.log10(y_max)))
    _ceil_max_y = math.pow(10, _log_max_y)

    _tp_size_plot.set_yscale('log')
    _tp_size_plot.set_ylim(0, _ceil_max_y)
    y_labels = _tp_size_plot.get_yticklabels()
    plt.setp(y_labels, rotation=0, fontsize=LABEL_FONT_SIZE)

    _tp_size_plot.legend((a[0], b[0], c[0]), ('#TPs', '#FPs : |FP|=|TP|', '#FPs : |FP|>=|TP|'), loc='upper right', fontsize=LEGEND_FONT_SIZE)

    plt.tight_layout(pad=0.4, w_pad=1.0, h_pad=1.0)
    plt.savefig(sys.argv[2] + "/stats.png", bbox_inches='tight')

if __name__ == "__main__":
    main()
