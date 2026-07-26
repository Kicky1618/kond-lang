#pragma once

#include <cstddef>
#include <stdexcept>

#ifdef _WIN32
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  include <windows.h>
#else
#  include <cerrno>
#  include <sys/socket.h>
#  include <sys/types.h>
#  include <unistd.h>
#endif

namespace kond {

#ifdef _WIN32
using Socket = SOCKET;
using SocketLength = int;
inline constexpr Socket kInvalidSocket = INVALID_SOCKET;

inline void ensureSocketsInitialized() {
    static const bool initialized = [] {
        WSADATA data{};
        return WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }();
    if (!initialized) throw std::runtime_error("Winsockを初期化できません");
}

inline bool socketInterrupted() { return WSAGetLastError() == WSAEINTR; }
inline int socketReceive(Socket socket, char *buffer, int length) {
    return ::recv(socket, buffer, length, 0);
}
inline int socketSend(Socket socket, const char *buffer, int length, int flags) {
    return ::send(socket, buffer, length, flags);
}
inline void closeSocket(Socket socket) { ::closesocket(socket); }
inline void shutdownSocket(Socket socket) { ::shutdown(socket, SD_BOTH); }
inline unsigned long processId() { return ::GetCurrentProcessId(); }
#else
using Socket = int;
using SocketLength = socklen_t;
inline constexpr Socket kInvalidSocket = -1;

inline void ensureSocketsInitialized() {}
inline bool socketInterrupted() { return errno == EINTR; }
inline ssize_t socketReceive(Socket socket, char *buffer, std::size_t length) {
    return ::recv(socket, buffer, length, 0);
}
inline ssize_t socketSend(Socket socket, const char *buffer, std::size_t length, int flags) {
    return ::send(socket, buffer, length, flags);
}
inline void closeSocket(Socket socket) { ::close(socket); }
inline void shutdownSocket(Socket socket) { ::shutdown(socket, SHUT_RDWR); }
inline unsigned long processId() { return static_cast<unsigned long>(::getpid()); }
#endif

} // namespace kond
