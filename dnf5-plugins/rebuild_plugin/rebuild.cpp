// Copyright Contributors to the DNF5 project.
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "rebuild.hpp"

#include "subprocess.hpp"
#include "utils.hpp"

#include <dnf5/context.hpp>
#include <json.h>
#include <libdnf5-cli/utils/userconfirm.hpp>
#include <libdnf5/logger/logger.hpp>
#include <libdnf5/repo/repo_errors.hpp>
#include <libdnf5/repo/repo_sack.hpp>
#include <libdnf5/rpm/package_query.hpp>
#include <libdnf5/rpm/rpm_signature.hpp>
#include <libdnf5/utils/bgettext/bgettext-lib.h>
#include <libdnf5/utils/bgettext/bgettext-mark-domain.h>
#include <libdnf5/utils/fs/file.hpp>
#include <libpkgmanifest/manifest/parser.hpp>

#include <string>

using namespace libdnf5::cli;
using namespace dnf5::rebuild::utils;

namespace dnf5 {

std::string get_base_image_id(const std::filesystem::path & conf_dir) {
    const auto & base_path = conf_dir / "base";
    const auto & base_image = trim(libdnf5::utils::fs::File{base_path, "r"}.read());
    return base_image;
}

std::string build_containerfile_if_exists(
    const std::filesystem::path & conf_dir,
    const std::string & from_image_id,
    const std::filesystem::path & containerfile_path) {
    std::string containerfile_contents = fmt::format("FROM {}\n", from_image_id);
    try {
        containerfile_contents += libdnf5::utils::fs::File{containerfile_path, "r"}.read();
    } catch (const libdnf5::FileSystemError & ex) {
        if (ex.get_error_code() == ENOENT) {
            return from_image_id;
        }
        throw;
    }

    using dnf5::rebuild::subprocess::SubprocessRedirect;
    const auto & result = dnf5::rebuild::subprocess::run(
        "podman",
        {"podman", "build", "--file", "-", conf_dir},
        string_to_byte_vector(containerfile_contents),
        SubprocessRedirect::TEE,
        SubprocessRedirect::TEE);

    if (result.returncode != 0) {
        throw libdnf5::RuntimeError(
            M_("Failed to build {}: {}"), containerfile_path.string(), byte_vector_to_string(*result.stderr));
    }

    const auto lines = split(byte_vector_to_string(*result.stdout), "\n");
    const auto it = std::find_if(lines.rbegin(), lines.rend(), [](const std::string & s) { return !s.empty(); });

    if (it == lines.rend()) {
        throw libdnf5::RuntimeError(
            M_("Could not build image from {}: {}"),
            containerfile_path.string(),
            byte_vector_to_string(*result.stderr));
    }

    return *it;
}


std::unique_ptr<json_object, decltype(&json_object_put)> get_bootc_status() {
    const auto & result = dnf5::rebuild::subprocess::run("bootc", {"bootc", "status", "--format=json"});

    // Check whether command failed (non-zero exit code)
    if (result.returncode != 0) {
        // Command ran but failed; show stderr to user
        std::string stderr_content(reinterpret_cast<const char *>(result.stderr->data()), result.stderr->size());
        if (!stderr_content.empty()) {
            throw libdnf5::RuntimeError(M_("Error checking bootc status: {}"), stderr_content);
        } else {
            throw libdnf5::RuntimeError(
                M_("Error checking bootc status: bootc command failed with exit code {}"), result.returncode);
        }
    }

    std::string stdout_content(reinterpret_cast<const char *>(result.stdout->data()), result.stdout->size());
    std::unique_ptr<json_object, decltype(&json_object_put)> root(
        json_tokener_parse(stdout_content.c_str()), json_object_put);

    if (!root) {
        throw libdnf5::RuntimeError(M_("Error checking bootc status: Failed to parse JSON output"));
    }

    return root;
}

std::string get_latest_digest(const std::string & image) {
    using dnf5::rebuild::subprocess::SubprocessRedirect;
    const auto & result = dnf5::rebuild::subprocess::run(
        "skopeo", {"skopeo", "inspect", "--format", "{{.Digest}}", fmt::format("docker://{}", image)});
    if (result.returncode != 0) {
        throw libdnf5::RuntimeError(
            M_("Failed to get latest digest for {}: {}"),
            image,
            dnf5::rebuild::utils::byte_vector_to_string(*result.stderr));
    }

    const auto digest = dnf5::rebuild::utils::trim(dnf5::rebuild::utils::byte_vector_to_string(*result.stdout));
    if (digest.empty()) {
        throw libdnf5::RuntimeError(M_("Skopeo returned empty digest for {}"), image);
    }
    return digest;
}

void RebuildCommand::pre_configure() {
    throw_missing_command();
}

void RebuildCommand::set_parent_command() {
    auto * parent_cmd = get_session().get_argument_parser().get_root_command();
    auto * this_cmd = get_argument_parser_command();
    parent_cmd->register_command(this_cmd);

    auto & group = parent_cmd->get_group("software_management_commands");
    group.register_argument(this_cmd);
}

void RebuildCommand::register_subcommands() {
    register_subcommand(std::make_unique<RebuildInitCommand>(get_context()));
    register_subcommand(std::make_unique<RebuildUpgradeCommand>(get_context()));
    register_subcommand(std::make_unique<RebuildRebaseCommand>(get_context()));
    register_subcommand(std::make_unique<RebuildInstallCommand>(get_context()));
    register_subcommand(std::make_unique<RebuildBuildCommand>(get_context()));
    register_subcommand(std::make_unique<RebuildSwitchCommand>(get_context()));
}

void RebuildCommand::set_argument_parser() {
    get_argument_parser_command()->set_description(_("Layer packages on bootc systems"));
}

void RebuildSubcommand::set_argument_parser() {
    auto & ctx = get_context();
    auto & parser = ctx.get_argument_parser();
    auto & cmd = *get_argument_parser_command();

    config_directory_option = dynamic_cast<libdnf5::OptionPath *>(
        parser.add_init_value(std::make_unique<libdnf5::OptionPath>(DEFAULT_REBUILD_CONFIG_DIRECTORY)));
    auto * config_directory_arg = parser.add_new_named_arg("config-directory");
    config_directory_arg->set_long_name("config-directory");
    config_directory_arg->set_description(_("Path to rebuild configuration directory"));
    config_directory_arg->set_has_value(true);
    config_directory_arg->link_value(config_directory_option);
    cmd.register_named_arg(config_directory_arg);
}

libdnf5::OptionString * create_tag_option(Command & command) {
    auto & parser = command.get_context().get_argument_parser();
    auto * option = dynamic_cast<libdnf5::OptionString *>(
        parser.add_init_value(std::make_unique<libdnf5::OptionString>(DEFAULT_REBUILD_IMAGE_TAG)));
    auto * arg = parser.add_new_named_arg("tag");
    arg->set_long_name("tag");
    arg->set_description(_("Tag to apply to the built image"));
    arg->set_has_value(true);
    arg->link_value(option);
    command.get_argument_parser_command()->register_named_arg(arg);
    return option;
}

libdnf5::OptionBool * create_apply_option(Command & command) {
    auto & parser = command.get_context().get_argument_parser();
    auto * option =
        dynamic_cast<libdnf5::OptionBool *>(parser.add_init_value(std::make_unique<libdnf5::OptionBool>(false)));
    auto * arg = parser.add_new_named_arg("apply");
    arg->set_long_name("apply");
    arg->set_description(_("Apply the changes immediately without rebooting"));
    arg->set_const_value("true");
    arg->link_value(option);
    command.get_argument_parser_command()->register_named_arg(arg);
    return option;
}

libdnf5::OptionBool * create_soft_reboot_option(Command & command) {
    auto & parser = command.get_context().get_argument_parser();
    auto * option =
        dynamic_cast<libdnf5::OptionBool *>(parser.add_init_value(std::make_unique<libdnf5::OptionBool>(false)));
    auto * arg = parser.add_new_named_arg("soft-reboot");
    arg->set_long_name("soft-reboot");
    arg->set_description(_("Perform a soft reboot after switching"));
    arg->set_const_value("true");
    arg->link_value(option);
    command.get_argument_parser_command()->register_named_arg(arg);
    return option;
}

libdnf5::OptionBool * create_switch_option(Command & command) {
    auto & parser = command.get_context().get_argument_parser();
    auto * option =
        dynamic_cast<libdnf5::OptionBool *>(parser.add_init_value(std::make_unique<libdnf5::OptionBool>(true)));

    auto * switch_arg = parser.add_new_named_arg("switch");
    switch_arg->set_long_name("switch");
    switch_arg->set_description(_("Build and switch to the image (default)"));
    switch_arg->set_const_value("true");
    switch_arg->link_value(option);
    command.get_argument_parser_command()->register_named_arg(switch_arg);

    auto * no_switch_arg = parser.add_new_named_arg("no-switch");
    no_switch_arg->set_long_name("no-switch");
    no_switch_arg->set_description(_("Do not build or switch"));
    no_switch_arg->set_const_value("false");
    no_switch_arg->link_value(option);
    command.get_argument_parser_command()->register_named_arg(no_switch_arg);

    return option;
}

void RebuildSubcommand::validate_conf_dir() {
    const std::filesystem::path conf_dir{config_directory_option->get_value()};
    if (!std::filesystem::exists(conf_dir)) {
        throw libdnf5::cli::CommandExitError(1, M_("Path {} does not exist."), conf_dir.string());
    }
    if (!std::filesystem::is_directory(conf_dir)) {
        throw libdnf5::cli::CommandExitError(1, M_("Path {} is not a directory."), conf_dir.string());
    }
    if (std::filesystem::is_empty(conf_dir)) {
        throw libdnf5::cli::CommandExitError(
            1, M_("Configuration directory {} exists but is empty."), conf_dir.string());
    }
}

}  // namespace dnf5
