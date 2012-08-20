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

parser.add_argument('--exptid',
                    dest="exptid",
                    help="Experiment ID",
                    default=None)

parser.add_argument('--rl',
                    dest="rl",
                    help="Which rate limiter to use",
                    choices=["htb", "hfsc", "newrl"],
                    default="")

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
                    default=["xlh7","xlh6"])

args = parser.parse_args()

def e(s):
    return "/tmp/%s/%s" % (args.exptid, s)

class Netperf(Expt):
    def start(self):
        # num servers, num clients
        ns = self.opts("ns")
        nc = self.opts("nrr")
        dir = self.opts("exptid")
        server = self.opts("hosts")[0]
        client = self.opts("hosts")[1]

        self.server = Host(server)
        self.client = Host(client)
        self.hlist = HostList()
        self.hlist.append(self.server)
        self.hlist.append(self.client)

        if self.opts("rl") == "htb":
            self.server.add_htb_qdisc("5Gbit")
        elif self.opts("rl") == 'newrl':
            self.server.insmod()
        else:
            self.server.rmmod()
            self.server.remove_qdiscs()

        self.server.start_netserver()
        self.hlist.rmrf(e(""))
        self.hlist.mkdir(e(""))
        sleep(1)
        # Start the connections
        if self.opts("nrr"):
            opts = "-v 2 -t TCP_RR -H %s -l %s -c -C" % (self.server.hostname(),
                                                         self.opts("t"))
            self.client.start_n_netperfs(self.opts("nrr"), opts, e(''), "rr")

        if self.opts("ns"):
            opts = "-t TCP_STREAM -H %s -l %s -c -C" % (self.server.hostname(),
                                                        self.opts("t"))
            self.client.start_n_netperfs(self.opts("ns"), opts, e(''), "stream")
        return

    def stop(self):
        self.hlist.killall("iperf netperf netserver")
        self.client.copy_local(e(''), self.opts("exptid"))
        return

Netperf(vars(args)).run()
