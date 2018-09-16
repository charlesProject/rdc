#ifdef _MSC_VER
#define XINLINE __forceinline
#pragma warning(disable : 4068)
#else
#define XINLINE inline __attribute__((always_inline))
#endif

#ifdef _WIN32
#define LOGERROR(msg)                                                          \
    LOG_F(ERROR, "Socket %s Error:WSAError-code=%d", msg, WSAGetLastError();
#else
#define LOGERROR(msg)                                                          \
    LOG_F(ERROR, "Socket %s Error: %d", msg, strerror(errno));
#endif

