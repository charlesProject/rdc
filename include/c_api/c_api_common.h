#pragma once
#ifdef __cplusplus
#define RDC_EXTERN_C extern "C"
#include <cstdio>
#else
#define RDC_EXTERN_C
#include <stdio.h>
#endif

#if defined(_MSC_VER) || defined(_WIN32)
#define RDC_DLL RDC_EXTERN_C __declspec(dllexport)
#else
#define RDC_DLL RDC_EXTERN_C
#endif


/*! \brief rdc unsigned long type */
typedef unsigned long rdc_ulong;  // NOLINT(*)
