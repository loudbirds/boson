#include "boson/syscalls.h"
#include "boson/exception.h"
#include "boson/internal/routine.h"
#include "boson/internal/thread.h"
#include "boson/syscall_traits.h"
#include "boson/engine.h"

namespace boson {

using namespace internal;

void yield() {
  thread* this_thread = current_thread();
  routine* current_routine = this_thread->running_routine();
  current_routine->status_ = routine_status::yielding;
  this_thread->context() = jump_fcontext(this_thread->context().fctx, nullptr);
  current_routine->previous_status_ = routine_status::yielding;
  current_routine->status_ = routine_status::running;
}

void sleep(std::chrono::milliseconds duration) {
  // Compute the time in ms
  using namespace std::chrono;
  thread* this_thread = current_thread();
  routine* current_routine = this_thread->running_routine();
  current_routine->start_event_round();
  current_routine->add_timer(
      time_point_cast<milliseconds>(high_resolution_clock::now() + duration));
  current_routine->commit_event_round();
  current_routine->previous_status_ = routine_status::wait_events;
  current_routine->status_ = routine_status::running;
}

template <bool IsARead>
int wait_readiness(fd_t fd, int timeout_ms) {
  using namespace std::chrono;
  thread* this_thread = current_thread();
  routine* current_routine = this_thread->running_routine();
  current_routine->start_event_round();
  add_event<IsARead>::apply(current_routine, fd);
  if (0 <= timeout_ms) {
    current_routine->add_timer(time_point_cast<milliseconds>(high_resolution_clock::now() + milliseconds(timeout_ms)));
  }
  current_routine->commit_event_round();
  current_routine->previous_status_ = routine_status::wait_events;
  current_routine->status_ = routine_status::running;

  int return_code = 0;
  if (current_routine->happened_rc_  < 0) {
    errno = -current_routine->happened_rc_;
    return_code = -1;
  }
  return return_code;
}

template <int SyscallId> struct boson_classic_syscall {
  template <class... Args>
  static inline decltype(auto) call_timeout(int fd, int timeout_ms, Args&&... args) {
    auto return_code = syscall_callable<SyscallId>::call(fd, std::forward<Args>(args)...);
    while(return_code < 0 && (EAGAIN == errno || EWOULDBLOCK == errno)) {
      return_code = wait_readiness<syscall_traits<SyscallId>::is_read>(fd, timeout_ms);
      if (0 == return_code) {
        return_code = syscall_callable<SyscallId>::call(fd, std::forward<Args>(args)...);
      }
    }
    return return_code;
  }
};

fd_t open(const char *pathname, int flags) {
  fd_t fd = ::syscall(SYS_open,pathname, flags | O_NONBLOCK, 0755);
  if (0 <= fd)
    current_thread()->engine_proxy_.get_engine().event_loop().signal_new_fd(fd);
  return fd;
}

fd_t open(const char *pathname, int flags, mode_t mode) {
  fd_t fd = ::syscall(SYS_open,pathname, flags | O_NONBLOCK, mode);
  if (0 <= fd)
    current_thread()->engine_proxy_.get_engine().event_loop().signal_new_fd(fd);
  return fd;
}

fd_t creat(const char *pathname, mode_t mode) {
  fd_t fd = ::syscall(SYS_open,pathname, O_CREAT | O_WRONLY | O_TRUNC| O_NONBLOCK, mode);
  if (0 <= fd)
    current_thread()->engine_proxy_.get_engine().event_loop().signal_new_fd(fd);
  return fd;
}

fd_t pipe(fd_t (&fds)[2]) {
  int rc = ::syscall(SYS_pipe2, fds, O_NONBLOCK);
  if (0 == rc) {
    current_thread()->engine_proxy_.get_engine().event_loop().signal_new_fd(fds[0]);
    current_thread()->engine_proxy_.get_engine().event_loop().signal_new_fd(fds[1]);
  }
  return rc;
}

fd_t pipe2(fd_t (&fds)[2], int flags) {
  int rc = ::syscall(SYS_pipe2, fds, flags | O_NONBLOCK);
  if (0 == rc) {
    current_thread()->engine_proxy_.get_engine().event_loop().signal_new_fd(fds[0]);
    current_thread()->engine_proxy_.get_engine().event_loop().signal_new_fd(fds[1]);
  }
  return rc;
}

socket_t socket(int domain, int type, int protocol) {
  socket_t socket = ::syscall(SYS_socket, domain, type | SOCK_NONBLOCK, protocol);
  if (0 <= socket)
    current_thread()->engine_proxy_.get_engine().event_loop().signal_new_fd(socket);
  return socket;
}

ssize_t read(fd_t fd, void *buf, size_t count) {
  return read(fd,buf,count,-1);
}

ssize_t write(fd_t fd, const void *buf, size_t count) {
  return write(fd, buf, count, -1);
}

socket_t accept(socket_t socket, sockaddr *address, socklen_t *address_len) {
  return accept(socket, address, address_len, -1);
}

ssize_t send(socket_t socket, const void *buffer, size_t length, int flags) {
  return send(socket, buffer, length, flags, -1);
}

ssize_t recv(socket_t socket, void *buffer, size_t length, int flags) {
  return recv(socket, buffer, length, flags, -1);
}

ssize_t read(fd_t fd, void* buf, size_t count, int timeout_ms) {
  return boson_classic_syscall<SYS_read>::call_timeout(fd, timeout_ms, buf,count);
}

ssize_t write(fd_t fd, const void* buf, size_t count, int timeout_ms) {
  return boson_classic_syscall<SYS_write>::call_timeout(fd, timeout_ms, buf,count);
}

socket_t accept(socket_t socket, sockaddr* address, socklen_t* address_len, int timeout_ms) {
  socket_t new_socket = boson_classic_syscall<SYS_accept>::call_timeout(socket, timeout_ms, address, address_len);
  if (0 <= new_socket) {
    ::fcntl(new_socket, F_SETFL, ::fcntl(new_socket, F_GETFD) | O_NONBLOCK);
    current_thread()->engine_proxy_.get_engine().event_loop().signal_new_fd(new_socket);
  }
  return new_socket;
}

ssize_t send(socket_t socket, const void* buffer, size_t length, int flags, int timeout_ms) {
  return boson_classic_syscall<SYS_sendto>::call_timeout(socket, timeout_ms, buffer, length, flags, nullptr, 0);
}

ssize_t recv(socket_t socket, void* buffer, size_t length, int flags, int timeout_ms) {
  return boson_classic_syscall<SYS_recvfrom>::call_timeout(socket, timeout_ms, buffer, length, flags, nullptr, 0);
}

/**
 * Connect system call
 *
 * Its implementation is a bit different from the others
 * the connect call does not have to be made again, and the retured error
 * is not EAGAIN
 */
int connect(socket_t sockfd, const sockaddr* addr, socklen_t addrlen) {
  return connect(sockfd, addr, addrlen, -1);
}

/**
 * Connect system call
 *
 * Its implementation is a bit different from the others
 * the connect call does not have to be made again, and the retured error
 * is not EAGAIN
 */
int connect(socket_t sockfd, const sockaddr* addr, socklen_t addrlen, int timeout_ms) {
  int return_code = syscall_callable<SYS_connect>::call(sockfd, addr, addrlen);
  if (return_code < 0 && errno == EINPROGRESS) {
    return_code = wait_readiness<syscall_traits<SYS_connect>::is_read>(sockfd, timeout_ms);
    if (0 == return_code) {
      socklen_t optlen = sizeof(return_code);
      if (::getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &return_code, &optlen) < 0)
        throw boson::exception(std::string("boson::connect getsockopt failed (") + ::strerror(errno) + ")");
      if (return_code != 0) {
        errno = return_code;
        return_code = -1;
      }
    }
  }
  return return_code;
}

int close(fd_t fd) {
  current_thread()->engine_proxy_.get_engine().event_loop().signal_fd_closed(fd);
  int rc = syscall_callable<SYS_close>::call(fd);
  auto current_errno = errno;
  //current_thread()->unregister_fd(fd);
  errno = current_errno;
  return rc;
}

}  // namespace boson
