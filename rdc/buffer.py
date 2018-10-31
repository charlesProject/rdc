"""
Buffer object in rdc
Author: Ankun Zheng
"""
import ctypes
import numpy as np

from rdc import _LIB


class Buffer(object):
    """rdc buffer object"""
    __slots__ = ('handle', 'pinned', 'addr', 'size')

    def __init__(self, **kwargs):
        self.handle = ctypes.c_void_p()
        if 'pinned' in kwargs:
            self.pinned = True
        else:
            self.pinned = False
        if 'buf' in kwargs:
            buf = kwargs['buf']
            if isinstance(buf, np.ndarray):
                self.addr = buf.ctypes.data_as(ctypes.c_void_p)
                self.size = buf.size * buf.itemsize
            else:
                raise TypeError('unsupport type for buffer')
        elif 'addr' in kwargs:
            assert 'size' in kwargs, 'size must accompany with addr'
            self.addr = kwargs['addr']
            self.size = kwargs['size']
        '''call c api to create a buffer'''
        _LIB.RdcNewBuffer(
            ctypes.byref(self.handle), self.addr, self.size, self.pinned)

    def __del__(self):
        _LIB.RdcDelBuffer(self.handle)
