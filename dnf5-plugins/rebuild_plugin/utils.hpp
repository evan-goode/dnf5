// Copyright Contributors to the DNF5 project.
// SPDX-License-Identifier: LGPL-2.1-or-later

#ifndef DNF5_PLUGINS_REBUILD_PLUGIN_UTILS_HPP
#define DNF5_PLUGINS_REBUILD_PLUGIN_UTILS_HPP

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace dnf5::rebuild::utils {

const std::filesystem::path temp_dir_name{"tmp"};

/// Return a copy of `str` with leading and trailing whitespace removed.
inline std::string trim(const std::string & str) {
    const auto begin = std::find_if(str.begin(), str.end(), [](unsigned char c) { return std::isspace(c) == 0; });
    const auto end =
        std::find_if(str.rbegin(), str.rend(), [](unsigned char c) { return std::isspace(c) == 0; }).base();
    return (begin < end) ? std::string(begin, end) : std::string();
}

/// Split `str` with a `delimiter` into a vector of strings.
/// The `limit` argument determines maximum number of elements in the resulting vector.
inline std::vector<std::string> split(
    const std::string & str, const std::string & delimiter, std::size_t limit = std::string::npos) {
    std::vector<std::string> result;

    if (str.empty()) {
        result.emplace_back("");
        return result;
    }

    if (limit < 2) {
        result.emplace_back(str);
        return result;
    }

    const auto delim_length = delimiter.size();

    std::size_t tokens_count = 1;
    for (auto pos = str.find(delimiter); pos != std::string::npos; pos = str.find(delimiter, pos + delim_length)) {
        ++tokens_count;
    }

    if (tokens_count > limit) {
        tokens_count = limit;
    }

    result.reserve(tokens_count);

    auto delim_pos = str.find(delimiter);
    std::size_t token_pos = 0;
    while (--tokens_count != 0) {
        result.emplace_back(str, token_pos, delim_pos - token_pos);
        token_pos = delim_pos + delim_length;
        delim_pos = str.find(delimiter, token_pos);
    }
    result.emplace_back(str, token_pos);

    return result;
}

/// Join elements from the `input` container with a `delimiter` string
template <typename ContainerT>
inline std::string join(const ContainerT & input, const std::string & delimiter) {
    auto it = std::begin(input);
    auto it_end = std::end(input);

    std::string result;

    if (it == it_end) {
        return result;
    }

    // Append first element
    result.append(*it);
    ++it;

    for (; it != it_end; ++it) {
        result.append(delimiter);
        result.append(*it);
    }

    return result;
}

inline std::string byte_vector_to_string(const std::vector<std::byte> & vec) {
    return {reinterpret_cast<const char *>(vec.data()), vec.size()};
}

inline std::vector<std::byte> string_to_byte_vector(const std::string & string) {
    return {
        reinterpret_cast<const std::byte *>(string.data()),
        reinterpret_cast<const std::byte *>(string.data() + string.size())};
}

/// Escape a string for safe interpolation into a shell script.
/// The result is wrapped in single quotes.
inline std::string shell_escape(const std::string & s) {
    std::string result = R"(')";
    for (char c : s) {
        if (c == '\'') {
            result += R"('\'')";
        } else {
            result += c;
        }
    }
    result += R"(')";
    return result;
}


}  // namespace dnf5::rebuild::utils

#endif  // DNF5_PLUGINS_REBUILD_PLUGIN_UTILS_HPP
