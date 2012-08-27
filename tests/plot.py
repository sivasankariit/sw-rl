#!/usr/bin/python
import sys
import argparse
import termcolor as T
import re
from collections import defaultdict
import matplotlib as mp
import matplotlib.pyplot as plt
import glob
import os
import numpy

parser = argparse.ArgumentParser(description="Plot netperf experiment outputs.")
parser.add_argument('--rr',
                    nargs="+",
                    help="rr files to parse")

parser.add_argument('--ss-dir',
                    help="stream output directories")

parser.add_argument('--out', '-o',
                    help="save plot to file")

parser.add_argument('--ymin',
                    type=float,
                    help="zoom into (ymin,1) on yaxis")

parser.add_argument('--xlog',
                    action="store_true",
                    help="make x-axis log scale")

args = parser.parse_args()
rspaces = re.compile(r'\s+')

def cdf(lst):
    vals = []
    nums = []
    cum = 0
    for val, num in lst:
        cum += num
        vals.append(val)
        nums.append(cum)
    return vals, map(lambda n: n*1.0/cum, nums)

def plot_cdf(x, y, **opts):
    #plt.figure()
    plt.plot(x, y, **opts)
    if args.xlog:
        plt.xscale("log")
    #plt.show()


class STParser:
    def __init__(self, filename):
        self.filename = filename
        self.lines = open(filename).readlines()
        self.done = False
        try:
            self.parse()
            self.done = True
        except:
            print 'error parsing %s' % filename
        return

    def parse(self):
        mbps_line = self.lines[6]
        fields = rspaces.split(mbps_line)
        self.mbps = float(fields[5])
        self.cpu_local = float(fields[6])
        return

class RRParser:
    def __init__(self, filename):
        self.filename = filename
        self.lines = open(filename).readlines()
        self.done = False
        try:
            self.parse()
            self.done = True
        except:
            print 'error parsing %s' % filename

    def parse(self):
        tps_line = self.lines[6]
        fields = rspaces.split(tps_line)
        self.tps = float(fields[5])
        self.cpu_local = float(fields[6])
        self.cpu_remote = float(fields[7])

        lat_line = self.lines[11]
        fields = rspaces.split(lat_line)
        self.latency = float(fields[4])
        self.mbps_out = float(fields[6])
        self.mbps_in = float(fields[7])
        self.parse_histogram()
        return

    def parse_histogram(self):
        unit = 1
        rsep = re.compile(r':\s+')
        ret = defaultdict(int)
        def parse_buckets(line):
            nums = line.split(":", 1)[1]
            nums = map(lambda e: int(e.strip()),
                       rsep.split(nums))
            return nums
        for lno in xrange(14, 22):
            nums = parse_buckets(self.lines[lno])
            for i,n in enumerate(nums):
                ret[unit+i*unit] += n
            unit *= 10
        ret = sorted(list(ret.iteritems()))
        self.histogram = ret
        return ret

def parse_st(dir):
    total_mbps = 0.0
    total_cpu_local = 0.0
    num_files = 0
    for f in glob.glob(os.path.join("%s/*" % dir)):
        r = STParser(f)
        if not r.done:
            continue
        total_mbps += r.mbps
        total_cpu_local += r.cpu_local
        num_files += 1
    avg_cpu_local = total_cpu_local / num_files
    return (dir, total_mbps, avg_cpu_local)

def plot_vbar(heights, start, skip, **kwargs):
    N = len(heights)
    xs = start + skip * numpy.arange(0, N)
    plt.bar(xs, heights, width=1, **kwargs)

def plot_rr():
    hist = defaultdict(int)
    total_tps = 0
    total_out_mbps = 0
    total_in_mbps = 0
    total_cpu_remote = 0
    def combine(hnew):
        for val,num in hnew:
            hist[val] += num
        return
    for f in args.rr:
        r = RRParser(f)
        if not r.done:
            continue
        c = cdf(r.histogram)
        combine(r.histogram)
        plot_cdf(c[0], c[1], alpha=0.1)

        total_tps += r.tps
        total_out_mbps += r.mbps_out
        total_in_mbps += r.mbps_in
        total_cpu_remote += r.cpu_remote
    agg_cdf = cdf(sorted(list(hist.iteritems())))
    plot_cdf(agg_cdf[0], agg_cdf[1], lw=2, color='red')
    plt.xlim((0, 1e4))
    plt.figure(1).get_axes()[0].yaxis.set_major_locator(mp.ticker.MaxNLocator(10))
    plt.grid(True)
    plt.xlabel("usec")
    plt.ylabel("fraction")
    title = "Total tps: %.3f / %.3fMbps IN / %.2f%%CPU" % (total_tps, total_in_mbps, total_cpu_remote / len(args.rr))
    title += '\n(norm: %.3f Mbps/CPU%%) ' % (total_in_mbps/(total_cpu_remote/len(args.rr)))
    plt.title(title)
    if args.ymin is not None:
        plt.ylim((args.ymin, 1))
    if args.out is None:
        plt.show()
    else:
        print 'saved to %s' % args.out
        plt.savefig(args.out)

if args.rr:
    plot_rr()
else:
    colours = dict(none="green", htb="red", newrl="blue")
    rls = ["htb", "newrl", "none"]
    ssizes = [64, 128, 256, 512, 1440, 32000]
    for start,rl in enumerate(rls):
        ys = []
        for ssize in ssizes:
            dir = "rl-%s-ssize-%s" % (rl, ssize)
            if not os.path.exists(os.path.join(args.ss_dir, dir)):
                continue
            _, mbps, cpu = parse_st(dir)
            print _, mbps, cpu
            ys.append(mbps/cpu)
        plot_vbar(ys, start, skip=4, color=colours[rl])
    L = len(rls)
    xticks = L/2.0 + (L+1) * numpy.arange(0, len(ssizes))
    xticklabels = map(lambda e: str(e), ssizes)
    plt.xticks(xticks, xticklabels)
    plt.ylabel("Mb/s per CPU%")
    plt.xlabel("Packet sizes")
    plt.title("Normalized CPU usage per Mb/s")
    plt.grid(True)
    plt.show()
