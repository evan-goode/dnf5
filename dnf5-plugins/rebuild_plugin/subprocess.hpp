// Copyright Contributors to the DNF5 project
// SPDX-License-Identifier: LGPL-2.1-or-later

#ifndef DNF5_REBUILD_SUBPROCESS_HPP
#define DNF5_REBUILD_SUBPROCESS_HPP

#include <optional>
#include <string>
#include <vector>

// Extended from libdnf5::utils::subprocess::run from libdnf5 private API
namespace dnf5::rebuild::subprocess {

/// @brief Controls how a child process's stdout or stderr stream is handled.
enum class SubprocessRedirect {
    PIPE,     ///< Capture the stream into CompletedProcess (default, like Python's subprocess.PIPE).
    INHERIT,  ///< Child inherits the parent's file descriptor, no capture.
    DEVNULL,  ///< Redirect to /dev/null, no capture.
    TEE,      ///< Capture into CompletedProcess AND simultaneously write to the parent's file descriptor.
};

struct CompletedProcess {
    int returncode;
    std::optional<std::vector<std::byte>> stdout;
    std::optional<std::vector<std::byte>> stderr;
};

/// @brief Run a command in a child process and wait for it to complete.
///
/// @param command Command to run. See `man 3 execvp`.
/// @param args Arguments to pass. argv[0] must be the first element of `args`.
/// @param input Data to write to the child's stdin. If empty, stdin is redirected to /dev/null.
/// @param stdout_redirect How to handle the child's stdout (default: PIPE).
/// @param stderr_redirect How to handle the child's stderr (default: PIPE).
/// @returns stdout, stderr, and return code. A negative return code `-N` indicates that the child was terminated by signal `N`.
///          For INHERIT and DEVNULL modes, the corresponding output will be std::nullopt.
/// @throws libdnf5::SystemError if any unexpected error occurred while forking, creating pipes, etc. No error is thrown if the command exits with nonzero code.
/// @throws libdnf5::RuntimeError if the child process terminated without a return code or signal (unreachable?)
CompletedProcess run(
    const std::string & command,
    const std::vector<std::string> & args,
    const std::vector<std::byte> & input = {},
    SubprocessRedirect stdout_redirect = SubprocessRedirect::PIPE,
    SubprocessRedirect stderr_redirect = SubprocessRedirect::PIPE);

}  // namespace dnf5::rebuild::subprocess

#endif  // DNF5_REBUILD_SUBPROCESS_HPP
