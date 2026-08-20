// Copyright Contributors to the DNF5 project.
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "rebuild.hpp"

#include <dnf5/context.hpp>
#include <libdnf5/utils/bgettext/bgettext-lib.h>
#include <libdnf5/utils/bgettext/bgettext-mark-domain.h>
#include <libpkgmanifest/input/input.hpp>
#include <libpkgmanifest/input/parser.hpp>
#include <libpkgmanifest/input/serializer.hpp>

#include <algorithm>

using namespace libdnf5::cli;

namespace dnf5 {

void RebuildInstallCommand::set_argument_parser() {
    RebuildSwitchableCommand::set_argument_parser();

    auto & parser = get_context().get_argument_parser();
    auto & cmd = *get_argument_parser_command();

    cmd.set_description(_("Add packages to the local layer definition"));

    auto * pkgs_arg =
        parser.add_new_positional_arg("packages", ArgumentParser::PositionalArg::AT_LEAST_ONE, nullptr, nullptr);
    pkgs_arg->set_description(_("List of package specs to install"));
    pkgs_arg->set_parse_hook_func(
        [this]([[maybe_unused]] ArgumentParser::PositionalArg * arg, int argc, const char * const argv[]) {
            for (int i = 0; i < argc; ++i) {
                pkg_specs.emplace_back(argv[i]);
            }
            return true;
        });
    cmd.register_positional_arg(pkgs_arg);
}

void RebuildInstallCommand::run() {
    auto & ctx = get_context();
    const std::filesystem::path conf_dir{config_directory_option->get_value()};
    const auto infile_path = conf_dir / "packages.input.yaml";

    libpkgmanifest::input::Parser parser;
    auto input = parser.parse(infile_path.string());

    auto & installs = input.get_packages().get_installs();
    for (const auto & spec : pkg_specs) {
        if (std::find(installs.begin(), installs.end(), spec) != installs.end()) {
            ctx.print_info(
                libdnf5::utils::sformat(_("Package spec \"{}\" is already in the input file, skipping"), spec));
            continue;
        }
        installs.push_back(spec);
    }

    libpkgmanifest::input::Serializer serializer;
    serializer.serialize(input, infile_path.string());

    build_and_switch_if_needed();
}

}  // namespace dnf5
