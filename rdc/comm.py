import ctypes


class WorkCompletion(object):
    __slots__ = ('handle')

    def __init__(self, **kwargs):
        self.handle = ctypes.c_void_p


class Buffer(object):
    __slots__ = ('handle')

    def __init__(self, **kwargs):
        self.handle = ctypes.c_void_p
        if 'pinned' not in kwargs:
            _LIB.RdcNewBuffer(
                ctypes.byref(self.handle), kwargs['addrs'], kwargs['size'],
                kwargs['pinned'])
        else:
            _LIB.RdcNewBuffer(
                ctypes.byref(self.handle), kwargs['addrs'], kwargs['size'],
                False)

    def __del__():
        _LIB.RdcDelBuffer(self.handle)


class Comm(object):
    __slots__ = ('handle')

    def __init__(self, **kwargs):
        self.handle = ctypes.c_void_p
        if len(kwargs) == 0:
            pass

    def isend(self, buf, dest_rank):
        _LIB.RdcISend(self.handle, buf.handle, dest_rank)

    def irecv(self, buf, src_rank):
        _LIB.RdcIRecv(self.handle, buf.handle, src_rank)
