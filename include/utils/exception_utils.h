#pragma once
#include <stdexcept>
#include <exception>
#include <string>
void LogException(const std::exception& e, int level = 0) {
    LOG_F(ERROR, "%sexception: %s", std::string(level, ' ').c_str(),
          e.what().c_str());
    try {
        std::rethrow_if_nested(e);
    } catch (const std::exception& e) {
        LogException(e, level + 1);
    } catch (...) {
    }
}
