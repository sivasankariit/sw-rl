#!/usr/bin/python
import sys
import argparse
import multiprocessing
import termcolor as T
from expt import Expt
from time import sleep
from host import *

parser = argparse.ArgumentParser(description="Netperf Test for various rate limiters.")
parser.add_argument('--nstream',
                    dest="ns",
                    type=int,
                    help="Number of TCP_STREAM flows.",
                    default=4)

parser.add_argument('--nrr',
                    dest="nrr",
                    type=int,
                    help="Number of TCP_RR flows.",
                    default=512)

parser.add_argument('--pin',
                    dest="pin",
                    help="Pin netperf to CPUs in round robin fashion.",
                    action="store_true",
                    default=False)

parser.add_argument('--dir',
                    dest="dir",
                    help="Directory to store output.",
                    required=True)

parser.add_argument('--exptid',
                    dest="exptid",
                    help="Experiment ID",
                    default=None)

parser.add_argument('--rl',
                    dest="rl",
                    help="Which rate limiter to use",
                    choices=["htb", "hfsc", "newrl"],
                    default="htb")

parser.add_argument('--time', '-t',
                    dest="t",
                    type=int,
                    help="Time to run the experiment",
                    default=10)

parser.add_argument('--dryrun',
                    dest="dryrun",
                    help="Don't execute experiment commands.",
                    action="store_true",
                    default=False)

parser.add_argument('--hosts',
                    dest="hosts",
                    help="The two hosts (client/server) to run tests",
                    nargs="+",
                    default=["xlh5","xlh6"])

args = parser.parse_args()

def e(s):
    return "/tmp/%s/%s" % (args.exptid, s)

class Netperf(Expt):
    def initialise(self):
        self.hlist.rmmod()
        self.hlist.remove_qdiscs()
        if self.opts("rl") == 'newrl':
            self.hlist.insmod()

    def start(self):
        # num servers, num clients
        ns = self.opts("ns")
        nc = self.opts("nrr")
        dir = self.opts("dir")
        server = self.opts("hosts")[0]
        client = self.opts("hosts")[1]

        self.server = Host(server)
        self.client = Host(client)
        self.hlist = HostList()
        self.hlist.append(self.server)
        self.hlist.append(self.client)

        self.server.start_netserver()
        self.hlist.mkdir(e(""))
        sleep(1)
        # Start the connections
        for i in xrange(self.opts("nrr")):
            opts = "-v 2 -t TCP_RR -H %s -l %s -c -C" % (self.server.hostname(), self.opts("t"))
            self.client.start_netperf(opts, e('rr-%d.txt' % i))
        for i in xrange(self.opts("ns")):
            opts = "-t TCP_STREAM -H %s -l %s -c -C" % (self.server.hostname(), self.opts("t"))
            self.client.start_netperf(opts, e("stream-%d.txt" % i))
        return

    def stop(self):
        self.hlist.killall("iperf netperf netserver")
        self.client.copy_local(e(''), self.opts("exptid"))
        return

Netperf(vars(args)).run()
