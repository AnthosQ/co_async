module;

#ifdef __linux__
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <sys/un.h>
#endif

export module co_async:system.socket;

import std;

#ifdef __linux__
import :system.error_handling;
import :system.fs;
import :system.system_loop;
import :utils.string_utils;
import :awaiter.task;

namespace co_async {

export struct IpAddress {
    explicit IpAddress(struct in_addr const &addr) noexcept : mAddr(addr) {}

    explicit IpAddress(struct in6_addr const &addr6) noexcept : mAddr(addr6) {}

    IpAddress(char const *ip) {
        in_addr addr = {};
        in6_addr addr6 = {};
        if (checkError(inet_pton(AF_INET, ip, &addr))) {
            mAddr = addr;
            return;
        }
        if (checkError(inet_pton(AF_INET6, ip, &addr6))) {
            mAddr = addr6;
            return;
        }
        hostent *hent = gethostbyname(ip);
        for (int i = 0; hent->h_addr_list[i]; i++) {
            if (hent->h_addrtype == AF_INET) {
                std::memcpy(&addr, hent->h_addr_list[i], sizeof(in_addr));
                mAddr = addr;
                return;
            } else if (hent->h_addrtype == AF_INET6) {
                std::memcpy(&addr6, hent->h_addr_list[i], sizeof(in6_addr));
                mAddr = addr6;
                return;
            }
        }
        throw std::invalid_argument("invalid domain name or ip address");
    }

    std::string toString() const {
        if (mAddr.index() == 1) {
            char buf[INET6_ADDRSTRLEN + 1] = {};
            inet_ntop(AF_INET6, &std::get<1>(mAddr), buf, sizeof(buf));
            return buf;
        } else {
            char buf[INET_ADDRSTRLEN + 1] = {};
            inet_ntop(AF_INET, &std::get<0>(mAddr), buf, sizeof(buf));
            return buf;
        }
    }

    auto repr() const {
        return toString();
    }

    std::variant<in_addr, in6_addr> mAddr;
};

export struct SocketAddress {
    SocketAddress() = default;

    SocketAddress(IpAddress ip, int port) {
        std::visit([&](auto const &addr) { initFromHostPort(addr, port); },
                   ip.mAddr);
    }

    union {
        struct sockaddr_in mAddrIpv4;
        struct sockaddr_in6 mAddrIpv6;
        struct sockaddr mAddr;
    };

    socklen_t mAddrLen;

    sa_family_t family() const noexcept {
        return mAddr.sa_family;
    }

    IpAddress host() const {
        if (family() == AF_INET) {
            return IpAddress(mAddrIpv4.sin_addr);
        } else if (family() == AF_INET6) {
            return IpAddress(mAddrIpv6.sin6_addr);
        } else [[unlikely]] {
            throw std::runtime_error("address family not ipv4 or ipv6");
        }
    }

    int port() const {
        if (family() == AF_INET) {
            return ntohs(mAddrIpv4.sin_port);
        } else if (family() == AF_INET6) {
            return ntohs(mAddrIpv6.sin6_port);
        } else [[unlikely]] {
            throw std::runtime_error("address family not ipv4 or ipv6");
        }
    }

    auto toString() const {
        return host().toString() + ":" + to_string(port());
    }

    auto repr() const {
        return toString();
    }

private:
    void initFromHostPort(struct in_addr const &host, int port) {
        struct sockaddr_in saddr = {};
        saddr.sin_family = AF_INET;
        std::memcpy(&saddr.sin_addr, &host, sizeof(saddr.sin_addr));
        saddr.sin_port = htons(port);
        std::memcpy(&mAddrIpv4, &saddr, sizeof(saddr));
        mAddrLen = sizeof(saddr);
    }

    void initFromHostPort(struct in6_addr const &host, int port) {
        struct sockaddr_in6 saddr = {};
        saddr.sin6_family = AF_INET6;
        std::memcpy(&saddr.sin6_addr, &host, sizeof(saddr.sin6_addr));
        saddr.sin6_port = htons(port);
        std::memcpy(&mAddrIpv6, &saddr, sizeof(saddr));
        mAddrLen = sizeof(saddr);
    }
};

export struct [[nodiscard]] SocketHandle : FileHandle {
    using FileHandle::FileHandle;
};

export struct [[nodiscard]] SocketServer : SocketHandle {
    using SocketHandle::SocketHandle;

    SocketAddress mAddr;

    SocketAddress const &address() const noexcept {
        return mAddr;
    }
};

export inline SocketAddress get_socket_address(SocketHandle &sock) {
    SocketAddress sa;
    sa.mAddrLen = sizeof(sa.mAddrIpv6);
    checkError(getsockname(sock.fileNo(), (sockaddr *)&sa.mAddr, &sa.mAddrLen));
    return sa;
}

template <class T>
inline T socketGetOption(SocketHandle &sock, int level, int optId) {
    T val;
    socklen_t len = sizeof(val);
    checkError(getsockopt(sock.fileNo(), level, optId, &val, &len));
    return val;
}

template <class T>
inline void socketSetOption(SocketHandle &sock, int level, int opt,
                            T const &optVal) {
    checkError(setsockopt(sock.fileNo(), level, opt, &optVal, sizeof(optVal)));
}

inline Task<SocketHandle> createSocket(int family, int type) {
    int fd = co_await uring_socket(loop, family, type, 0, 0);
    SocketHandle sock(fd);
    co_return sock;
}

export inline Task<SocketHandle> socket_connect(SocketAddress const &addr) {
    SocketHandle sock = co_await createSocket(addr.family(), SOCK_STREAM);
    co_await uring_connect(loop, sock.fileNo(),
                           (const struct sockaddr *)&addr.mAddr, addr.mAddrLen);
    co_return sock;
}

export inline Task<SocketServer> server_bind(SocketAddress const &addr,
                                             int backlog = SOMAXCONN) {
    SocketHandle sock = co_await createSocket(addr.family(), SOCK_STREAM);
    socketSetOption(sock, SOL_SOCKET, SO_REUSEADDR, 1);
    SocketServer serv(sock.releaseFile());
    checkError(bind(serv.fileNo(), (struct sockaddr const *)&addr.mAddr,
                    addr.mAddrLen));
    checkError(listen(serv.fileNo(), backlog));
    serv.mAddr = addr;
    co_return serv;
}

export inline Task<SocketHandle> server_accept(SocketServer &serv) {
    int fd = co_await uring_accept(loop, serv.fileNo(),
                                   (struct sockaddr *)&serv.mAddr.mAddr,
                                   &serv.mAddr.mAddrLen, 0);
    SocketHandle sock(fd);
    co_return sock;
}

} // namespace co_async
#endif
