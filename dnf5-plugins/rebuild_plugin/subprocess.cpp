// Copyright Contributors to the DNF5 project
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "subprocess.hpp"

#include "utils.hpp"

#include <fcntl.h>
#include <libdnf5/common/exception.hpp>
#include <libdnf5/utils/bgettext/bgettext-lib.h>
#include <libdnf5/utils/bgettext/bgettext-mark-domain.h>
#include <poll.h>
#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

/// The class template OnScopeExit is a general-purpose scope guard
/// intended to call its exit function when a scope is exited.
template <typename TExitFunction>
    requires requires(TExitFunction f) {
        { f() } noexcept;
    }
class OnScopeExit {
public:
    OnScopeExit(TExitFunction && function) noexcept : exit_function{std::move(function)} {}

    ~OnScopeExit() noexcept { exit_function(); }

    OnScopeExit(const OnScopeExit &) = delete;
    OnScopeExit(OnScopeExit &&) = delete;
    OnScopeExit & operator=(const OnScopeExit &) = delete;
    OnScopeExit & operator=(OnScopeExit &&) = delete;

private:
    TExitFunction exit_function;
};

class Pipe {
public:
    Pipe() {
        if (pipe2(fds, O_CLOEXEC) == -1) {
            throw libdnf5::SystemError(errno, M_("cannot create pipe"));
        }
    }

    Pipe(const Pipe &) = delete;
    Pipe & operator=(const Pipe &) = delete;

    Pipe(Pipe && other) noexcept { *this = std::move(other); }

    Pipe & operator=(Pipe && pipe) noexcept {
        if (this != &pipe) {
            fds[PipeEnd::READ] = pipe.fds[PipeEnd::READ];
            fds[PipeEnd::WRITE] = pipe.fds[PipeEnd::WRITE];
            pipe.fds[PipeEnd::READ] = -1;
            pipe.fds[PipeEnd::WRITE] = -1;
        }
        return *this;
    }

    int get_in() const noexcept { return fds[PipeEnd::READ]; }
    int get_out() const noexcept { return fds[PipeEnd::WRITE]; }

    void close_in() noexcept { close(PipeEnd::READ); }
    void close_out() noexcept { close(PipeEnd::WRITE); }

    ~Pipe() {
        close_in();
        close_out();
    }

private:
    enum PipeEnd { READ = 0, WRITE = 1 };

    void close(int fd_idx) noexcept {
        if (fds[fd_idx] != -1) {
            ::close(fds[fd_idx]);
            fds[fd_idx] = -1;
        }
    }

    int fds[2];
};

/// @brief Helper to check whether a redirect mode needs a pipe (parent reads from child).
bool needs_pipe(dnf5::rebuild::subprocess::SubprocessRedirect mode) {
    return mode == dnf5::rebuild::subprocess::SubprocessRedirect::PIPE ||
           mode == dnf5::rebuild::subprocess::SubprocessRedirect::TEE;
}

/// @brief Redirect a child fd to /dev/null. Reports error via error pipe and calls _exit on failure.
void child_redirect_devnull(int target_fd, int open_flags, int error_pipe_fd, int error_code) {
    const int dev_null_fd = open("/dev/null", open_flags);
    if (dev_null_fd == -1) {
        struct {
            int error;
            int err_code;
        } msg{error_code, errno};
        if (write(error_pipe_fd, &msg, sizeof(msg)) != sizeof(msg)) {
        }
        _exit(255);
    }
    if (dup2(dev_null_fd, target_fd) == -1) {
        struct {
            int error;
            int err_code;
        } msg{error_code, errno};
        if (write(error_pipe_fd, &msg, sizeof(msg)) != sizeof(msg)) {
        }
        _exit(255);
    }
    ::close(dev_null_fd);
}

/// @brief Write all bytes to a file descriptor, retrying on EINTR.
void write_all(int fd, const char * data, std::size_t len) {
    while (len > 0) {
        const ssize_t written = write(fd, data, len);
        if (written == -1) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        data += written;
        len -= static_cast<std::size_t>(written);
    }
}

}  // anonymous namespace

namespace dnf5::rebuild::subprocess {

CompletedProcess run(
    const std::string & command,
    const std::vector<std::string> & args,
    const std::vector<std::byte> & input,
    SubprocessRedirect stdout_redirect,
    SubprocessRedirect stderr_redirect) {
    // Struct is used to pass a possible error from a child process before starting a new program.
    struct ErrorMessage {
        enum { BIND_STDIN, BIND_STDOUT, BIND_STDERR, EXEC } error;  // what failed
        int err_code;                                               // errno
    };

    Pipe pipe_error_msg_from_child;
    Pipe pipe_in_to_child;

    // Only create stdout/stderr pipes when we need to read from the child (PIPE or TEE).
    // For INHERIT and DEVNULL, no pipe is needed.
    std::optional<Pipe> pipe_out_from_child;
    std::optional<Pipe> pipe_err_from_child;
    if (needs_pipe(stdout_redirect)) {
        pipe_out_from_child.emplace();
    }
    if (needs_pipe(stderr_redirect)) {
        pipe_err_from_child.emplace();
    }

    // Prepare a null-terminated array of arguments for the exec procedure.
    // We don't want to risk throwing an exception in the child process, so we prepare it here.
    std::vector<char *> c_args;
    c_args.reserve(args.size() + 1);
    for (const auto & arg : args) {
        c_args.push_back(const_cast<char *>(arg.data()));
    }
    c_args.push_back(nullptr);

    const auto child_pid = fork();
    if (child_pid == -1) {
        throw libdnf5::SystemError(errno, M_("cannot fork"));
    }

    int child_exit_status;
    std::optional<std::vector<std::byte>> stdout_bytes;
    std::optional<std::vector<std::byte>> stderr_bytes;
    if (needs_pipe(stdout_redirect)) {
        stdout_bytes.emplace();
    }
    if (needs_pipe(stderr_redirect)) {
        stderr_bytes.emplace();
    }

    if (child_pid == 0) {
        // === Child process ===
        pipe_error_msg_from_child.close_in();
        pipe_in_to_child.close_out();
        if (pipe_out_from_child) {
            pipe_out_from_child->close_in();
        }
        if (pipe_err_from_child) {
            pipe_err_from_child->close_in();
        }

        // --- stdin ---
        if (input.empty()) {
            // redirect stdin to /dev/null so the child doesn't inherit parent's stdin
            child_redirect_devnull(
                fileno(stdin), O_RDONLY, pipe_error_msg_from_child.get_out(), ErrorMessage::BIND_STDIN);
        } else {
            // bind stdin of the child process to the reading end of the pipe
            if (dup2(pipe_in_to_child.get_in(), fileno(stdin)) == -1) {
                ErrorMessage msg{ErrorMessage::BIND_STDIN, errno};
                if (write(pipe_error_msg_from_child.get_out(), &msg, sizeof(msg)) != sizeof(msg)) {
                }
                _exit(255);
            }
            pipe_in_to_child.close_in();
        }

        // --- stdout ---
        switch (stdout_redirect) {
            case SubprocessRedirect::PIPE:
            case SubprocessRedirect::TEE:
                // bind stdout of the child process to the writing end of the pipe
                if (dup2(pipe_out_from_child->get_out(), fileno(stdout)) == -1) {
                    ErrorMessage msg{ErrorMessage::BIND_STDOUT, errno};
                    if (write(pipe_error_msg_from_child.get_out(), &msg, sizeof(msg)) != sizeof(msg)) {
                    }
                    _exit(255);
                }
                pipe_out_from_child->close_out();
                break;
            case SubprocessRedirect::DEVNULL:
                child_redirect_devnull(
                    fileno(stdout), O_WRONLY, pipe_error_msg_from_child.get_out(), ErrorMessage::BIND_STDOUT);
                break;
            case SubprocessRedirect::INHERIT:
                // Child inherits parent's stdout fd — nothing to do.
                break;
        }

        // --- stderr ---
        switch (stderr_redirect) {
            case SubprocessRedirect::PIPE:
            case SubprocessRedirect::TEE:
                // bind stderr of the child process to the writing end of the pipe
                if (dup2(pipe_err_from_child->get_out(), fileno(stderr)) == -1) {
                    ErrorMessage msg{ErrorMessage::BIND_STDERR, errno};
                    if (write(pipe_error_msg_from_child.get_out(), &msg, sizeof(msg)) != sizeof(msg)) {
                    }
                    _exit(255);
                }
                pipe_err_from_child->close_out();
                break;
            case SubprocessRedirect::DEVNULL:
                child_redirect_devnull(
                    fileno(stderr), O_WRONLY, pipe_error_msg_from_child.get_out(), ErrorMessage::BIND_STDERR);
                break;
            case SubprocessRedirect::INHERIT:
                // Child inherits parent's stderr fd — nothing to do.
                break;
        }

        execvp(command.c_str(), c_args.data());  // replace the child process with the command
        ErrorMessage msg{ErrorMessage::EXEC, errno};
        if (write(pipe_error_msg_from_child.get_out(), &msg, sizeof(msg)) != sizeof(msg)) {
        }
        _exit(255);
    } else {
        // === Parent process ===
        OnScopeExit finish([&pipe_error_msg_from_child,
                            &pipe_in_to_child,
                            &pipe_out_from_child,
                            &pipe_err_from_child,
                            child_pid,
                            &child_exit_status]() noexcept {
            pipe_error_msg_from_child.close_in();
            pipe_in_to_child.close_out();
            if (pipe_out_from_child) {
                pipe_out_from_child->close_in();
            }
            if (pipe_err_from_child) {
                pipe_err_from_child->close_in();
            }
            waitpid(child_pid, &child_exit_status, 0);
        });

        pipe_error_msg_from_child.close_out();
        pipe_in_to_child.close_in();
        if (pipe_out_from_child) {
            pipe_out_from_child->close_out();
        }
        if (pipe_err_from_child) {
            pipe_err_from_child->close_out();
        }

        // Check the pipe for errors. The child process will close it empty or write an error.
        ErrorMessage err_msg;
        const auto ret = read(pipe_error_msg_from_child.get_in(), &err_msg, sizeof(err_msg));
        if (ret == sizeof(err_msg)) {
            switch (err_msg.error) {
                case ErrorMessage::BIND_STDIN:
                    throw libdnf5::SystemError(err_msg.err_code, M_("cannot bind child stdin"));
                case ErrorMessage::BIND_STDOUT:
                    throw libdnf5::SystemError(err_msg.err_code, M_("cannot bind child stdout"));
                case ErrorMessage::BIND_STDERR:
                    throw libdnf5::SystemError(err_msg.err_code, M_("cannot bind child stderr"));
                case ErrorMessage::EXEC:
                    throw libdnf5::SystemError(
                        err_msg.err_code, M_("failed to execute command: {}"), dnf5::rebuild::utils::join(args, " "));
            }
        } else if (ret != 0) {
            throw libdnf5::SystemError(
                err_msg.err_code,
                M_("error during preparation of child process: {}"),
                dnf5::rebuild::utils::join(args, " "));
        }
        pipe_error_msg_from_child.close_in();

        // Write input data to the child's stdin and close the pipe
        if (!input.empty()) {
            write_all(pipe_in_to_child.get_out(), reinterpret_cast<const char *>(input.data()), input.size());
        }
        pipe_in_to_child.close_out();

        // Use poll() to multiplex I/O operations: read from stdout and stderr concurrently.
        // This prevents issues when the child blocks on writing to one stream while we're reading the other.
        // For INHERIT and DEVNULL modes, we have no pipe to poll on that stream.

        constexpr int STDOUT_FD = 0;
        constexpr int STDERR_FD = 1;

        std::array<pollfd, 2> fds{};
        fds[STDOUT_FD] = {
            .fd = pipe_out_from_child ? pipe_out_from_child->get_in() : -1, .events = POLLIN, .revents = 0};
        fds[STDERR_FD] = {
            .fd = pipe_err_from_child ? pipe_err_from_child->get_in() : -1, .events = POLLIN, .revents = 0};

        char buffer[4096];

        // Continue polling while we have active file descriptors
        while (fds[STDOUT_FD].fd != -1 || fds[STDERR_FD].fd != -1) {
            const int poll_result = poll(fds.data(), fds.size(), -1);  // -1 = wait indefinitely

            if (poll_result == -1) {
                if (errno == EINTR) {
                    continue;  // Interrupted by signal, retry
                }
                throw libdnf5::SystemError(errno, M_("poll failed during subprocess I/O"));
            }

            if (fds[STDOUT_FD].revents & (POLLIN | POLLHUP)) {
                const ssize_t bytes_read = read(fds[STDOUT_FD].fd, buffer, sizeof(buffer));
                if (bytes_read > 0) {
                    stdout_bytes->insert(
                        stdout_bytes->end(),
                        reinterpret_cast<std::byte *>(buffer),
                        reinterpret_cast<std::byte *>(buffer + bytes_read));
                    if (stdout_redirect == SubprocessRedirect::TEE) {
                        write_all(STDOUT_FILENO, buffer, static_cast<std::size_t>(bytes_read));
                    }
                } else if (bytes_read == 0) {
                    fds[STDOUT_FD].fd = -1;
                } else if (errno != EINTR) {
                    fds[STDOUT_FD].fd = -1;
                }
            }

            if (fds[STDERR_FD].revents & (POLLIN | POLLHUP)) {
                const ssize_t bytes_read = read(fds[STDERR_FD].fd, buffer, sizeof(buffer));
                if (bytes_read > 0) {
                    stderr_bytes->insert(
                        stderr_bytes->end(),
                        reinterpret_cast<std::byte *>(buffer),
                        reinterpret_cast<std::byte *>(buffer + bytes_read));
                    if (stderr_redirect == SubprocessRedirect::TEE) {
                        write_all(STDERR_FILENO, buffer, static_cast<std::size_t>(bytes_read));
                    }
                } else if (bytes_read == 0) {
                    fds[STDERR_FD].fd = -1;
                } else if (errno != EINTR) {
                    fds[STDERR_FD].fd = -1;
                }
            }

            if (fds[STDOUT_FD].revents & POLLERR) {
                fds[STDOUT_FD].fd = -1;
            }
            if (fds[STDERR_FD].revents & POLLERR) {
                fds[STDERR_FD].fd = -1;
            }
        }
    }

    int returncode;
    if (WIFEXITED(child_exit_status)) {
        returncode = WEXITSTATUS(child_exit_status);
    } else if (WIFSIGNALED(child_exit_status)) {
        returncode = -WTERMSIG(child_exit_status);
    } else {
        throw libdnf5::RuntimeError(M_("unexpected child process status after waitpid"));
    }

    return CompletedProcess{
        .returncode = returncode,
        .stdout = std::move(stdout_bytes),
        .stderr = std::move(stderr_bytes),
    };
}

}  // namespace dnf5::rebuild::subprocess
