..
    Copyright Contributors to the DNF5 project.
    SPDX-License-Identifier: GPL-2.0-or-later

.. _rebuild_plugin_ref-label:

##################
 Rebuild Command
##################

.. warning::
   The rebuild plugin is under development and considered experimental. The
   interface and behavior may change.

Synopsis
========

``dnf5 rebuild init [options]``

``dnf5 rebuild install [options] <package-spec>...``

``dnf5 rebuild remove [options] <package-spec>...``

``dnf5 rebuild upgrade [options]``

``dnf5 rebuild rebase [options] <image>``

``dnf5 rebuild build [options]``

``dnf5 rebuild switch [options]``


Description
===========

Layer packages on `bootc <https://containers.github.io/bootc/>`_ systems.

The rebuild plugin manages a local layer definition that describes packages to
install on top of a base bootc container image. It builds a derived container
image with the layered packages and switches the system to it using
``bootc switch``.

The layer definition is stored in a configuration directory (by default
``/etc/dnf/dnf5-plugins/rebuild.d``) and consists of:

``base``
    A file containing the pinned base image reference (``image@digest``).

``packages.input.yaml``
    A `libpkgmanifest <https://github.com/rpm-software-management/libpkgmanifest>`_
    input file listing packages to layer.

``Containerfile.pre``
    Optional Containerfile steps executed before the package transaction.

``Containerfile.post``
    Optional Containerfile steps executed after the package transaction.

Get started by running ``dnf5 rebuild init``, which will create a blank local
layer definition based on the staged or booted bootc image. Then, add layered
packages by editing ``/etc/dnf/dnf5-plugins/rebuild.d/packages.input.yaml`` or
by running ``dnf5 rebuild install <package>``. Run ``dnf5 rebuild switch`` to
build the new local layer and switch to it. To upgrade the base layer and all
layered packages, run ``dnf5 rebuild upgrade`` instead of ``bootc upgrade``.

Note: after switching to an image built by ``dnf5 rebuild``, ``bootc status``
will report the staged/booted image as
``containers-storage:localhost/dnf-rebuild:latest``, and ``bootc upgrade`` will
have no effect since it won't recognize the underlying base image or know how
to rebuild the local layer. ``bootc rebase <base image>`` can be used to
discard the local layer and switch back to a system managed only by bootc
without the rebuild plugin.


Subcommands
===========

``init``
    Create a blank local layer definition based on the staged or booted bootc
    image. Initializes the configuration directory with an empty
    ``packages.input.yaml``, template Containerfiles, and a ``base`` file
    pinned to the currently deployed image.

    The configuration directory must not already exist or must be empty.

``install <package-spec>...``
    Add one or more package specs to the ``packages.input.yaml`` input file.
    If a given package spec is already present, it is skipped.

    By default, immediately builds the derived image and switches to it.

``remove <package-spec>...``
    Remove one or more package specs from the ``packages.input.yaml`` input
    file. If a given package spec is not present, it is skipped. Note: removing
    packages from the base image is planned but not yet supported.

    By default, immediately builds the derived image and switches to it.

``upgrade``
    Upgrade the base image to the latest digest from the registry. Fetches the
    latest digest for the current base image reference and updates the ``base``
    file.

    By default, immediately builds the derived image and switches to it.

``rebase <image>``
    Rebase to a new container image. Replaces the ``base`` file with the given
    image reference. If the reference does not include a digest, fetches the
    latest digest.

    By default, immediately builds the derived image and switches to it.

``build``
    Build a container image with the layered packages without switching to it.

``switch``
    Build a container image with the layered packages and switch to it using
    ``bootc switch``.


---------
Arguments
---------

``<package-spec>``
    Package specification to add or remove from the layer definition.
    Used by the ``install`` and ``remove`` subcommands.

``<image>``
    Container image reference (e.g. ``quay.io/fedora/fedora-bootc:rawhide``).
    Used by the ``rebase`` subcommand. May include a digest
    (``image@sha256:...``); if omitted, the latest digest is fetched
    automatically.


-------
Options
-------

The following options are shared by the ``install``, ``remove``, ``upgrade``,
``rebase``, ``build``, and ``switch`` subcommands unless noted otherwise.

``--config-directory <path>``
    Path to the rebuild configuration directory.
    Default: ``/etc/dnf/dnf5-plugins/rebuild.d``.

``--tag <tag>``
    Tag to apply to the built container image.
    Default: ``localhost/dnf-rebuild:latest``.

``--switch``
    Build and switch to the image. This is the default for ``install``,
    ``remove``, ``upgrade``, and ``rebase``.
    Not available for ``build`` (build-only) or ``switch`` (always switches).

``--no-switch``
    Do not build or switch after modifying the layer definition.
    Not available for ``build`` or ``switch``.

``--apply``
    Apply the changes immediately and reboot (calls ``bootc switch --apply``).

``--soft-reboot``
    Perform a soft reboot after switching (calls ``bootc switch --soft-reboot``).


--------
Examples
--------

``dnf5 rebuild init``
    Initialize a new layer definition from the currently deployed bootc image.

``dnf5 rebuild install vim tmux``
    Add ``vim`` and ``tmux`` to the layer, build, and switch.

``dnf5 rebuild install --no-switch htop``
    Add ``htop`` to the layer definition without building or switching.

``dnf5 rebuild remove vim``
    Remove ``vim`` from the layer, build, and switch.

``dnf5 rebuild upgrade``
    Pull the latest digest for the base image, rebuild, and switch.

``dnf5 rebuild upgrade --no-switch``
    Pull the latest digest for the base image without rebuilding.

``dnf5 rebuild rebase quay.io/fedora/fedora-bootc:42``
    Rebase to Fedora 42, build, and switch.

``dnf5 rebuild build``
    Build the container image without switching to it.

``dnf5 rebuild switch --apply``
    Build and switch to the image, and reboot to apply changes immediately.

``dnf5 rebuild switch --apply --soft-reboot``
    Build, switch, and apply via soft reboot.


See Also
========

    | `bootc upstream <https://containers.github.io/bootc/>`_
    | `libpkgmanifest upstream <https://github.com/rpm-software-management/libpkgmanifest>`_
