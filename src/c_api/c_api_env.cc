#include "c_api/c_api_env.h"
#include "common/env.h"
#include "core/logging.h"
int RdcEnvGetEnv(const char* key, int default_val) {
    return Env::Get()->GetEnv(key, default_val);
}

void RdcEnvFind(const char* key, char** val) {
    *val = const_cast<char*>(Env::Get()->Find(key));
    LOG(INFO) << *val;
    return;
}

int RdcEnvGetIntEnv(const char* key) {
    return Env::Get()->GetIntEnv(key);
}

