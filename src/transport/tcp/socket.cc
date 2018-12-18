#pragma once
#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#ifdef _MSC_VER
#pragma comment(lib, "Ws2_32.lib")
#endif
#else
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif
#include <cstring>
#include <string>
#include "core/base.h"
#include "core/logging.h"
namespace rdc {
SockAddr::SockAddr() {
    std::memset(&addr, 0, sizeof(addr));
}
SockAddr::SockAddr(const char *url, int port) {
    std::memset(&addr, 0, sizeof(addr));
    this->Set(url, port);
}
SockAddr::SockAddr(std::string host, int port) {
    std::memset(&addr, 0, sizeof(addr));
    this->Set(host.c_str(), port);
}
std::string SockAddr::GetHostName() {
    std::string buf;
    buf.resize(256);
    CHECK_S(gethostname(&buf[0], 256) != -1) << "fail to get host name";
    return std::string(buf.c_str());
}
void SockAddr::Set(const char *host, int port) {
    addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_protocol = SOCK_STREAM;
    addrinfo *res = NULL;
    int sig = getaddrinfo(host, NULL, &hints, &res);
    CHECK_S(sig == 0 && res != NULL) << "cannot obtain address of " << host;
    CHECK_S(res->ai_family == AF_INET) << "Does not support IPv6";
    memcpy(&addr, res->ai_addr, res->ai_addrlen);
    addr.sin_port = htons(port);
    freeaddrinfo(res);
}
int SockAddr::port() const {
    return ntohs(addr.sin_port);
}
std::string SockAddr::AddrStr() const {
    std::string buf;
    buf.resize(256);
#ifdef _WIN32
    const char *s =
        inet_ntop(AF_INET, (PVOID)&addr.sin_addr, &buf[0], buf.length());
#else
    const char *s = inet_ntop(AF_INET, &addr.sin_addr, &buf[0], buf.length());
#endif
    CHECK_S(s != NULL) << "cannot decode address";
    return std::string(s);
}

operator Socket::SOCKET() const {
    return sockfd;
}
int Socket::GetLastError() {
#ifdef _WIN32
    return WSAGetLastError();
#else
    return errno;
#endif
}
/*! @return whether last error was would block */
bool Socket::LastErrorWouldBlock() {
    int errsv = GetLastError();
#ifdef _WIN32
    return errsv == WSAEWOULDBLOCK;
#else
    return errsv == EAGAIN || errsv == EWOULDBLOCK;
#endif
}
void Socket::Startup() {
#ifdef _WIN32
    WSADATA wsa_data;
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) == -1) {
        LOGERROR("Startup");
    }
    if (LOBYTE(wsa_data.wVersion) != 2 || HIBYTE(wsa_data.wVersion) != 2) {
        WSACleanup();
        logging::LOG_S(ERROR)
            << "Could not find a usable version of Winsock.dll";
    }
#endif
}
void Socket::Finalize() {
#ifdef _WIN32
    WSACleanup();
#endif
}
void Socket::SetNonBlock(bool non_block) {
#ifdef _WIN32
    u_long mode = non_block ? 1 : 0;
    if (ioctlsocket(sockfd, FIONBIO, &mode) != NO_LOGERROR) {
        LOGERROR("SetNonBlock");
    }
#else
    int flag = fcntl(sockfd, F_GETFL, 0);
    if (flag == -1) {
        LOGERROR("SetNonBlock-1");
    }
    if (non_block) {
        flag |= O_NONBLOCK;
    } else {
        flag &= ~O_NONBLOCK;
    }
    if (fcntl(sockfd, F_SETFL, flag) == -1) {
        LOGERROR("SetNonBlock-2");
    }
#endif
}
void Socket::Bind(const SockAddr &addr) {
    if (bind(sockfd, reinterpret_cast<const sockaddr *>(&addr.addr),
             sizeof(addr.addr)) == -1) {
        LOGERROR("Bind");
    }
}
int Socket::TryBindHost(int port) {
    SockAddr addr("0.0.0.0", port);
    addr.addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(sockfd, reinterpret_cast<sockaddr *>(&addr.addr),
             sizeof(addr.addr)) == 0) {
        return port;
    }
#if defined(_WIN32)
    if (WSAGetLastError() != WSAEADDRINUSE) {
        LOGERROR("TryBindHost");
    }
#else
    if (errno != EADDRINUSE) {
        LOGERROR("TryBindHost");
    }
#endif
    return -1;
}
int Socket::TryBindHost(int start_port, int end_port) {
    for (int port = start_port; port < end_port; ++port) {
        SockAddr addr("0.0.0.0", port);
        if (bind(sockfd, reinterpret_cast<sockaddr *>(&addr.addr),
                 sizeof(addr.addr)) == 0) {
            return port;
        }
#if defined(_WIN32)
        if (WSAGetLastError() != WSAEADDRINUSE) {
            LOGERROR("TryBindHost");
        }
#else
        if (errno != EADDRINUSE) {
            LOGERROR("TryBindHost");
        }
#endif
    }

    return -1;
}
int Socket::GetSockError() const {
    int error = 0;
    socklen_t len = sizeof(error);
    if (getsockopt(sockfd, SOL_SOCKET, SO_ERROR,
                   reinterpret_cast<char *>(&error), &len) != 0) {
        Error("GetSockError");
    }
    return error;
}
bool Socket::BadSocket() const {
    if (IsClosed())
        return true;
    int err = GetSockError();
    if (err == EBADF || err == EINTR)
        return true;
    return false;
}
bool Socket::IsClosed() const {
    return sockfd == INVALID_SOCKET;
}
/*! @brief close the socket */
void Socket::Close() {
    if (sockfd != INVALID_SOCKET) {
#ifdef _WIN32
        closesocket(sockfd);
#else
        close(sockfd);
#endif
        sockfd = INVALID_SOCKET;
    } else {
        Error(
            "Socket::Close double close the socket or close without "
            "create");
    }
}
// report an socket error
void Socket::Error(const char *msg) {
#ifdef _WIN32
    LOG_F(ERROR, "Socket %s Error:WSAError-code=%d", msg, WSAGetLastError());
#else
    LOG_F(ERROR, "Socket %s Error: %s", msg, strerror(errno));
#endif
}

Socket::Socket(SOCKET sockfd) : sockfd(sockfd) {
}
// constructor
TcpSocket::TcpSocket(bool create = true) : Socket(INVALID_SOCKET) {
    if (create)
        Create();
}

TcpSocket::TTcpSocket(SOCKET sockfd, bool create = true) : Socket(sockfd) {
    if (create)
        Create();
}

void TcpSocket::SetKeepAlive(bool keepalive) {
    int opt = _cast<int>(keepalive);
    if (setsockopt(sockfd, SOL_SOCKET, SO_KEEPALIVE,
                   reinterpret_cast<char *>(&opt), sizeof(opt)) < 0) {
        LOGERROR("SetKeepAlive");
    }
}

void TcpSocket::SetReuseAddr(bool reuse) {
    int opt = _cast<int>(reuse);
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR,
                   reinterpret_cast<char *>(&opt), sizeof(opt)) < 0) {
        LOGERROR("SetReuseAddr");
    }
}

void TcpSocket::Create(int af = PF_INET) {
    sockfd = socket(af, SOCK_STREAM, 0);
    if (sockfd == INVALID_SOCKET) {
        LOGERROR("Create");
    }
}
void TcpSocket::Bind(const SockAddr &addr, bool reuse = true) {
    SetReuseAddr(reuse);
    Socket::Bind(addr);
}

bool TcpSocket::Listen(int backlog = 128) {
    if (listen(sockfd, backlog) == -1) {
        return false;
    } else {
        return true;
    }
}
TcpSocket TcpSocket::Accept() {
    SOCKET newfd = accept(sockfd, NULL, NULL);
    if (newfd == INVALID_SOCKET) {
        LOGERROR("Accept");
    }
    return TcpSocket(newfd, false);
}
int TcpSocket::AtMark() const {
#ifdef _WIN32
    unsigned long atmark;  // NOLINT(*)
    if (ioctlsocket(sockfd, SIOCATMARK, &atmark) != NO_LOGERROR)
        return -1;
#else
    int atmark;
    if (ioctl(sockfd, SIOCATMARK, &atmark) == -1)
        return -1;
#endif
    return _cast<int>(atmark);
}
bool TcpSocket::Connect(const SockAddr &addr) {
    int ret = connect(sockfd, reinterpret_cast<const sockaddr *>(&addr.addr),
                      sizeof(addr.addr));
    if (ret != 0) {
        LOGERROR("Connect");
        return false;
    }
    return true;
}
bool TcpSocket::Connect(const std::string &url, int port) {
    SockAddr serv_addr(url, port);
    return Connect(serv_addr);
}
ssize_t TcpSocket::Send(const void *buf_, size_t len, int flag = 0) {
    const char *buf = reinterpret_cast<const char *>(buf_);
    return send(sockfd, buf, _cast<sock_size_t>(len), flag);
}
ssize_t TcpSocket::Recv(void *buf_, size_t len, int flags = 0) {
    char *buf = reinterpret_cast<char *>(buf_);
    return recv(sockfd, buf, _cast<sock_size_t>(len), flags);
}
size_t TcpSocket::SendAll(const void *buf_, size_t len) {
    const char *buf = reinterpret_cast<const char *>(buf_);
    size_t ndone = 0;
    while (ndone < len) {
        ssize_t ret = send(sockfd, buf, _cast<ssize_t>(len - ndone), 0);
        if (ret == -1) {
            if (LastErrorWouldBlock())
                return ndone;
            LOGERROR("SendAll");
        }
        buf += ret;
        ndone += ret;
    }
    return ndone;
}
size_t TcpSocket::RecvAll(void *buf_, size_t len) {
    char *buf = reinterpret_cast<char *>(buf_);
    size_t ndone = 0;
    while (ndone < len) {
        ssize_t ret =
            recv(sockfd, buf, _cast<sock_size_t>(len - ndone), MSG_WAITALL);
        if (ret == -1) {
            if (LastErrorWouldBlock())
                return ndone;
            LOGERROR("RecvAll");
        }
        if (ret == 0)
            return ndone;
        buf += ret;
        ndone += ret;
    }
    return ndone;
}
void TcpSocket::SendStr(const std::string &str) {
    int32_t len = _cast<int32_t>(str.length());
    CHECK_F(this->SendAll(&len, sizeof(len)) == sizeof(len),
            "error during send SendStr");
    if (len != 0) {
        CHECK_F(this->SendAll(str.c_str(), str.length()) == str.length(),
                "error during send SendStr");
    }
}
void TcpSocket::RecvStr(std::string &out_str) {
    int32_t len;
    CHECK_F(this->RecvAll(&len, sizeof(len)) == sizeof(len),
            "error during send RecvStr");
    out_str.resize(len);
    if (len != 0) {
        CHECK_F(this->RecvAll(&(out_str)[0], len) == out_str.length(),
                "error during send SendStr");
    }
}
void TcpSocket::SendInt(const int32_t &val) {
    CHECK_F(this->SendAll(&val, sizeof(val)) == sizeof(val),
            "error during send SendInt");
}
void TcpSocket::RecvInt(int32_t &out_val) {
    CHECK_F(this->RecvAll(&out_val, sizeof(out_val)) == sizeof(out_val),
            "error during send RecvInt");
}  // namespace rdc
