#pragma once
#include "c_api_common.h"

/*! \brief Inhibit C++ name-mangling for Rdc functions. */
#ifdef __cplusplus
extern "C" {
#endif  // __cplusplus

/*! \brief Get an int environment variable
 *  \param key key of this environment variable
 *  \param default_val default value
 *  \return value of this environment variable*/
RDC_DLL int RdcEnvGetEnv(const char* key, int default_val);

/*! \brief Get an universal environment variable
 *  \param key key of this environment variable
 *  \return value of this environment variable*/
RDC_DLL void RdcEnvFind(const char* key, char** value);

/*! \brief Get an int environment variable without default value
 *  \param key key of this environment variable
 *  \return value of this environment variable*/
RDC_DLL int RdcEnvGetIntEnv(const char* key);

#ifdef __cplusplus
}
#endif  // __cplusplus
