# SPDX-FileCopyrightText: 2025 AstroSnail <astrosnail@protonmail.com>
#
# SPDX-License-Identifier: Apache-2.0

{ inputs, ... }:

{
  imports = [ inputs.git-hooks-nix.flakeModule ];

  perSystem =
    # { pkgs, ... }:
    {
      pre-commit.settings.hooks.deadnix.enable = true;
      pre-commit.settings.hooks.nixfmt-rfc-style.enable = true;
      pre-commit.settings.hooks.statix.enable = true;

      pre-commit.settings.hooks.clang-format.enable = true;
      # TODO
      # pre-commit.settings.hooks.clang-tidy.enable = true;

      # # BUG: clippy check passes even when clippy has things to say!!!
      # pre-commit.settings.hooks.clippy.enable = true;
      # pre-commit.settings.hooks.rustfmt.enable = true;
      # pre-commit.settings.settings.rust.check.cargoDeps = pkgs.rustPlatform.importCargoLock {
      #   lockFile = ./Cargo.lock;
      # };
    };
}
