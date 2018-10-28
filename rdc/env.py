"""
Core enviroment variable related utilities.

Author: Ankun Zheng
"""
import ctypes
from rdc import _LIB


def find(key):
    val_p = ctypes.c_char_p()
    key_p = ctypes.cast(key, ctypes.c_char_p)
    _LIB.RdcEnvFind(key_p, ctypes.byref(val_p))
    val = str(val_p)
    return val


def get_env(key, default_val):
    return _LIB.RdcEnvGetEnv(key, default_val)


def get_int_env(key):
    return _LIB.RdcEnvGetIntEnv(key)
