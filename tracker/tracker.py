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
from threading import Condition
from enum import Enum
from loguru import logger
import traceback
import configparser
import utils
from topo import TopoHelper
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



class State(Enum):
    CMD = 1
    FIN = 2
    UNKNOWN = 3


class TrackerHandler:

    def __init__(self, sock, tracker, worker_id):
        self.sock = sock
        self.tracker = tracker
        self.worker_id = worker_id
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
            elif self.cmd == 'exclude':
                self.handle_exclude()
            elif self.cmd == 'unexclude':
                self.handle_unexclude()
            elif self.cmd == 'heartbeat':
                self.handle_heartbeat()
            elif self.cmd == 'shutdown':
                return False
            self.state = State.FIN
            self.cmd = None
        return True

    def handle_start(self):
        rank = self.recvint()
        self.tracker.tracker_lock.acquire()
        self.tracker.worker_id_to_ranks[self.worker_id] = rank
        self.addr = self.recvstr()
        self.tracker.tracker_lock.release()
        self.tracker.rank_cond.acquire()
        self.tracker.rank_counter += 1
        if self.tracker.rank_counter != self.tracker.nworker:
            self.tracker.rank_cond.wait()
        else:
            self.tracker.rank_counter = 0
            self.tracker.realloc_ranks()
            self.tracker.rank_cond.notify_all()
        self.tracker.rank_cond.release()

        self.rank = self.tracker.worker_id_to_ranks[self.worker_id]
        self.tracker.rank_cond.acquire()
        self.tracker.addrs[self.rank] = self.addr
        if len(self.tracker.addrs) != self.tracker.nworker:
            self.tracker.rank_cond.wait()
        else:
            self.tracker.rank_cond.notify_all()
        self.tracker.rank_cond.release()

        # send world size
        self.sendint(self.tracker.nworker)
        # send rank
        self.tracker.tracker_lock.acquire()
        self.sendint(self.rank)
        num_conn = 0
        num_accept = 0
        for rank, addr in self.tracker.addrs.items():
            if rank < self.rank:
                num_conn += 1
            elif rank > self.rank:
                num_accept += 1
        self.sendint(num_conn)
        self.sendint(num_accept)
        for rank, addr in self.tracker.addrs.items():
            if rank < self.rank:
                self.sendstr(addr)
                self.sendint(rank)
        self.tracker.tracker_lock.release()

    def handle_print(self):
        msg = self.recvstr()
        if self.rank != -1:
            msg = 'rank %d: %s ' % (self.rank, msg.strip())
        logger.info(msg)

    '''A distributed lock impletentation, only communicator or group
    with same name can continue, otherwise will be blocked
    '''

    def handle_exclude(self):
        comm = self.recvstr()
        self.tracker.comm_lock.acquire()
        if self.tracker.last_comm != comm:
            if self.tracker.last_comm == None:
                self.tracker.last_comm = comm
            else:
                if not self.tracker.comm_added[comm]:
                    self.tracker.pending_comms.add(comm)
                    self.tracker.comm_added[comm] = True
            self.sendstr('exclude_undone')
            self.tracker.comm_lock.release()
        else:
            self.sendstr('exclude_done')
            self.tracker.comm_lock.release()

    def handle_unexclude(self):
        comm = self.recvstr()
        self.tracker.comm_cond.acquire()
        self.tracker.lock_counter += 1
        if self.tracker.lock_counter != self.tracker.nworker:
            self.tracker.comm_cond.wait()
        else:
            self.tracker.lock_counter = 0
            self.tracker.comm_lock.acquire()
            if len(self.tracker.pending_comms):
                self.tracker.last_comm = self.tracker.pending_comms.pop()
            else:
                self.tracker.last_comm = None
            self.tracker.comm_lock.release()
            self.tracker.comm_cond.notify_all()
        self.tracker.comm_cond.release()
        self.sendstr('unexclude_done')

    def handle_barrier(self):
        name = self.recvstr()
        self.tracker.name_to_barrier_conds[name].acquire()
        self.tracker.name_to_barrier_counter[name] += 1
        if self.tracker.name_to_barrier_counter[name] != \
                self.tracker.nworker:
            self.tracker.name_to_barrier_conds[name].wait()
        else:
            self.tracker.name_to_barrier_counter[name] = 0
            self.tracker.name_to_barrier_conds[name].notify_all()
        self.tracker.name_to_barrier_conds[name].release()
        self.sendstr("barrier_done")

    def handle_register(self):
        name = self.recvstr()
        self.tracker.register_lock.acquire()
        if name not in self.tracker.names:
            self.tracker.names.add(name)
            self.tracker.name_to_ranks[name] = set()
            self.tracker.name_to_barrier_counter[name] = 0
            self.tracker.name_to_barrier_conds[name] = Condition()
            self.tracker.name_to_barrier_locks[name] = Lock()
            self.tracker.comm_added[name] = False

        self.tracker.name_to_ranks[name].add(self.rank)
        self.tracker.register_lock.release()

    '''keep heartbeat'''

    def handle_heartbeat(self):
        self.tracker.last_heartbeat_timepoint[self.worker_id] = time.time()
        self.sendstr('heartbeat_done')

    def recvint(self):
        return self.sock.recvint()

    def recvstr(self):
        return self.sock.recvstr()

    def sendint(self, data):
        return self.sock.sendint(data)

    def sendstr(self, data):
        return self.sock.sendstr(data)


class Tracker:

    def __init__(self, host_ip, port, nworker):
        self.cur_rank = 0
        # trakcer addr
        self.host_ip = host_ip
        self.port = port
        self.nworker = nworker
        # create track sock then lisen
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.bind((host_ip, port))
        self.sock.listen(128)
        self.addrs = dict()

        # communicator name associated members
        # register related
        self.names = set()
        self.name_to_ranks = dict()
        self.register_lock = Lock()
        self.last_rank = 0

        self.worker_id_to_ranks = dict()
        self.rank_cond = threading.Condition()
        self.rank_counter = 0
        # thread associated members
        self.tracker_lock = Lock()
        self.name_lock = Lock()

        # barrier related
        self.name_to_barrier_counter = dict()
        self.name_to_barrier_conds = dict()
        self.name_to_barrier_locks = dict()
        # exclude related
        self.last_comm = None
        self.pending_comms = set()
        self.comm_added = dict()
        self.comm_lock = Lock()

        # heartbeat related
        self.last_heartbeat_timepoint = dict()
        self.lock_counter = 0
        self.comm_cond = Condition()
        # construct initial tree map
        self.topohelper = TopoHelper()
        self.tree_map, self.parent_map, self.ring_map = \
            self.topohelper.get_link_map(nworker)

        def run(worker_id):
            fd, s_addr = self.sock.accept()
            sock = ExSocket(fd)
            handler = TrackerHandler(sock, self, worker_id)
            ret = True
            while ret:
                ret = handler.handle()

        logger.info('start listen on %s:%d' % (host_ip, self.port))
        self.threads = dict()
        for worker_id in range(nworker):
            thread = Thread(target=run, args=(worker_id,))
            self.threads[self.cur_rank] = thread
            thread.setDaemon(True)
            thread.start()

    def realloc_ranks(self):
        existing_ranks = set()
        for worker_id, rank in self.worker_id_to_ranks.items():
            if rank != -1:
                existing_ranks.add(rank)
        last_rank = 0
        for worker_id, rank in self.worker_id_to_ranks.items():
            if rank != -1:
                continue
            else:
                while last_rank in existing_ranks:
                    last_rank += 1
                self.worker_id_to_ranks[worker_id] = last_rank
                last_rank += 1

    def worker_envs(self):
        """
        get enviroment variables for workers
        can be passed in as args or envs
        """
        common_envs = {
            'RDC_TRACKER_URI': self.host_ip,
            'RDC_TRACKER_PORT': self.port,
            'RDC_HEARTBEAT_INTERVAL': 5000,
        }
        return common_envs

    def join(self):
        for thread in self.threads.values():
            while thread.isAlive():
                thread.join(100)


def submit(nworker, fun_submit, host_ip='auto', pscmd=None):
    """submit job

    Paramaters
    ----------
    nworker : int
        number of workers
    fun_sumbit : func
        the function to submit the jobs for servers and workers
    host_ip : str, optional
        the host ip of the root node
    pscmd :
    """
    host_ip, port, envs = utils.basic_tracker_config(host_ip)
    # start the root
    tracker = Tracker(host_ip=host_ip, port=port, nworker=nworker)

    envs.update(tracker.worker_envs())

    # start the workers
    fun_submit(nworker, envs)

    # wait the root finished
    tracker.join()
