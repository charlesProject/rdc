#pragma once
#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#ifdef _MSC_VER
#pragma comment(lib, "Ws2_32.lib")
#endif
#else
#include <fcntl.h>
#include <netdb.h>
#include <errno.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/ioctl.h>
#endif
#include <string>
#include <cstring>
#include "core/base.h"
#include "core/logging.h"
#if defined(_WIN32)
typedef int ssize_t;
typedef int sock_size_t;
#else
typedef int SOCKET;
typedef size_t sock_size_t;
const int INVALID_SOCKET = -1;
#endif
namespace rdc {
/*! \brief data structure for network address */
struct SockAddr {
    sockaddr_in addr;
    // constructor
    SockAddr() {
        std::memset(&addr, 0, sizeof(addr));
    }
    SockAddr(const char *url, int port) {
        std::memset(&addr, 0, sizeof(addr));
        this->Set(url, port);
    }
    SockAddr(std::string host,int port) {
        std::memset(&addr, 0, sizeof(addr));
        this->Set(host.c_str(), port);
    }
    inline static std::string GetHostName() {
        std::string buf; buf.resize(256);
        CHECK_S(gethostname(&buf[0], 256) != -1) << "fail to get host name";
        return std::string(buf.c_str());
    }
    /*!
    * \brief set the address
    * \param url the url of the address
    * \param port the port of address
    */
    inline void Set(const char *host, int port) {
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
    /*! \brief return port of the address*/
    inline int port() const {
        return ntohs(addr.sin_port);
    }
    /*! \return a string representation of the address */
    inline std::string AddrStr() const {
        std::string buf; buf.resize(256);
#ifdef _WIN32
        const char *s = inet_ntop(AF_INET, (PVOID)&addr.sin_addr,
            &buf[0], buf.length());
#else
        const char *s = inet_ntop(AF_INET, &addr.sin_addr,
            &buf[0], buf.length());
#endif
        CHECK_S(s != NULL) << "cannot decode address";
        return std::string(s);
    }
};

/*!
* \brief base class containing common operations of Tcp and Udp sockets
*/
class Socket {
public:
    /*! \brief the file descriptor of socket */
    SOCKET sockfd;
    // default conversion to int
    inline operator SOCKET() const {
        return sockfd;
    }
    /*!
    * \return last error of socket operation
    */
    inline static int GetLastError() {
#ifdef _WIN32
        return WSAGetLastError();
#else
        return errno;
#endif
    }
    /*! \return whether last error was would block */
    inline static bool LastErrorWouldBlock() {
        int errsv = GetLastError();
#ifdef _WIN32
        return errsv == WSAEWOULDBLOCK;
#else
        return errsv == EAGAIN || errsv == EWOULDBLOCK;
#endif
    }
    /*!
    * \brief start up the socket module
    *   call this before using the sockets
    */
    inline static void Startup() {
#ifdef _WIN32
        WSADATA wsa_data;
        if (WSAStartup(MAKEWORD(2, 2), &wsa_data) == -1) {
            LOGERROR("Startup");
        }
        if (LOBYTE(wsa_data.wVersion) != 2 || HIBYTE(wsa_data.wVersion) != 2) {
            WSACleanup();
            logging::LOG_S(ERROR) << "Could not find a usable version of Winsock.dll";
        }
#endif
    }
    /*!
    * \brief shutdown the socket module after use, all sockets need to be closed
    */
    inline static void Finalize() {
#ifdef _WIN32
        WSACleanup();
#endif
    }
    /*!
    * \brief set this socket to use non-blocking mode
    * \param non_block whether set it to be non-block, if it is false
    *        it will set it back to block mode
    */
    inline void SetNonBlock(bool non_block) {
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
        }
        else {
            flag &= ~O_NONBLOCK;
        }
        if (fcntl(sockfd, F_SETFL, flag) == -1) {
            LOGERROR("SetNonBlock-2");
        }
#endif
    }
    /*!
    * \brief bind the socket to an address
    * \param addr
    * \param reuse whether or not to resue the address
    */
    inline void Bind(const SockAddr &addr) {
        if (bind(sockfd, reinterpret_cast<const sockaddr*>(&addr.addr),
            sizeof(addr.addr)) == -1) {
            LOGERROR("Bind");
        }
    }
    /*!
    * \brief try bind the socket to host, from start_port to end_port
    * \param end_port ending port number to try
    * \return the port successfully bind to, return -1 if failed to bind any port
    */
    inline int TryBindHost(int port) {
        SockAddr addr("0.0.0.0", port);
        addr.addr.sin_addr.s_addr = htonl(INADDR_ANY);
        if (bind(sockfd, reinterpret_cast<sockaddr*>(&addr.addr),
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
    /*!
    * \brief try bind the socket to host, from start_port to end_port
    * \param start_port starting port number to try
    * \param end_port ending port number to try
    * \return the port successfully bind to, return -1 if failed to bind any port
    */
    inline int TryBindHost(int start_port, int end_port) {
        for (int port = start_port; port < end_port; ++port) {
            SockAddr addr("0.0.0.0", port);
            if (bind(sockfd, reinterpret_cast<sockaddr*>(&addr.addr),
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
    /*! \brief get last error code if any */
    inline int GetSockError() const {
        int error = 0;
        socklen_t len = sizeof(error);
        if (getsockopt(sockfd, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&error), &len) != 0) {
            Error("GetSockError");
        }
        return error;
    }
    /*! \brief check if anything bad happens */
    inline bool BadSocket() const {
        if (IsClosed()) return true;
        int err = GetSockError();
        if (err == EBADF || err == EINTR) return true;
        return false;
    }
    /*! \brief check if socket is already closed */
    inline bool IsClosed() const {
        return sockfd == INVALID_SOCKET;
    }
    /*! \brief close the socket */
    inline void Close() {
        if (sockfd != INVALID_SOCKET) {
#ifdef _WIN32
            closesocket(sockfd);
#else
            close(sockfd);
#endif
            sockfd = INVALID_SOCKET;
        }
        else {
            Error("Socket::Close double close the socket or close without create");
        }
    }
    // report an socket error
    inline static void Error(const char *msg) {
#ifdef _WIN32
        LOG_F(ERROR, "Socket %s Error:WSAError-code=%d",
                msg, WSAGetLastError());
#else
        LOG_F(ERROR, "Socket %s Error: %s", msg, strerror(errno));
#endif

    }

protected:
    explicit Socket(SOCKET sockfd) : sockfd(sockfd) {
    }
    };

/*!
* \brief a wrapper of Tcp socket that hopefully be cross platform
*/
class TcpSocket : public Socket {
public:
    // constructor
    TcpSocket(bool create = true) : Socket(INVALID_SOCKET) {
        if(create)
            Create();
    }
    explicit TcpSocket(SOCKET sockfd,bool create = true) : Socket(sockfd) {
        if(create)
            Create();
    }
    /*!
    * \brief enable/disable Tcp keepalive
    * \param keepalive whether to set the keep alive option on
    */
    inline void SetKeepAlive(bool keepalive) {
        int opt = static_cast<int>(keepalive);
        if (setsockopt(sockfd, SOL_SOCKET, SO_KEEPALIVE,
            reinterpret_cast<char*>(&opt), sizeof(opt)) < 0) {
            LOGERROR("SetKeepAlive");
        }
    }
    /*!
    * \brief enable/disable Tcp addr_reuse
    * \param reuse whether to set the address reuse option on
    */
    inline void SetReuseAddr(bool reuse) {
        int opt = static_cast<int>(reuse);
        if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR,
            reinterpret_cast<char*>(&opt), sizeof(opt)) < 0) {
            LOGERROR("SetReuseAddr");
        }
    }
    /*!
    * \brief create the socket, call this before using socket
    * \param af domain
    */
    inline void Create(int af = PF_INET) {
        sockfd = socket(af, SOCK_STREAM, 0);
        if (sockfd == INVALID_SOCKET) {
            LOGERROR("Create");
        }
    }
    /*!
    * \brief bind the socket to an address
    * \param addr
    * \param reuse whether or not to resue the address
    */
    inline void Bind(const SockAddr &addr,bool reuse = true) {
        SetReuseAddr(reuse);
        Socket::Bind(addr);
    }

    /*!
    * \brief perform listen of the socket
    * \param backlog backlog parameter
    */
    inline bool Listen(int backlog = 128) {
        if (listen(sockfd, backlog) == -1) {
            return false;
        } else {
            return true;
        }
    }
    /*! \brief get a new connection */
    TcpSocket Accept() {
        SOCKET newfd = accept(sockfd, NULL, NULL);
        if (newfd == INVALID_SOCKET) {
            LOGERROR("Accept");
        }
        return TcpSocket(newfd, false);
    }
    /*!
    * \brief decide whether the socket is at OOB mark
    * \return 1 if at mark, 0 if not, -1 if an error occured
    */
    inline int AtMark() const {
#ifdef _WIN32
        unsigned long atmark;  // NOLINT(*)
        if (ioctlsocket(sockfd, SIOCATMARK, &atmark) != NO_LOGERROR) return -1;
#else
        int atmark;
        if (ioctl(sockfd, SIOCATMARK, &atmark) == -1) return -1;
#endif
        return static_cast<int>(atmark);
    }
    /*!
    * \brief connect to an address
    * \param addr the address to connect to
    * \return whether connect is successful
    */
    inline bool Connect(const SockAddr &addr) {
        int ret = connect(sockfd, reinterpret_cast<const sockaddr*>(&addr.addr),
            sizeof(addr.addr));
        if (ret != 0) {
            LOGERROR("Connect");
            return false;
        }
        return true;
    }
    /*!
    * \brief connect to an address
    * \param url the hostname of the server address 
    * \param port the port of the server address
    * \return whether connect is successful
  */
    inline bool Connect(const std::string& url,int port) {
        SockAddr serv_addr(url,port);
        return Connect(serv_addr);
    }
    /*!
    * \brief send data using the socket
    * \param buf the pointer to the buffer
    * \param len the size of the buffer
    * \param flags extra flags
    * \return size of data actually sent
    *         return -1 if error occurs
    */
    inline ssize_t Send(const void *buf_, size_t len, int flag = 0) {
        const char *buf = reinterpret_cast<const char*>(buf_);
        return send(sockfd, buf, static_cast<sock_size_t>(len), flag);
    }
    /*!
    * \brief receive data using the socket
    * \param buf_ the pointer to the buffer
    * \param len the size of the buffer
    * \param flags extra flags
    * \return size of data actually received
    *         return -1 if error occurs
    */
    inline ssize_t Recv(void *buf_, size_t len, int flags = 0) {
        char *buf = reinterpret_cast<char*>(buf_);
        return recv(sockfd, buf, static_cast<sock_size_t>(len), flags);
    }
    /*!
    * \brief peform block write that will attempt to send all data out
    *    can still return smaller than request when error occurs
    * \param buf the pointer to the buffer
    * \param len the size of the buffer
    * \return size of data actually sent
    */
    inline size_t SendAll(const void *buf_, size_t len) {
        const char *buf = reinterpret_cast<const char*>(buf_);
        size_t ndone = 0;
        while (ndone < len) {
            ssize_t ret = send(sockfd, buf, static_cast<ssize_t>(len - ndone), 0);
            if (ret == -1) {
                if (LastErrorWouldBlock()) return ndone;
                LOGERROR("SendAll");
            }
            buf += ret;
            ndone += ret;
        }
        return ndone;
    }
    /*!
    * \brief peforma block read that will attempt to read all data
    *    can still return smaller than request when error occurs
    * \param buf_ the buffer pointer
    * \param len length of data to recv
    * \return size of data actually sent
    */
    inline size_t RecvAll(void *buf_, size_t len) {
        char *buf = reinterpret_cast<char*>(buf_);
        size_t ndone = 0;
        while (ndone < len) {
            ssize_t ret = recv(sockfd, buf,
                static_cast<sock_size_t>(len - ndone), MSG_WAITALL);
            if (ret == -1) {
                if (LastErrorWouldBlock()) return ndone;
                LOGERROR("RecvAll");
            }
            if (ret == 0) return ndone;
            buf += ret;
            ndone += ret;
        }
        return ndone;
    }
    /*!
     * \brief send a string over network
     * \param str the string to be sent
     */
    inline void SendStr(const std::string &str) {
      int32_t len = static_cast<int32_t>(str.length());
      CHECK_F(this->SendAll(&len, sizeof(len)) == sizeof(len),
                    "error during send SendStr");
      if (len != 0) {
          CHECK_F(this->SendAll(str.c_str(), str.length()) == str.length(),
                      "error during send SendStr");
      }
    }
    /*!
     * \brief recv a string from network
     * \param out_str the string to receive
     */
    inline void RecvStr(std::string& out_str) {
      int32_t len;
      CHECK_F(this->RecvAll(&len, sizeof(len)) == sizeof(len),
                    "error during send RecvStr");
      out_str.resize(len);
      if (len != 0) {
          CHECK_F(this->RecvAll(&(out_str)[0], len) == out_str.length(),
                      "error during send SendStr");
      }
    }
    /*!
     * \brief send a string over network
     * \param val the integer to be sent
     */
    inline void SendInt(const int32_t& val) {
        CHECK_F(this->SendAll(&val, sizeof(val)) == sizeof(val),
                      "error during send SendInt");
    }
    /*!
     * \brief recv a int from network
     * \param out_val the integer to receive
     */
    inline void RecvInt(int32_t& out_val) {
        CHECK_F(this->RecvAll(&out_val, sizeof(out_val)) == sizeof(out_val),
                      "error during send RecvInt");
    }
};
}
