/*!
 *  Copyright (c) 2014 by Contributors
 * \file utils.h
 * \brief simple utils to support the code
 * \author Ankun Zheng
 */
#pragma once
#include <cstdio>
#include <string>
#include <cstdlib>
#include <vector>
#include <cstdarg>
#include <memory>
namespace rdc {
/*! \brief namespace for helper utils of the project */
namespace utils {

/*! \brief error message buffer length */
const int kPrintBuffer = 1 << 12;

/*!
 * \brief handling of Assert error, caused by inappropriate input
 * \param msg error message
 */
inline void HandleAssertError(const char *msg) {
  fprintf(stderr, "AssertError:%s\n", msg);
  exit(-1);
}
/*!
 * \brief handling of Check error, caused by inappropriate input
 * \param msg error message
 */
inline void HandleCheckError(const char *msg) {
    fprintf(stderr, "%s\n", msg);
    exit(-1);
}
inline void HandlePrint(const char *msg) {
  printf("%s", msg);
}
inline void HandleLogPrint(const char *msg) {
  fprintf(stderr, "%s", msg);
  fflush(stderr);
}
// these function pointers are to be assigned
inline void Printf(const char *fmt, ...) {
    std::string msg(kPrintBuffer, '\0');
    va_list args;
    va_start(args, fmt);
    vsnprintf(&msg[0], kPrintBuffer, fmt, args);
    va_end(args);
    HandlePrint(msg.c_str());
}
/*! \brief portable version of snprintf */
inline int SPrintf(char *buf, size_t size, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int ret = vsnprintf(buf, size, fmt, args);
    va_end(args);
    return ret;
}

/*! \brief assert a condition is true, use this to handle debug information */
inline void Assert(bool exp, const char *fmt, ...) {
    if (!exp) {
        std::string msg(kPrintBuffer, '\0');
        va_list args;
        va_start(args, fmt);
        vsnprintf(&msg[0], kPrintBuffer, fmt, args);
        va_end(args);
        HandleAssertError(msg.c_str());
    }
}

/*!\brief same as assert, but this is intended to be used as a message for users */
inline void Check(bool exp, const char *fmt, ...) {
    if (!exp) {
        std::string msg(kPrintBuffer, '\0');
        va_list args;
        va_start(args, fmt);
        vsnprintf(&msg[0], kPrintBuffer, fmt, args);
        va_end(args);
        HandleCheckError(msg.c_str());
    }
}

/*! \brief report error message, same as check */
inline void Error(const char *fmt, ...) {
    std::string msg(kPrintBuffer, '\0');
    va_list args;
    va_start(args, fmt);
    vsnprintf(&msg[0], kPrintBuffer, fmt, args);
    va_end(args);
    HandleCheckError(msg.c_str());
}

// easy utils that can be directly accessed in xgboost
/*! \brief get the beginning address of a vector */
template<typename T>
inline T *BeginPtr(std::vector<T> &vec) {  // NOLINT(*)
    if (vec.size() == 0) {
        return nullptr;
    } else {
        return &vec[0];
    }
}
/*! \brief get the beginning address of a vector */
template<typename T>
inline const T *BeginPtr(const std::vector<T> &vec) {  // NOLINT(*)
  if (vec.size() == 0) {
      return nullptr;
  } else {
      return &vec[0];
  }
}
inline char* BeginPtr(std::string &str) {  // NOLINT(*)
    if (str.length() == 0) return nullptr;
    return &str[0];
}
inline const char* BeginPtr(const std::string &str) {
    if (str.length() == 0) return nullptr;
    return &str[0];
}
inline void* IncrVoidPtr(void* ptr, size_t step) {
    return reinterpret_cast<void*>(
            reinterpret_cast<int8_t*>(ptr) + step);
}
inline const void* IncrConstVoidPtr(const void* ptr, size_t step) {
    return reinterpret_cast<const void*>(
            reinterpret_cast<int8_t*>(const_cast<void*>(ptr)) + step);
}
inline std::vector<std::pair<int, int>> Split(
        int begin, int end, int nparts) {
    std::vector<std::pair<int, int>> ranges(nparts);
    int len = end - begin;
    int k = len / nparts;
    int m = len % nparts;
    for (int i = 0; i < nparts; i++) {
        int rbegin = begin + i * k + std::min(i, m);
        int rend = begin + (i + 1) * k + std::min(i + 1, m);
        ranges[i] = std::make_pair(rbegin, rend);
    }
    return ranges;
}
template <typename Container>
std::string ConcatList(const Container& container) {
    std::string str = "";
    for (const auto& item : container) {
        str += std::to_string(item);
        str += '\t';
    }
    return str;
}
inline void* AllocTemp(const size_t& nbytes) {
    return malloc(nbytes);
}

inline void Free(void* ptr) {
    return free(ptr);
}

inline void ZeroBuf(void* buf, size_t nbytes) {
//    memset(buf, 0, nbytes);
    return;
}
template<typename T, typename... Args>
inline std::unique_ptr<T> make_unique(Args&&... args) {
    return std::unique_ptr<T>(new T(std::forward<Args>(args)...));
}
}
}  // namespace rdc

