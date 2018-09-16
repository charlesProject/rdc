/**
 *  Copyright (c) 2018 by Contributors
 * @file   string_utils.h
 * @brief  string utilities
 */
#pragma once

#include <cstdlib>
#include <cstring>
#include <cstdarg>
#include <string>
#include <algorithm>

namespace rdc {
namespace str_utils {
/*! \brief message buffer length */
const int kPrintBuffer = 1 << 12;

/*! \brief trim string from leftmost */
inline void Ltrim(std::string &s) {
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](int ch) {
        return !std::isspace(ch);
    }));
}

/*! \brief trim string from rightmost */
inline void Rtrim(std::string &s) {
    s.erase(std::find_if(s.rbegin(), s.rend(), [](int ch) {
        return !std::isspace(ch);
    }).base(), s.end());
}

/*! \brief trim string from both ends */
inline void Trim(std::string &s) {
    Ltrim(s);
    Rtrim(s);
}

/*! \brief c++11 version of string format */
inline std::string SPrintf(const char *fmt, ...) {
    std::string msg(kPrintBuffer, '\0');
    va_list args;
    va_start(args, fmt);
    vsnprintf(&msg[0], kPrintBuffer, fmt, args);
    va_end(args);
    Trim(msg);
    return msg;
}

template <typename Container>
std::string ConcatToString(const Container& container) {
    std::string str = "";
    for (const auto& item : container) {
        str += std::to_string(item);
        str += '\t';
    }
    return str;
}

inline bool StartsWith(const std::string& str, const std::string& prefix) {
    if (str.size() < prefix.size()) {
        return false;
    }
    auto len = prefix.size();
    for (auto i = 0U; i < len; i++) {
        if (str[i] != prefix[i]) {
            return false;
        }
    }
    return true;
}

inline bool EndsWith(const std::string& str, const std::string& suffix) {
    if (str.size() < suffix.size()) {
        return false;
    }
    auto str_len = str.size();
    auto suf_len = suffix.size();
    for (auto i = 0U; i < suf_len; i++) {
        if (str[str_len - i - 1] != suffix[suf_len - i - 1]) {
            return false;
        }
    }
    return true;
}

inline std::vector<std::string> Split(const std::string& str,
        const char& sep = ' ') {
    std::string::size_type begin = 0;
    std::vector<std::string> result;

    while ((begin = str.find_first_not_of(sep, begin)) != std::string::npos) {
        auto end = str.find_first_of(sep, begin);
        result.push_back(str.substr(begin, end - begin));
        begin = end;
    }
    return result;
}
} // namespace str_utils
} // namesapce rdc
