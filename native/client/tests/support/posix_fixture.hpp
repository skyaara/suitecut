// SPDX-License-Identifier: MIT
#pragma once
#include <cerrno>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace suitecut_test {
// Fixtures create sockets before launching browser children. Keep every fixture
// descriptor close-on-exec on platforms without socket/accept4 flags as well.
inline int protect_socket(int fd, bool nonblocking = false) {
  if (fd < 0) return fd;
  const int flags = ::fcntl(fd, F_GETFL);
  if (::fcntl(fd, F_SETFD, FD_CLOEXEC) < 0 || flags < 0 ||
      ::fcntl(fd, F_SETFL, nonblocking ? flags | O_NONBLOCK : flags & ~O_NONBLOCK) < 0) {
    const int error = errno;
    ::close(fd); errno = error; return -1;
  }
  return fd;
}
inline int socket() { return protect_socket(::socket(AF_INET, SOCK_STREAM, 0), true); }
inline int accept(int listener, bool nonblocking = false) {
  pollfd ready{listener, POLLIN, 0};
  const int available = ::poll(&ready, 1, 100);
  if (available <= 0) { if (!available) errno = EAGAIN; return -1; }
  return protect_socket(::accept(listener, nullptr, nullptr), nonblocking);
}
}
