#!/usr/bin/python
import sys
import argparse
import termcolor as T
import re
from collections import defaultdict
import matplotlib as mp
import matplotlib.pyplot as plt

parser = argparse.ArgumentParser(description="Plot netperf experiment outputs.")
parser.add_argument('--rr',
                    nargs="+",
                    help="rr files to parse")

parser.add_argument('--out', '-o',
                    help="save plot to file")

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
    plt.xscale("log")
    #plt.show()

class RRParser:
    def __init__(self, filename):
        self.filename = filename
        self.lines = open(filename).readlines()
        try:
            self.parse()
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

def plot():
    hist = defaultdict(int)
    total_tps = 0
    def combine(hnew):
        for val,num in hnew:
            hist[val] += num
        return
    for f in args.rr:
        r = RRParser(f)
        c = cdf(r.histogram)
        combine(r.histogram)
        plot_cdf(c[0], c[1], alpha=0.1)
        total_tps += r.tps
    agg_cdf = cdf(sorted(list(hist.iteritems())))
    plot_cdf(agg_cdf[0], agg_cdf[1], lw=2, color='blue')
    plt.xlim((0, 1e4))
    plt.yticks( map(lambda y: y/10.0, range(0, 11)) )
    plt.grid(True)
    plt.xlabel("usec")
    plt.ylabel("fraction")
    plt.title("Total tps: %.3f" % total_tps)
    if args.out is None:
        plt.show()
    else:
        print 'saved to %s' % args.out
        plt.savefig(args.out)

plot()
