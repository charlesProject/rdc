"""
Core communicator utilities.

Author: Ankun Zheng
"""
import ctypes
from rdc import _LIB


class WorkComp(object):
    __slots__ = ('handle')

    def __init__(self, **kwargs):
        self.handle = ctypes.c_void_p()
        if 'own_handle' in self.own_handle:
            self.own_handle = kwargs['own_handle']
        else:
            self.own_handle = False

    def __del__(self):
        if self.own_handle:
            _LIB.RdcDelWorkCompletion(self.handle)

    def wait(self):
        return _LIB.RdcWorkCompletionWait(self.handle)

    def status(self):
        return _LIB.RdcWorkCompletionStatus(self.handle)


class Comm(object):
    __slots__ = ('handle')

    def __init__(self, **kwargs):
        self.handle = ctypes.c_void_p()
        if 'own_handle' in self.own_handle:
            self.own_handle = kwargs['own_handle']
        else:
            self.own_handle = False

    def isend(self, buf, dest_rank):
        wc = WorkComp(own_handle=True)
        wc_handle = _LIB.RdcISend(self.handle, buf.handle, dest_rank)
        wc.handle = wc_handle
        return wc

    def irecv(self, buf, src_rank):
        wc = WorkComp(own_handle=True)
        wc_handle = _LIB.RdcIRecv(self.handle, buf.handle, src_rank)
        wc.handle = wc_handle
        return wc


def new_comm(name):
    comm = Comm()
    if isinstance(name, str):
        _LIB.RdcNewCommunicator(ctypes.byref(comm.handle), name.decode('utf-8'))
    elif isinstance(name, bytes):
        _LIB.RdcNewCommunicator(ctypes.byref(comm.handle), name)
    else:
        raise TypeError('name must be a string or bytearray')
    return comm


def get_comm(name):
    comm = Comm()
    if isinstance(name, str):
        _LIB.RdcGetCommunicator(ctypes.byref(comm.handle), name.decode('utf-8'))
    elif isinstance(name, bytes):
        _LIB.RdcGetCommunicator(ctypes.byref(comm.handle), name)
    else:
        raise TypeError('name must be a string or bytearray')
    return comm
