# SPDX-FileCopyrightText: 2025 AstroSnail <astrosnail@protonmail.com>
#
# SPDX-License-Identifier: Apache-2.0

{ inputs, lib, ... }:

{
  imports = [ inputs.git-hooks-nix.flakeModule ];

  perSystem =
    # { pkgs, ... }:
    lib.mkMerge [
      {
        pre-commit.settings.hooks = {
          reuse.enable = true;

          deadnix.enable = true;
          nixfmt-rfc-style.enable = true;
          statix.enable = true;

          clang-format.enable = true;
          # TODO
          # clang-tidy.enable = true;

          shellcheck.enable = true;
          shfmt.enable = true;
        };
      }

      {
        # # BUG: clippy check passes even when clippy has things to say!!!
        # pre-commit.settings = {
        #   hooks = {
        #     clippy.enable = true;
        #     rustfmt.enable = true;
        #   };
        #   settings.rust.check.cargoDeps = pkgs.rustPlatform.importCargoLock {
        #     lockFile = ./Cargo.lock;
        #   };
        # };
      }
    ];
}
