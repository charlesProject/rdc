#pragma once
#include "c_api_common.h"

/*! \brief Inhibit C++ name-mangling for Rdc functions. */
#ifdef __cplusplus
extern "C" {
#endif  // __cplusplus
#ifdef RDC_WITH_PYTHON
/*! \brief Get an int environment variable
 *  \param key key of this environment variable
 *  \param default_val default value
 *  \return value of this environment variable*/
RDC_DLL int RdcEnvGetEnv(PyObject* key, int default_val);

/*! \brief Get an universal environment variable
 *  \param key key of this environment variable
 *  \return value of this environment variable*/
RDC_DLL PyObject* RdcEnvFind(PyObject* key);

/*! \brief Get an int environment variable without default value
 *  \param key key of this environment variable
 *  \return value of this environment variable*/
RDC_DLL int RdcEnvGetIntEnv(PyObject* key);
#endif
#ifdef __cplusplus
}
#endif  // __cplusplus
