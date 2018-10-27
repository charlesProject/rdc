"""
Buffer object in rdc
Author: Ankun Zheng
"""
import ctypes
from rdc import _LIB


class Buffer(object):
    """rdc buffer object"""
    __slots__ = ('handle')

    def __init__(self, **kwargs):
        self.handle = ctypes.c_void_p()
        if 'pinned' in kwargs:
            _LIB.RdcNewBuffer(
                ctypes.byref(self.handle), kwargs['addr'], kwargs['size'],
                kwargs['pinned'])
        else:
            _LIB.RdcNewBuffer(
                ctypes.byref(self.handle), kwargs['addr'], kwargs['size'],
                False)

    def __del__(self):
        _LIB.RdcDelBuffer(self.handle)
