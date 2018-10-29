#include "c_api/python.h"
#include "common/env.h"
#include "core/logging.h"
#ifdef RDC_WITH_PYTHON
#include "c_api/c_api_env.h"
int RdcEnvGetEnv(PyObject* key, int default_val) {
    assert(PyStr_Check(key));
    return Env::Get()->GetEnv(PyStr_AsString(key), default_val);
}
PyObject* RdcEnvFind(PyObject* key) {
    assert(PyStr_Check(key));
    const char* val = Env::Get()->Find(PyStr_AsString(key));
    return PyStr_FromString(val);
}
int RdcEnvGetIntEnv(PyObject* key) {
    assert(PyStr_Check(key));
    return Env::Get()->GetIntEnv(PyStr_AsString(key));
}
#endif
