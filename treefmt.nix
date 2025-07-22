# SPDX-FileCopyrightText: 2025 AstroSnail <astrosnail@protonmail.com>
#
# SPDX-License-Identifier: Apache-2.0

{ inputs, ... }:

{
  imports = [ inputs.treefmt-nix.flakeModule ];

  perSystem = {
    treefmt.programs.nixfmt.enable = true;

    treefmt.programs.clang-format.enable = true;

    # treefmt.programs.rustfmt.enable = true;
    # treefmt.programs.rustfmt.edition = config.cargo.package.edition;
  };
}
