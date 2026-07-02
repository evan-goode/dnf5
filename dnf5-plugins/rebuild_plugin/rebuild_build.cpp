// Copyright Contributors to the DNF5 project.
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "rebuild.hpp"
#include "subprocess.hpp"
#include "utils.hpp"

#include <dnf5/context.hpp>
#include <libdnf5/utils/bgettext/bgettext-lib.h>
#include <libdnf5/utils/bgettext/bgettext-mark-domain.h>
#include <libdnf5/utils/fs/file.hpp>
#include <libdnf5/utils/fs/temp.hpp>
#include <libpkgmanifest/common/exception.hpp>
#include <libpkgmanifest/manifest/parser.hpp>

#include <optional>
#include <set>

using namespace libdnf5::cli;
using namespace dnf5::rebuild::utils;

namespace dnf5 {

std::string build(Context & ctx, const std::filesystem::path & conf_dir, const std::string & tag) {
    const auto & cachedir = ctx.get_base().get_config().get_cachedir_option().get_value();
    const auto & rebuild_cachedir = DEFAULT_REBUILD_CACHEDIR;

    std::filesystem::create_directories(cachedir);
    std::filesystem::create_directories(rebuild_cachedir);

    // Build the pre-transaction image from base and optional Containerfile.pre
    const auto & base_image_id = get_base_image_id(conf_dir);
    const auto & pre_image_id = build_containerfile_if_exists(conf_dir, base_image_id, conf_dir / "Containerfile.pre");

    // Resolve the manifest inside the pre-transaction container
    const libdnf5::utils::fs::TempDir manifest_dir{"manifest"};
    const std::filesystem::path conf_dir_mount_path{"/run/dnf-rebuild/conf"};
    const std::filesystem::path manifest_mount_path{"/run/dnf-rebuild/manifest"};

    // Always pass --refresh to `dnf5 manifest resolve`. The packages in the
    // base image could be from newer repositories than what we have cached
    // locally.
    const auto & manifest_resolve_script = fmt::format(
        R"""(
set -euo pipefail
conf_dir={}
infile={}
manifest_path={}
cd "$conf_dir"
DNF5_FORCE_INTERACTIVE=1 dnf5 manifest resolve --assumeyes --use-system --use-host-repos --refresh --input "$infile" --manifest "$manifest_path"
DNF5_FORCE_INTERACTIVE=1 dnf5 manifest download --assumeyes --manifest "$manifest_path" --setopt=destdir=/var/cache/dnf-rebuild
)""",
        shell_escape(conf_dir_mount_path.string()),
        shell_escape("packages.input.yaml"),
        shell_escape((manifest_mount_path / "packages.manifest.yaml").string()));

    using dnf5::rebuild::subprocess::SubprocessRedirect;
    const auto & resolve_result = dnf5::rebuild::subprocess::run(
        "podman",
        {"podman",
         "run",
         "--volume",
         fmt::format("{}:{}:ro", conf_dir.string(), conf_dir_mount_path.string()),
         "--volume",
         fmt::format("{}:/var/cache/libdnf5:Z", cachedir),
         "--volume",
         fmt::format("{}:/var/cache/dnf-rebuild:Z", rebuild_cachedir),
         "--volume",
         fmt::format("{}:{}:Z", manifest_dir.get_path().string(), manifest_mount_path.string()),
         "--interactive",
         pre_image_id,
         "sh"},
        string_to_byte_vector(manifest_resolve_script),
        SubprocessRedirect::TEE,
        SubprocessRedirect::TEE);
    if (resolve_result.returncode != 0) {
        throw libdnf5::cli::CommandExitError(1, M_("Failed to resolve transaction or download RPMs."));
    }

    // Remove cached RPMs that are not going to be used by the new build.
    // If the manifest cannot be parsed (e.g. produced by a newer
    // libpkgmanifest in the container), skip this step.
    std::optional<libpkgmanifest::manifest::Manifest> manifest;
    try {
        manifest = libpkgmanifest::manifest::Parser().parse(manifest_dir.get_path() / "packages.manifest.yaml");
    } catch (const libpkgmanifest::common::ParserError &) {
    }
    if (manifest.has_value()) {
        std::set<std::string> needed_rpms;
        for (const auto & pkg : manifest->get_packages().get()) {
            const auto & location = pkg.get_location();
            if (!location.empty()) {
                needed_rpms.insert(std::filesystem::path{location}.filename().string());
            }
        }
        for (const auto & entry : std::filesystem::directory_iterator{rebuild_cachedir}) {
            if (entry.is_regular_file() && !needed_rpms.contains(entry.path().filename().string())) {
                std::filesystem::remove(entry.path());
            }
        }
    }

    // Run the transaction using the container's DNF. COPY checksums the
    // manifest; if unchanged, podman reuses the cached layer.
    const auto & containerfile = fmt::format(
        R"""(
FROM {}
COPY packages.manifest.yaml /run/dnf-rebuild/packages.manifest.yaml
RUN SOURCE_DATE_EPOCH=0 DNF5_FORCE_INTERACTIVE=1 dnf5 manifest install \
    --assumeyes \
    --setopt=keepcache=true \
    --manifest /run/dnf-rebuild/packages.manifest.yaml \
    --setopt=destdir=/var/cache/dnf-rebuild \
    && rm -rf /run/dnf-rebuild/packages.manifest.yaml \
        /usr/lib/sysimage/libdnf5/* \
        /usr/lib/sysimage/libdnf5/.* \
        /var/log
)""",
        pre_image_id);

    const auto & build_result = dnf5::rebuild::subprocess::run(
        "podman",
        {"podman",
         "build",
         "--timestamp",
         "0",
         "--file",
         "-",
         "--volume",
         fmt::format("{}:/var/cache/libdnf5:Z", cachedir),
         "--volume",
         fmt::format("{}:/var/cache/dnf-rebuild:Z", rebuild_cachedir),
         manifest_dir.get_path()},
        string_to_byte_vector(containerfile),
        SubprocessRedirect::TEE,
        SubprocessRedirect::TEE);

    if (build_result.returncode != 0) {
        throw libdnf5::cli::CommandExitError(
            1, M_("Failed to build image: {}"), byte_vector_to_string(*build_result.stderr));
    }

    // Last nonempty line of podman build output contains built image ID
    const auto lines = split(byte_vector_to_string(*build_result.stdout), "\n");
    const auto it = std::find_if(lines.rbegin(), lines.rend(), [](const std::string & s) { return !s.empty(); });
    if (it == lines.rend()) {
        throw libdnf5::cli::CommandExitError(1, M_("Could not determine built image ID"));
    }
    const auto & built_image_id = *it;

    // Apply Containerfile.post (if present)
    const auto & post_image_id =
        build_containerfile_if_exists(conf_dir, built_image_id, conf_dir / "Containerfile.post");

    // Tag the final image
    const auto & tag_result = dnf5::rebuild::subprocess::run("podman", {"podman", "tag", post_image_id, tag});
    if (tag_result.returncode != 0) {
        throw libdnf5::cli::CommandExitError(
            1, M_("Failed to tag image {} as {}: {}"), post_image_id, tag, byte_vector_to_string(*tag_result.stderr));
    }

    return tag;
}

void RebuildBuildCommand::set_argument_parser() {
    RebuildSubcommand::set_argument_parser();
    get_argument_parser_command()->set_description(_("Build a container image with layered packages"));
    tag_option = create_tag_option(*this);
}

void RebuildBuildCommand::pre_configure() {
    auto & ctx = get_context();

    ctx.set_create_repos(false);
    ctx.set_load_system_repo(false);
    ctx.set_load_available_repos(Context::LoadAvailableRepos::NONE);
}

void RebuildBuildCommand::configure() {
    validate_conf_dir();
}

void RebuildBuildCommand::run() {
    auto & ctx = get_context();
    const std::filesystem::path conf_dir{config_directory_option->get_value()};
    build(ctx, conf_dir, tag_option->get_value());
}

}  // namespace dnf5
