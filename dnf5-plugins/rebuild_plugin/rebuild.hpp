// Copyright Contributors to the DNF5 project.
// SPDX-License-Identifier: LGPL-2.1-or-later

#ifndef DNF5_PLUGINS_REBUILD_PLUGIN_REBUILD_HPP
#define DNF5_PLUGINS_REBUILD_PLUGIN_REBUILD_HPP

#include <dnf5/context.hpp>
#include <json.h>
#include <libdnf5/conf/option_bool.hpp>
#include <libdnf5/conf/option_enum.hpp>

const std::string DEFAULT_REBUILD_CONFIG_DIRECTORY{"/etc/dnf/dnf5-plugins/rebuild.d"};
const std::string DEFAULT_REBUILD_CACHEDIR{"/var/cache/dnf-rebuild"};
const std::string DEFAULT_REBUILD_IMAGE_TAG{"localhost/dnf-rebuild:latest"};

namespace dnf5 {

std::unique_ptr<json_object, decltype(&json_object_put)> get_bootc_status();

std::string get_base_image_id(const std::filesystem::path & conf_dir);
std::string build_containerfile_if_exists(
    const std::filesystem::path & conf_dir,
    const std::string & from_image_id,
    const std::filesystem::path & containerfile_path);

/// Build a container image from the configuration.
///
/// Applies Containerfile.pre, resolves the infile, installs packages inside
/// the container, applies Containerfile.post, and tags the final image.
std::string build(Context & ctx, const std::filesystem::path & conf_dir, const std::string & tag);

/// Query the registry for the latest digest of an image via skopeo.
std::string get_latest_digest(const std::string & image);

void bootc_switch(const std::string & tag, bool apply, const std::string & soft_reboot);

libdnf5::OptionString * create_tag_option(Command & command);
libdnf5::OptionBool * create_apply_option(Command & command);
libdnf5::OptionEnum * create_soft_reboot_option(Command & command);
libdnf5::OptionBool * create_switch_option(Command & command);


class RebuildCommand : public Command {
public:
    explicit RebuildCommand(Context & context) : Command(context, "rebuild") {}
    void set_parent_command() override;
    void set_argument_parser() override;
    void register_subcommands() override;
    void pre_configure() override;
};


class RebuildSubcommand : public Command {
public:
    explicit RebuildSubcommand(Context & context, const std::string & name) : Command(context, name) {}
    void set_argument_parser() override;

protected:
    /// @brief Validate that the configuration directory exists, is a directory, and is non-empty.
    void validate_conf_dir();

    libdnf5::OptionPath * config_directory_option{nullptr};
};

class RebuildInitCommand : public RebuildSubcommand {
public:
    explicit RebuildInitCommand(Context & context) : RebuildSubcommand(context, "init") {}
    void set_argument_parser() override;
    void configure() override;
    void run() override;
};


/// Base class for subcommands that support --switch/--no-switch, --tag, --apply, --soft-reboot.
class RebuildSwitchableCommand : public RebuildSubcommand {
public:
    using RebuildSubcommand::RebuildSubcommand;
    void set_argument_parser() override;
    void pre_configure() override;
    void configure() override;

protected:
    /// Build the image and switch to it if --switch is set.
    void build_and_switch_if_needed();

    libdnf5::OptionString * tag_option{nullptr};
    libdnf5::OptionBool * switch_option{nullptr};
    libdnf5::OptionBool * apply_option{nullptr};
    libdnf5::OptionEnum * soft_reboot_option{nullptr};
};

class RebuildUpgradeCommand : public RebuildSwitchableCommand {
public:
    explicit RebuildUpgradeCommand(Context & context) : RebuildSwitchableCommand(context, "upgrade") {}
    void set_argument_parser() override;
    void run() override;
};

class RebuildRebaseCommand : public RebuildSwitchableCommand {
public:
    explicit RebuildRebaseCommand(Context & context) : RebuildSwitchableCommand(context, "rebase") {}
    void set_argument_parser() override;
    void run() override;

private:
    std::string image_spec;
};

class RebuildInstallCommand : public RebuildSwitchableCommand {
public:
    explicit RebuildInstallCommand(Context & context) : RebuildSwitchableCommand(context, "install") {}
    void set_argument_parser() override;
    void run() override;

private:
    std::vector<std::string> pkg_specs;
};

class RebuildRemoveCommand : public RebuildSwitchableCommand {
public:
    explicit RebuildRemoveCommand(Context & context) : RebuildSwitchableCommand(context, "remove") {}
    void set_argument_parser() override;
    void run() override;

private:
    std::vector<std::string> pkg_specs;
};

class RebuildBuildCommand : public RebuildSubcommand {
public:
    explicit RebuildBuildCommand(Context & context) : RebuildSubcommand(context, "build") {}
    void set_argument_parser() override;
    void pre_configure() override;
    void configure() override;
    void run() override;

private:
    libdnf5::OptionString * tag_option{nullptr};
};

class RebuildSwitchCommand : public RebuildSubcommand {
public:
    explicit RebuildSwitchCommand(Context & context) : RebuildSubcommand(context, "switch") {}
    void set_argument_parser() override;
    void pre_configure() override;
    void configure() override;
    void run() override;

private:
    libdnf5::OptionString * tag_option{nullptr};
    libdnf5::OptionBool * apply_option{nullptr};
    libdnf5::OptionEnum * soft_reboot_option{nullptr};
};

}  // namespace dnf5

#endif  // DNF5_PLUGINS_REBUILD_PLUGIN_REBUILD_HPP
