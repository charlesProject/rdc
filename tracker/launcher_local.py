#!/usr/bin/env python
"""
submission script, local machine version
"""

import argparse
import sys
import os
import signal
import subprocess
from threading import Thread
import tracker
import signal
import logging

from tracker import utils
from tracker.args import parse_args
keepalive = """
nrep=0
rc=254
while [ $rc -eq 254 ];
do
    export DMLC_NUM_ATTEMPT=$nrep
    %s
    rc=$?;
    nrep=$((nrep+1));
done
"""


class LocalLauncher(object):

    def __init__(self, args, unknown):
        self.args = args
        self.cmd = ' '.join(args.command) + ' ' + ' '.join(unknown)

    def exec_cmd(self, cmd, pass_env):
        env = os.environ.copy()
        for k, v in pass_env.items():
            env[k] = str(v)

        ntrial = 0
        while True:
            if os.name == 'nt':
                env['DMLC_NUM_ATTEMPT'] = str(ntrial)
                ret = subprocess.call(cmd, shell=True, env=env)
                if ret == 254:
                    ntrial += 1
                    continue
            else:
                bash = keepalive % (cmd)
                ret = subprocess.call(
                    bash, shell=True, executable='bash', env=env)
            if ret == 0:
                logging.debug('Thread %d exit with 0')
                return
            else:
                if os.name == 'nt':
                    os.exit(-1)
                else:
                    raise Exception('Get nonzero return code=%d' % ret)

    def submit(self):

        def mthread_submit(nworker, envs):
            """
            customized submit script
            """
            procs = {}
            for i in range(nworker):
                procs[i] = Thread(target=self.exec_cmd, args=(self.cmd, envs))
                procs[i].setDaemon(True)
                procs[i].start()

        return mthread_submit

    def run(self):
        utils.config_logger(self.args)
        tracker.submit(
            self.args.num_workers, fun_submit=self.submit(), pscmd=self.cmd)


def signal_handler(sig, frame):
    sys.exit(0)


def main():
    args, unknown = parse_args()

    launcher = LocalLauncher(args, unknown)
    signal.signal(signal.SIGINT, signal_handler)
    launcher.run()


if __name__ == '__main__':
    main()
