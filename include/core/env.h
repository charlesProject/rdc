#pragma once
#include <cstdlib>
#include <unordered_map>
#include <memory>
#include <string>

class Env {
public:
    static inline Env* Get() {
       return _GetSharedRef(nullptr).get();
    }
    static inline std::shared_ptr<Env> _GetSharedRef() {
        return _GetSharedRef(nullptr);
    }
    static inline Env* Init(
        const std::unordered_map<std::string, std::string>& envs) {
        return _GetSharedRef(&envs).get();
    }
    const char* Find(const char* k) {
        std::string key(k);
        return kvs.find(key) == kvs.end() ? getenv(k) : kvs[key].c_str();
    }
    static std::shared_ptr<Env> _GetSharedRef(
      const std::unordered_map<std::string, std::string>* envs) {
        static std::shared_ptr<Env> inst_ptr(new Env(envs));
        return inst_ptr;
    }
    template <typename V>
    inline V GetEnv(const char* key, V default_val) {
        const char* val = Find(key);
        if(val == nullptr) {
            return default_val;
        } else {
            return std::atoi(val);
        }
    }
    inline int32_t GetIntEnv(const char* key) {
        const char* val = Find(key);
        if(val == nullptr) {
            return 0;
        } else {
            return std::atoi(val);
        }
    }

private:
    explicit Env(const std::unordered_map<std::string, std::string>* envs) {
        if (envs) kvs = *envs;
    }

    std::unordered_map<std::string, std::string> kvs;
};
