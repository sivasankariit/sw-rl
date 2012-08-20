
import paramiko
from subprocess import Popen
import termcolor as T
import os
import socket
from time import sleep
import pexpect

RL_MODULE = '/root/vimal/newrl.ko'
DEFAULT_DEV = 'eth0'
NETPERF_DIR = '/root/vimal'
# NETPERF_DIR = '/usr/bin'
SHELL_PROMPT = '#'

class HostList(object):
    def __init__(self, *lst):
        self.lst = list(lst)

    def append(self, host):
        self.lst.append(host)

    def __getattribute__(self, name, *args):
        try:
            return object.__getattribute__(self, name)
        except AttributeError:
            ret = lambda *args: map(lambda h: h.__getattribute__(name)(*args), self.lst)
            return ret

    def __iter__(self):
        return self.lst

def local_cmd(c):
    print T.colored(c, "green")
    p = Popen(c, shell=True)
    p.wait()


class ShellWrapper:
    def __init__(self, chan):
        self.chan = chan

    def cmd_async(self, cmd):
        self.chan.send("(%s;) &\n" % cmd)

    def cmd(self, c):
        self.chan.send(c)
        self.chan.recv_ready()
        return self.chan.recv(10**6)

class SSHWrapper:
    def __init__(self, ssh):
        self.ssh = ssh

    def cmd_async(self, cmd):
        cmd = "(%s;) &" % cmd
        self.ssh.sendline(cmd)

    def cmd(self, c):
        self.ssh.sendline(c)
        self.ssh.expect(SHELL_PROMPT)

class Host(object):
    _ssh_cache = {}
    _shell_cache = {}
    def __init__(self, addr):
        self.addr = addr
        # List of processes spawned async on this host
        self.procs = []
        self.delay = False
        self.delayed_cmds = []
        self.dryrun = False

    def set_dryrun(self, state=True):
        self.dryrun = state

    def get(self):
        ssh = Host._ssh_cache.get(self.addr, None)
        if ssh is None:
            ssh = pexpect.spawn("ssh %s" % self.addr)
            ssh.expect(SHELL_PROMPT)
            Host._ssh_cache[self.addr] = ssh
        return ssh

    def get_shell(self):
        shell = Host._shell_cache.get(self.addr, None)
        if shell is None:
            client = self.get()
            shell = SSHWrapper(client)
            Host._shell_cache[self.addr] = shell
        return shell

    def cmd(self, c, dryrun=False):
        self.log(c)
        if not self.delay:
            if dryrun or self.dryrun:
                return (self.addr, c)
            ssh = self.get()
            self.get_shell().cmd(c)
            return (self.addr, c)
        else:
            self.delayed_cmds.append(c)
        return (self.addr, c)

    def delayed_cmds_execute(self):
        if len(self.delayed_cmds) == 0:
            return None
        self.delay = False
        ssh = self.get()
        cmds = ';'.join(self.delayed_cmds)
        out = ssh.exec_command(cmds)[1].read()
        self.delayed_cmds = []
        return out

    def cmd_async(self, c, dryrun=False):
        self.log(c)
        if not self.delay:
            if dryrun or self.dryrun:
                return (self.addr, c)
            #ssh = self.get()
            #out = ssh.exec_command(c)
            sh = self.get_shell()
            sh.cmd_async(c)
        else:
            self.delayed_cmds.append(c)
        return (self.addr, c)

    def delayed_async_cmds_execute(self):
        if len(self.delayed_cmds) == 0:
            return None
        self.delay = False
        ssh = self.get()
        cmds = ';'.join(self.delayed_cmds)
        out = ssh.exec_command(cmds)[1]
        self.delayed_cmds = []
        return out

    def log(self, c):
        addr = T.colored(self.addr, "magenta")
        c = T.colored(c, "grey", attrs=["bold"])
        print "%s: %s" % (addr, c)

    def get_10g_dev(self):
        return DEFAULT_DEV

    def mkdir(self, dir):
        self.cmd("mkdir -p %s" % dir)

    def rmrf(self, dir):
        print T.colored("removing %s" % dir, "red", attrs=["bold"])
        if dir == "/tmp" or dir == "~" or dir == "/":
            # useless
            return
        self.cmd("rm -rf %s" % dir)

    def rmmod(self, mod=RL_MODULE):
        self.cmd("rmmod %s" % mod)

    def insmod(self, mod=RL_MODULE, params="rate=5000 dev=%s" % DEFAULT_DEV, rmmod=True):
        cmd = "insmod %s %s" % (mod, params)
        if rmmod:
            cmd = "rmmod %s; " % mod + cmd
        self.cmd(cmd)

    def remove_qdiscs(self):
        iface = self.get_10g_dev()
        self.cmd("tc qdisc del dev %s root" % iface)

    def add_htb_qdisc(self, rate='5Gbit'):
        iface = self.get_10g_dev()
        self.remove_qdiscs()
        self.rmmod()
        c  = "tc qdisc add dev %s root handle 1: htb default 1;" % iface
        c += "tc class add dev %s classid 1:1 parent 1: " % iface
        c += "htb rate %s mtu 65000 burst 15k;" % rate
        self.cmd(c)

    def killall(self, extra=""):
        for p in self.procs:
            try:
                p.kill()
            except:
                pass
        self.cmd("killall -9 ssh iperf top bwm-ng %s" % extra)

    def configure_tx_interrupt_affinity(self):
        dev = self.get_10g_dev()
        c = "n=`grep '%s-tx' /proc/interrupts | awk -F ':' '{print $1}' | tr -d '\\n '`; " % dev
        c += " echo 0 > /proc/irq/$n/smp_affinity; "
        self.cmd(c)

    # starting common apps
    def start_netserver(self):
        self.cmd_async("%s/netserver" % NETPERF_DIR)

    def start_netperf(self, args, outfile):
        self.cmd_async("%s/netperf %s 2>&1 > %s" % (NETPERF_DIR, args, outfile))

    def start_n_netperfs(self, n, args, dir, outfile_prefix):
        cmd = "for i in `seq 1 %s`; " % n
        cmd += "  do (%s/netperf %s 2>&1 > %s/%s-$i.txt &);" % (NETPERF_DIR,
                                                                args,
                                                                dir,
                                                                outfile_prefix)
        cmd += " done;"
        self.cmd(cmd)
        return

    # Monitoring scripts
    def start_cpu_monitor(self, dir="/tmp"):
        dir = os.path.abspath(dir)
        path = os.path.join(dir, "cpu.txt")
        self.cmd("mkdir -p %s" % dir)
        cmd = "(top -b -p1 -d1 | grep --line-buffered \"^Cpu\") > %s" % path
        return self.cmd_async(cmd)

    def start_bw_monitor(self, dir="/tmp", interval_sec=2):
        dir = os.path.abspath(dir)
        path = os.path.join(dir, "net.txt")
        self.cmd("mkdir -p %s" % dir)
        cmd = "bwm-ng -t %s -o csv -u bits -T rate -C ',' > %s" % (interval_sec * 1000, path)
        return self.cmd_async(cmd)

    def start_perf_monitor(self, dir="/tmp", time=30):
        dir = os.path.abspath(dir)
        path = os.path.join(dir, "perf.txt")
        events = [
            "instructions",
            "cache-misses",
            "branch-instructions",
            "branch-misses",
            "L1-dcache-loads",
            "L1-dcache-load-misses",
            "L1-dcache-stores",
            "L1-dcache-store-misses",
            "L1-dcache-prefetches",
            "L1-dcache-prefetch-misses",
            "L1-icache-loads",
            "L1-icache-load-misses",
            ]
        # This command will use debug counters, so you can't run it when
        # running oprofile
        events = ','.join(events)
        cmd = "(perf stat -e %s -a sleep %d) > %s 2>&1" % (events, time, path)
        return self.cmd_async(cmd)

    def start_monitors(self, dir='/tmp', interval=1e8):
        return [self.start_cpu_monitor(dir),
                self.start_bw_monitor(dir)]

    def copy_local(self, src_dir="/tmp", exptid=None):
        """Copy remote experiment output to a local directory for analysis"""
        if src_dir == "/tmp":
            return
        if exptid is None:
            print "Please supply experiment id"
            return

        # First compress output
        self.cmd("tar czf /tmp/%s.tar.gz %s --xform='s|tmp/||'" % (exptid, src_dir))
        opts = "-o StrictHostKeyChecking=no"
        c = "scp %s -r %s:/tmp/%s.tar.gz ." % (opts, self.hostname(), exptid)
        print "Copying experiment output"
        local_cmd(c)

    def hostname(self):
        return socket.gethostbyaddr(self.addr)[0]
