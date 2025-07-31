# usage: python3 elapsed_time_parser.py <elapsed time log path> <variables log path>

import sys
import matplotlib.pyplot as plt
from matplotlib.ticker import MaxNLocator
import math
import pandas as pd

# Updated after parse_file() called
logdata = {}
part_idx_mx = 0
turnaround_times = {}

def parse_file(filename):
    global logdata, part_idx_mx, turnaround_times

    logdata = {}
    part_idx_mx = 0
    turnaround_times = {}

    with open(filename, mode="r", encoding="utf-8") as f:
        for line in f:
            ret = line.rstrip().split()
            session_name, part_idx, loop_idx, timestamp, data = ret
            part_idx = int(part_idx)
            loop_idx = int(loop_idx)
            timestamp = int(timestamp)

            if not part_idx in logdata:
                logdata[part_idx] = []
                part_idx_mx = max(part_idx_mx, part_idx)

            logdata[part_idx].append(timestamp)

    for i in range(0, part_idx_mx):
        turnaround_times[i] = []
        for j in range(len(logdata[0])):
            turnaround_times[i].append(logdata[i+1][j] - logdata[i][j])

    for i in range(0, part_idx_mx):
        sorted_times = sorted(turnaround_times[i])
        length = len(sorted_times)
        print("{}: part index = {}".format(session_name, i + 1))
        print("50%tile {}us".format(sorted_times[math.ceil(length * 0.5 - 1)]));
        print("90%tile {}us".format(sorted_times[math.ceil(length * 0.9 - 1)]));
        print("99%tile {}us".format(sorted_times[math.ceil(length * 0.99 - 1)]));
        print("------------------------")

    return session_name

def L2norm(a):
    s = 0
    for i in range(len(a)):
        s += float(a[i]) * float(a[i])
    s = math.sqrt(s)
    return s

def parse_file_variables(filename):
    var_id = dict()
    var_names = []
    vi = 0
    pair_vec = []
    is_single = []
    with open(filename, mode="r", encoding="utf-8") as f:
        for line in f:
            ret = line.rstrip().split()
            session_name = ret[0]
            loop_idx = int(ret[1])
            var_name = ret[2]
            data = 0
            #print(len(ret))
            if len(ret) >= 4:
                # calculate L2 norm
                if not (var_name in var_id.keys()):
                    var_id[var_name] = vi
                    var_names.append(var_name)
                    vi+=1
                    if len(ret) == 4:
                        is_single.append(1)
                    else:
                        is_single.append(0)
                    pair_vec.append([])
                if len(turnaround_times[1])<=loop_idx:
                    continue
                pair_vec[var_id[var_name]].append((L2norm(ret[3:]),turnaround_times[1][loop_idx]))
            else:
                continue
    print(len(pair_vec))

    # calculate correlation and sort by corr
    corr_idx = []
    for i in range(len(pair_vec)):
        l1 = []
        l2 = []
        for p in pair_vec[i]:
            l1.append(p[0])
            l2.append(p[1])
        s1 = pd.Series(l1)
        s2 = pd.Series(l2)
        corr = s1.corr(s2)
        if not math.isnan(corr):
            corr_idx.append((corr,i))
    corr_idx = sorted(corr_idx)
    for id in range(len(corr_idx)):
        corr = corr_idx[id][0]
        i = corr_idx[id][1]
        print(var_names[i]+" corr = {}".format(corr))
        plt.figure(figsize=(8, 8))
        plt.title(var_names[i]+" corr={}".format(corr))
        if is_single[i]:
            plt.xlabel(var_names[i])
        else:
            plt.xlabel(var_names[i] + " L2 norm")
        plt.ylabel("turn-around time (us) in part 1")
        l1 = []
        l2 = []
        for p in pair_vec[i]:
            l1.append(p[0])
            l2.append(p[1])
        plt.scatter(l1, l2)
        plt.savefig("var_fig/{}.png".format(var_names[i]))


def visualize(session_name, bins=50):
    for i in range(part_idx_mx):
        fig = plt.figure(figsize=(16, 16))
        max_value = max(turnaround_times[i])

        ax0 = fig.add_subplot(2, 1, 1)
        ax0.set_title("{}: part {} - elapsed time time-series".format(session_name, i))
        ax0.set_xlabel("sample index")
        ax0.set_ylabel("turn-around time (us)")
        ax0.set_ylim([0, max_value])
        ax0.plot(turnaround_times[i])

        ax1 = fig.add_subplot(2, 1, 2)
        ax1.set_title("{}: part {} - elapsed time histgram".format(session_name, i))
        ax1.set_xlabel("turn-around time (us)")
        ax1.set_ylabel("the number of samples")
        ax1.set_xlim([0, max_value])
        ax1.hist(turnaround_times[i], bins=bins)

        plt.savefig("{}.part{}_histgram.pdf".format(session_name, i))

if __name__ == "__main__":
    # python elapsed_time_parser.py time_log variables_log 
    session_name = parse_file(sys.argv[1])
    parse_file_variables(sys.argv[2])
    visualize(session_name)

