// Copyright Contributors to the DNF5 project.
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "rebuild.hpp"

#include <dnf5/context.hpp>
#include <libdnf5/utils/bgettext/bgettext-lib.h>
#include <libdnf5/utils/bgettext/bgettext-mark-domain.h>
#include <libdnf5/utils/fs/file.hpp>

using namespace libdnf5::cli;

namespace dnf5 {

void RebuildUpgradeCommand::set_argument_parser() {
    RebuildSwitchableCommand::set_argument_parser();
    get_argument_parser_command()->set_description(_("Upgrade the base image to the latest digest from the registry"));
}

void RebuildUpgradeCommand::run() {
    auto & ctx = get_context();
    const std::filesystem::path conf_dir{config_directory_option->get_value()};

    // Upgrade the base image digest to the latest from the registry
    auto base_image = get_base_image_id(conf_dir);
    const auto at_pos = base_image.find('@');
    const auto image_without_digest = (at_pos != std::string::npos) ? base_image.substr(0, at_pos) : base_image;
    const auto digest = get_latest_digest(image_without_digest);
    const auto & new_base_image = fmt::format("{}@{}", image_without_digest, digest);

    if (new_base_image == base_image) {
        ctx.print_info(_("Base image is up to date"));
    } else {
        libdnf5::utils::fs::File(conf_dir / "base", "w").write(new_base_image);
        ctx.print_info(libdnf5::utils::sformat(_("Base image updated to {}"), new_base_image));
    }

    build_and_switch_if_needed();
}

}  // namespace dnf5
