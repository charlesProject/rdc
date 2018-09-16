"""
Tracker script for RDC
Implements the tracker control protocol
 - start rdc tracker
 - help nodes to establish links with each other

AnKun Zheng
"""

import sys
import os
import socket
import struct
import subprocess
import time
import logging
import random
import inspect
import threading
from threading import Thread
from threading import Lock
from enum import Enum

"""
Extension of socket to handle recv and send of special data
"""
class ExSocket:
    def __init__(self, sock):
        self.sock = sock
    def recvall(self, nbytes):
        res = []
        sock = self.sock
        nread = 0
        while nread < nbytes:
            chunk = self.sock.recv(min(nbytes - nread, 1024))
            nread += len(chunk)
            res.append(chunk)
        return b''.join(res)
    def recvint(self):
        return struct.unpack('@i', self.recvall(4))[0]
    def sendint(self, n):
        return self.sock.send(struct.pack('@i', n))
    def sendstr(self, s):
        size = 0
        size += self.sendint(len(s))
        size += self.sock.send(s.encode())
        return size
    def recvstr(self):
        slen = self.recvint()
        return self.recvall(slen).decode()

def log_args(level=logging.INFO):
    """Decorator to log arguments passed to func."""
    def inner_func(func):
        line_no = inspect.getsourcelines(func)[-1]

        @wraps(func)
        def return_func(*args, **kwargs):
            arg_list = list("{!r}".format(arg) for arg in args)
            arg_list.extend("{}={!r}".format(key, val)
                            for key, val in kwargs.iteritems())
            msg = arg_log_fmt.format(name=func.__name__,
                                     arg_str=", ".join(arg_list))
            logging.getLogger('').log(level, msg)
            return func(*args, **kwargs)
        return return_func

    return inner_func

class TopoHelper:
    def get_neighbor(self, rank, nworker):
        rank = rank + 1
        ret = []
        if rank > 1:
            ret.append(rank // 2 - 1)
        if rank * 2 - 1  < nworker:
            ret.append(rank * 2 - 1)
        if rank * 2 < nworker:
            ret.append(rank * 2)
        return ret

    def get_tree(self, nworker):
        tree_map = {}
        parent_map = {}
        for r in range(nworker):
            tree_map[r] = self.get_neighbor(r, nworker)
            parent_map[r] = (r + 1) // 2 - 1
        return tree_map, parent_map

    def find_share_ring(self, tree_map, parent_map, r):
        """
        get a ring structure that tends to share nodes with the tree
        return a list starting from r
        """
        nset = set(tree_map[r])
        cset = nset - set([parent_map[r]])
        if len(cset) == 0:
            return [r]
        rlst = [r]
        cnt = 0
        for v in cset:
            vlst = self.find_share_ring(tree_map, parent_map, v)
            cnt += 1
            if cnt == len(cset):
                vlst.reverse()
            rlst += vlst
        return rlst

    def get_ring(self, tree_map, parent_map):
        """
        get a ring connection used to recover local data
        """
        assert parent_map[0] == -1
        rlst = self.find_share_ring(tree_map, parent_map, 0)
        assert len(rlst) == len(tree_map)
        ring_map = {}
        nworker = len(tree_map)
        for r in range(nworker):
            rprev = (r + nworker - 1) % nworker
            rnext = (r + 1) % nworker
            ring_map[rlst[r]] = (rlst[rprev], rlst[rnext])
        return ring_map

    def get_link_map(self, nworker):
        """
        get the link map, this is a bit hacky, call for better algorithm
        to place similar nodes together
        """
        tree_map, parent_map = self.get_tree(nworker)
        ring_map = self.get_ring(tree_map, parent_map)
        rmap = {0 : 0}
        k = 0
        for i in range(nworker - 1):
            k = ring_map[k][1]
            rmap[k] = i + 1

        ring_map_ = {}
        tree_map_ = {}
        parent_map_ = {}
        for k, v in ring_map.items():
            ring_map_[rmap[k]] = (rmap[v[0]], rmap[v[1]])
        for k, v in tree_map.items():
            tree_map_[rmap[k]] = [rmap[x] for x in v]
        for k, v in parent_map.items():
            if k != 0:
                parent_map_[rmap[k]] = rmap[v]
            else:
                parent_map_[rmap[k]] = -1
        return tree_map_, parent_map_, ring_map_

class State(Enum):
    CMD = 1
    FIN = 2
    UNKNOWN = 3

class TrackerHandler:
    def __init__(self, sock, tracker):
        self.sock = sock
        self.tracker = tracker
        self.state = State.FIN
        self.cmd = None
    def handle(self):
        if self.state == State.FIN:
            self.cmd = self.recvstr()
            self.state = State.CMD
        elif self.state == State.CMD:
            if self.cmd == 'print':
                self.handle_print()
            elif self.cmd == 'start':
                self.handle_start()
            elif self.cmd == 'register':
                self.handle_register()
            elif self.cmd == 'barrier':
                self.handle_barrier()
            elif self.cmd == 'shutdown':
                return False
            self.state = State.FIN
            self.cmd = None
        return True
    def handle_start(self):
        with self.tracker.tracker_lock:
            # send world size
            self.sendint(self.tracker.nworker)
            # send rank
            self.rank = self.tracker.last_rank
            self.tracker.last_rank += 1
            self.sendint(self.rank)
            if self.tracker.last_rank == self.tracker.nworker:
                self.tracker.last_rank = 0

            self.addr = self.recvstr()

            # send the whole tree map
            tree_map = self.tracker.tree_map
            parent_map = self.tracker.parent_map
            ring_map = self.tracker.ring_map
            nnset = set(tree_map[self.rank])
            rprev, rnext = ring_map[self.rank]

            # send parent rank
            self.sendint(parent_map[self.rank])

            # send num neighbors
            self.sendint(len(nnset))
            for r in nnset:
                self.sendint(r)
            # send prev link$
            if rprev != -1 and rprev != self.rank:
                nnset.add(rprev)
                self.sendint(rprev)
            else:
                self.sendint(-1)
            # send next link
            if rnext != -1 and rnext != self.rank:
                nnset.add(rnext)
                self.sendint(rnext)
            else:
                self.sendint(-1)
            for rank, neighbors in self.tracker.tree_map.items():
                self.sendint(rank)
                self.sendint(len(neighbors))
                for i in range(len(neighbors)):
                    self.sendint(neighbors[i])
            # send num_conn and num_accept
            # connnect to node who has rank less than my rank
            self.sendint(self.rank)
            self.sendint(self.tracker.nworker - self.rank - 1)
            for rank, addr in self.tracker.addrs.items():
                self.sendstr(addr)
                self.sendint(rank)
            self.tracker.addrs[self.rank] = self.addr

    def handle_print(self):
        msg = self.recvstr()
        if self.rank != -1:
            msg = 'rank %d: %s ' % (self.rank, msg.strip())
        logging.info(msg)

    def handle_barrier(self):
        name = self.recvstr()
        self.tracker.name_to_barrier_counts += 1
        if self.tracker.name_to_barrier_counts == \
                len(self.self.tracker.name_to_ranks):
            self.tracker.name_to_barrier_conds[name].wait()
            self.sendstr("barrier_done")
        else:
            self.tracker.name_to_barrier_counts = 0
            self.trakcer.name_to_barrier_conds[name].notify()
            self.sendstr("barrier_done")
    def handle_register(self):
        name = self.recvstr()
        if name not in self.tracker.names:
            self.tracker.names.add(name)
            self.tracker.name_to_ranks[name] = set()
            self.tracker.name_to_barrier_counts[name] = 0
            self.tracker.name_to_barrier_conds[name] = \
                    threading.Condition()
        self.tracker.name_to_ranks[name].add(self.rank)

    def recvint(self):
        return self.sock.recvint()
    def recvstr(self):
        return self.sock.recvstr()
    def sendint(self, data):
        return self.sock.sendint(data)
    def sendstr(self, data):
        return self.sock.sendstr(data)

class Tracker:
    def __init__(self, hostIP, port, nworker):
        self.cur_rank = 0
        # trakcer addr
        self.hostIP = hostIP
        self.port = port
        self.nworker = nworker
        # create track sock then lisen
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.bind((hostIP, port))
        self.sock.listen(128)
        self.addrs = dict()

        # communicator name associated members
        self.names = set()
        self.name_to_ranks = dict()
        self.last_rank = 0

        # thread associated members
        self.tracker_lock = Lock()
        self.name_lock = Lock()

        # barrier related
        self.name_to_barrier_counts = dict()
        self.name_to_barrier_conds = dict()
        # construct initial tree map
        self.topohelper = TopoHelper()
        self.tree_map, self.parent_map, self.ring_map = \
            self.topohelper.get_link_map(nworker)

        def run():
            fd, s_addr = self.sock.accept()
            sock = ExSocket(fd)
            handler = TrackerHandler(sock, self)
            ret = True
            while ret:
                ret = handler.handle()

        logging.info('start listen on %s:%d' % (hostIP, self.port))
        self.threads = dict()
        for _ in range(nworker):
            thread = Thread(target = run, args = ())
            self.threads[self.cur_rank] = thread
            thread.setDaemon(True)
            thread.start()

    def worker_envs(self):
        """
        get enviroment variables for workers
        can be passed in as args or envs
        """
        return {'RDC_TRACKER_URI': self.hostIP,
                'RDC_TRACKER_PORT': self.port}

    def join(self):
        for thread in self.threads.values():
            while thread.isAlive():
                thread.join(100)


def submit(nworker, fun_submit, hostIP = 'auto', pscmd = None):
    """submit job

    Paramaters
    ----------
    nworker : int
        number of workers
        a parmaeter server job
    fun_sumbit : func
        the function to submit the jobs for servers and workers
    hostIP : str, optional
        the host ip of the root node
    pscmd :
    """
    # get the root node ip
    if hostIP == 'auto':
        hostIP = 'ip'
    if hostIP == 'dns':
        hostIP = socket.getfqdn()
    elif hostIP == 'ip':
        from socket import gaierror
        try:
            hostIP = socket.gethostbyname(socket.getfqdn())
        except gaierror:
            logging.warn('gethostbyname(socket.getfqdn()) failed...'\
                    ' trying on hostname()')
            hostIP = socket.gethostbyname(socket.gethostname())
        if hostIP.startswith("127."):
            s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            # doesn't have to be reachable
            s.connect(('10.255.255.255', 0))
            hostIP = s.getsockname()[0]
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.bind((hostIP, 0))
    port = s.getsockname()[1]
    envs = {'RDC_NUM_WORKERS' : nworker}

    # start the root
    tracker = Tracker(hostIP=hostIP, port=port, nworker=nworker)

    envs.update(tracker.worker_envs())

    # start the workers
    fun_submit(nworker, envs)

    # wait the root finished
    tracker.join()

def hash_combine(values=[]):
    ret = 0
    for value in values:
        ret ^= hash(value) + 0x9e3779b9 + (ret << 6) + (ret >>2 )
    return ret

def build_addr(backend, host, port):
    return "%s:%s:%d" % (backend, host, port)

def config_logger(args):
    FORMAT = '[%(asctime)s (%(name)s:%(lineno)s) %(levelname)s] %(message)s'
    #FORMAT = '%(asctime)s %(levelname)s %(message)s'
    level = args.log_level if 'log_level' in args else 'DEBUG'
    level = eval('logging.' + level)
    if 'log_file' not in args or args.log_file is None:
        logging.basicConfig(format=FORMAT, level = level)
    else:
        logging.basicConfig(format=FORMAT, level = level, filename = args.log_file)
        console = logging.StreamHandler()
        console.setFormatter(logging.Formatter(FORMAT))
        console.setLevel(level)
        logging.getLogger('').addHandler(console)
