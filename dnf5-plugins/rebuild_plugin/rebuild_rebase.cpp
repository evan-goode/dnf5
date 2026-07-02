// Copyright Contributors to the DNF5 project.
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "rebuild.hpp"

#include <dnf5/context.hpp>
#include <libdnf5/utils/bgettext/bgettext-lib.h>
#include <libdnf5/utils/bgettext/bgettext-mark-domain.h>
#include <libdnf5/utils/fs/file.hpp>

using namespace libdnf5::cli;

namespace dnf5 {

void RebuildRebaseCommand::set_argument_parser() {
    RebuildSubcommand::set_argument_parser();

    auto & parser = get_context().get_argument_parser();
    auto & cmd = *get_argument_parser_command();

    cmd.set_description(_("Rebase to a new container image"));

    auto * image_arg = parser.add_new_positional_arg("image", 1, nullptr, nullptr);
    image_arg->set_description(_("New base image reference (e.g. quay.io/fedora/fedora-bootc:rawhide)"));
    image_arg->set_parse_hook_func(
        [this](
            [[maybe_unused]] libdnf5::cli::ArgumentParser::PositionalArg * arg, int argc, const char * const argv[]) {
            if (argc == 1) {
                image_spec = argv[0];
            }
            return true;
        });
    cmd.register_positional_arg(image_arg);

    switch_option = create_switch_option(*this);
    tag_option = create_tag_option(*this);
    apply_option = create_apply_option(*this);
    soft_reboot_option = create_soft_reboot_option(*this);
}

void RebuildRebaseCommand::pre_configure() {
    auto & ctx = get_context();

    ctx.set_create_repos(false);
    ctx.set_load_system_repo(false);
    ctx.set_load_available_repos(Context::LoadAvailableRepos::NONE);
}

void RebuildRebaseCommand::configure() {
    validate_conf_dir();
}

void RebuildRebaseCommand::run() {
    auto & ctx = get_context();
    const std::filesystem::path conf_dir{config_directory_option->get_value()};

    // If the image spec includes a digest, use it directly; otherwise fetch the latest
    const auto at_pos = image_spec.find('@');
    std::string new_base;
    if (at_pos != std::string::npos) {
        new_base = image_spec;
    } else {
        new_base = fmt::format("{}@{}", image_spec, get_latest_digest(image_spec));
    }
    ctx.print_info(libdnf5::utils::sformat(_("Rebasing on {}"), new_base));
    libdnf5::utils::fs::File(conf_dir / "base", "w").write(new_base);

    if (switch_option->get_value()) {
        const auto & tag = build(ctx, conf_dir, tag_option->get_value());
        bootc_switch(tag, apply_option->get_value(), soft_reboot_option->get_value());
    }
}

}  // namespace dnf5
