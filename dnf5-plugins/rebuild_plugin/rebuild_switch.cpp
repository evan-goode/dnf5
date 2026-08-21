// Copyright Contributors to the DNF5 project.
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "rebuild.hpp"
#include "subprocess.hpp"
#include "utils.hpp"

#include <dnf5/context.hpp>
#include <libdnf5/utils/bgettext/bgettext-lib.h>
#include <libdnf5/utils/bgettext/bgettext-mark-domain.h>

using namespace libdnf5::cli;
using namespace dnf5::rebuild::utils;

namespace dnf5 {

void RebuildSwitchCommand::set_argument_parser() {
    RebuildSubcommand::set_argument_parser();
    get_argument_parser_command()->set_description(_("Build a container image with layered packages and switch to it"));
    tag_option = create_tag_option(*this);
    apply_option = create_apply_option(*this);
    soft_reboot_option = create_soft_reboot_option(*this);
}

void RebuildSwitchCommand::pre_configure() {
    auto & ctx = get_context();

    ctx.set_create_repos(false);
    ctx.set_load_system_repo(false);
    ctx.set_load_available_repos(Context::LoadAvailableRepos::NONE);
}

void RebuildSwitchCommand::configure() {
    validate_conf_dir();
}

void bootc_switch(const std::string & tag, bool apply, const std::string & soft_reboot) {
    std::vector<std::string> bootc_args{"bootc", "switch", "--transport", "containers-storage"};
    if (apply) {
        bootc_args.emplace_back("--apply");
    }
    if (!soft_reboot.empty()) {
        bootc_args.emplace_back(fmt::format("--soft-reboot={}", soft_reboot));
    }
    bootc_args.push_back(tag);

    using dnf5::rebuild::subprocess::SubprocessRedirect;
    const auto & switch_result =
        dnf5::rebuild::subprocess::run("bootc", bootc_args, {}, SubprocessRedirect::TEE, SubprocessRedirect::TEE);
    if (switch_result.returncode != 0) {
        throw libdnf5::cli::CommandExitError(
            1, M_("Failed to switch to image {}: {}"), tag, byte_vector_to_string(*switch_result.stderr));
    }
}

void RebuildSwitchCommand::run() {
    auto & ctx = get_context();
    const std::filesystem::path conf_dir{config_directory_option->get_value()};
    const auto & tag = build(ctx, conf_dir, tag_option->get_value());
    bootc_switch(tag, apply_option->get_value(), soft_reboot_option->get_value());
}

}  // namespace dnf5
