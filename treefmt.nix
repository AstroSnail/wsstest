# SPDX-FileCopyrightText: 2025 AstroSnail <astrosnail@protonmail.com>
#
# SPDX-License-Identifier: Apache-2.0

{ inputs, ... }:

{
  imports = [ inputs.treefmt-nix.flakeModule ];

  perSystem = {
    treefmt.programs = {
      nixfmt = {
        enable = true;
        strict = true;
        # nixfmt uses this as a "soft cap". i don't know what the hard cap is,
        # but this seems to keep lines within 80 characters.
        # TODO: also add it to git-hooks, otherwise it will complain.
        # width = 72;
      };

      clang-format.enable = true;

      # rustfmt.enable = true;
      # rustfmt.edition = config.cargo.package.edition;

      shellcheck.enable = true;
      shfmt.enable = true;
    };
  };
}
