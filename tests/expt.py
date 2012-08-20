
import termcolor as T
import sys
import os
from time import sleep

def progress(t):
    while t > 0:
        print T.colored('  %3d seconds left  \r' % (t), 'cyan'),
        t -= 1
        sys.stdout.flush()
        sleep(1)
    print '\r\n'

class Expt(object):
    def __init__(self, opts):
        # Some default opts
        self._opts = {
            't': 30,
            'dir': opts.get('dir', '/tmp')
            }
        self._opts.update(opts)
        self._monitors = []
        self.procs = []

    def start(self):
        pass

    def stop(self):
        pass

    def opts(self, name):
        return self._opts.get(name, None)

    def run(self):
        try:
            dir = self.opts('dir')
            if not os.path.exists(dir):
                os.makedirs(dir)
            self.start()
            t = self.opts('t')
            progress(t)
        except KeyboardInterrupt:
            self.log("Stopping tests...")
        self.stop_monitors()
        self.stop()

    def start_monitor(self, m):
        self._monitors.append(m)
        m.start()

    def stop_monitors(self):
        for m in self._monitors:
            m.terminate()

    def log(self, s):
        print s
