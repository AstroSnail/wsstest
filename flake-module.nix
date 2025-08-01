# SPDX-FileCopyrightText: 2025 AstroSnail <astrosnail@protonmail.com>
#
# SPDX-License-Identifier: Apache-2.0

{ config, inputs, ... }:

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
      wsstest = pkgs.callPackage ./wsstest.nix { };
      wsstest-clang = wsstest.override { stdenv = pkgs.clangStdenv; };

      wsstest-dev = pkgs.mkShell {
        packages = wsstest.buildInputs ++ [
          pkgs.clang-tools
          pkgs.gdb
          pkgs.lldb
          pkgs.reuse
          pkgs.statix
          pkgs.valgrind
        ];
        env.HACKS = "${pkgs.xscreensaver}/libexec/xscreensaver";
        shellHook = perSystem.config.pre-commit.installationScript;
      };

      inherit (config.flake) overlays;
      treefmt = perSystem.config.treefmt.build.wrapper;

    in
    {
      _module.args.pkgs = import inputs.nixpkgs {
        inherit system;
        # config.allowUnfree = true;
        overlays = [ overlays.default ];
      };

      devShells = {
        inherit wsstest-dev;
        default = wsstest-dev;
      };

      formatter = treefmt;

      packages = {
        inherit wsstest wsstest-clang;
        default = wsstest;
      };
    };
}
