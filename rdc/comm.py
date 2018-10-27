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

    def __del__(self):
        _LIB.RdcDelWorkCompletion(self.handle)


class Comm(object):
    __slots__ = ('handle')

    def __init__(self, **kwargs):
        self.handle = ctypes.c_void_p()
        if len(kwargs) == 0:
            pass

    def isend(self, buf, dest_rank):
        _LIB.RdcISend(self.handle, buf.handle, dest_rank)

    def irecv(self, buf, src_rank):
        _LIB.RdcIRecv(self.handle, buf.handle, src_rank)
