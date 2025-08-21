# SPDX-FileCopyrightText: 2025 AstroSnail <astrosnail@protonmail.com>
#
# SPDX-License-Identifier: Apache-2.0

{
  config,
  inputs,
  lib,
  ...
}:

{
  imports = [
    ./overlays.nix
    ./git-hooks.nix
    ./treefmt.nix
  ];

  systems = import inputs.systems;

  perSystem =
    { pkgs, system, ... }@perSystem:

    let
      inherit (pkgs) wayland-client-protocols wsstest wsstest-clang;

      wsstest-dev = pkgs.wsstest-dev.overrideAttrs (
        _final: _prev: { shellHook = perSystem.config.pre-commit.installationScript; }
      );

      treefmt = perSystem.config.treefmt.build.wrapper;

    in
    {
      _module.args.pkgs = import inputs.nixpkgs {
        inherit system;
        # config.allowUnfree = true;
        overlays = lib.attrValues config.flake.overlays;
      };

      devShells = {
        inherit wsstest-dev;
        default = wsstest-dev;
      };

      formatter = treefmt;

      packages = {
        inherit wayland-client-protocols wsstest wsstest-clang;
        default = wsstest;
      };
    };
}
