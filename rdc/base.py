import ctypes
import os
import numpy as np
_LIB = None
def _find_lib_path(dll_name):
    """Find the rdc dynamic library files.

    Returns
    -------
    lib_path: list(string)
       List of all found library path to rdc
    """
    curr_path = os.path.dirname(os.path.abspath(os.path.expanduser(__file__)))
    # make pythonpack hack: copy this directory one level upper for setup.py
    dll_path = [
        curr_path,
        os.path.join(curr_path, '../lib/'),
        os.path.join(curr_path, './lib/')
    ]
    if os.name == 'nt':
        dll_path = [os.path.join(p, dll_name) for p in dll_path]
    else:
        dll_path.append('/usr/local/lib')
        dll_path = [os.path.join(p, dll_name) for p in dll_path]
    lib_path = [p for p in dll_path if os.path.exists(p) and os.path.isfile(p)]
    if len(lib_path) == 0:
        raise RuntimeError(
            'Cannot find Rdc Libarary in the candicate path, ' +
            'did you install compilers and run build.sh in root path?\n'
            'List of candidates:\n' + ('\n'.join(dll_path)))
    return lib_path


def _load_lib(lib='standard', lib_dll=None):
    """Load rdc library."""
    global _LIB
    if _LIB is not None:
        warnings.warn('rdc.int call was ignored because it has'\
                          ' already been initialized', level=2)
        return

    if lib_dll is not None:
        _LIB = lib_dll
        return

    if lib == 'standard':
        dll_name = 'librdc'
    else:
        dll_name = 'librdc_' + lib

    if os.name == 'nt':
        dll_name += '.dll'
    else:
        dll_name += '.so'

    _LIB = ctypes.cdll.LoadLibrary(_find_lib_path(dll_name)[0])
    _LIB.RdcGetRank.restype = ctypes.c_int
    _LIB.RdcGetWorldSize.restype = ctypes.c_int
    _LIB.RdcVersionNumber.restype = ctypes.c_int


def _unload_lib():
    """Unload rdc library."""
    global _LIB
    del _LIB
    _LIB = None


#library instance
_load_lib()
import atexit
atexit.register(_unload_lib)
# type definitions
rdc_uint = ctypes.c_uint
rdc_float = ctypes.c_float
rdc_float_p = ctypes.POINTER(rdc_float)
rdc_real_t = np.float32
