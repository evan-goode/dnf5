// Copyright Contributors to the DNF5 project.
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "rebuild.hpp"

#include <libdnf5/rpm/package_query.hpp>
#include <libdnf5/utils/bgettext/bgettext-lib.h>
#include <libdnf5/utils/bgettext/bgettext-mark-domain.h>
#include <libdnf5/utils/fs/file.hpp>

using namespace libdnf5::cli;

namespace dnf5 {

void RebuildInitCommand::set_argument_parser() {
    RebuildSubcommand::set_argument_parser();

    auto & cmd = *get_argument_parser_command();

    cmd.set_description(_("Create a blank local layer definition based on staged or booted bootc image spec"));
}

void RebuildInitCommand::configure() {}

// Get bootc status — pull image reference from status.staged, falling back to status.booted
// Returns (image, digest)
std::tuple<std::string, std::string> get_deployed_image() {
    const auto & bootc_status = get_bootc_status();
    json_object * status_obj{nullptr};
    if (!json_object_object_get_ex(bootc_status.get(), "status", &status_obj)) {
        throw libdnf5::cli::CommandExitError(1, M_("Unable to parse bootc status: status not found"));
    }
    json_object * deployment_obj{nullptr};
    std::string deployment_name{"staged"};
    if (!json_object_object_get_ex(status_obj, "staged", &deployment_obj) || !deployment_obj) {
        deployment_name = "booted";
        if (!json_object_object_get_ex(status_obj, "booted", &deployment_obj) || !deployment_obj) {
            throw libdnf5::cli::CommandExitError(
                1, M_("Unable to parse bootc status: found neither status.staged nor status.booted"));
        }
    }
    json_object * deployment_image_obj{nullptr};
    if (!json_object_object_get_ex(deployment_obj, "image", &deployment_image_obj)) {
        throw libdnf5::cli::CommandExitError(
            1, M_("Unable to parse bootc status: {}.image not found"), deployment_name);
    }

    // Get transport
    json_object * deployment_image_image_obj{nullptr};
    if (!json_object_object_get_ex(deployment_image_obj, "image", &deployment_image_image_obj)) {
        throw libdnf5::cli::CommandExitError(
            1, M_("Unable to parse bootc status: {}.image.image not found"), deployment_name);
    }
    json_object * transport_obj{nullptr};
    if (!json_object_object_get_ex(deployment_image_image_obj, "transport", &transport_obj)) {
        throw libdnf5::cli::CommandExitError(1, M_("{}.image.image.transport not found"), deployment_name);
    }
    const std::string transport{json_object_get_string(transport_obj)};

    if (transport != "registry") {
        throw libdnf5::cli::CommandExitError(1, M_("bootc image transport type must be \"registry\""));
    }

    // Get image reference
    json_object * image_ref_obj{nullptr};
    if (!json_object_object_get_ex(deployment_image_image_obj, "image", &image_ref_obj)) {
        throw libdnf5::cli::CommandExitError(1, M_("{}.image.image.image not found"), deployment_name);
    }
    const std::string image{json_object_get_string(image_ref_obj)};

    // Get digest
    json_object * image_digest_obj{nullptr};
    if (!json_object_object_get_ex(deployment_image_obj, "imageDigest", &image_digest_obj)) {
        throw libdnf5::cli::CommandExitError(1, M_("{}.image.imageDigest not found"), deployment_name);
    }
    const std::string digest{json_object_get_string(image_digest_obj)};

    return {image, digest};
}

void RebuildInitCommand::run() {
    auto & ctx = get_context();

    const std::filesystem::path conf_dir{config_directory_option->get_value()};

    if (std::filesystem::exists(conf_dir)) {
        if (std::filesystem::is_directory(conf_dir)) {
            if (!std::filesystem::is_empty(conf_dir)) {
                throw libdnf5::cli::CommandExitError(
                    1, M_("Configuration directory {} exists but is not empty."), conf_dir.string());
            }
        } else {
            throw libdnf5::cli::CommandExitError(1, M_("Path {} exists but is not a directory."), conf_dir.string());
        }
    }
    std::filesystem::create_directories(conf_dir);

    // Create base
    const auto & [image, digest] = get_deployed_image();
    const auto & base_path = conf_dir / "base";
    libdnf5::utils::fs::File(base_path, "w").write(fmt::format("{}@{}", image, digest));

    // Create blank libpkgmanifest infile
    const auto & infile_path = conf_dir / "packages.input.yaml";
    const auto & arch = ctx.get_base().get_vars()->get_value("arch");
    const auto & infile = fmt::format(
        R"""(document: rpm-package-input
version: 0.0.2
repositories:
packages:
    install:
archs:
    - {}
)""",
        arch);
    libdnf5::utils::fs::File(infile_path, "w").write(infile);

    // Create template Containerfile.pre, Containerfile.post
    const auto & containerfile_pre_path = conf_dir / "Containerfile.pre";
    libdnf5::utils::fs::File(containerfile_pre_path, "w")
        .write("# Steps in this file will execute before the DNF transaction.");
    const auto & containerfile_post_path = conf_dir / "Containerfile.post";
    libdnf5::utils::fs::File(containerfile_post_path, "w")
        .write("# Steps in this file will execute after the DNF transaction.");

    ctx.print_info(fmt::format("Created empty template at {} from image {}@{}", conf_dir.string(), image, digest));
}

}  // namespace dnf5
