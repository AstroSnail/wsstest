# SPDX-FileCopyrightText: 2025 AstroSnail <astrosnail@protonmail.com>
#
# SPDX-License-Identifier: Apache-2.0

{
  description = "Wayland screen locker that displays XScreenSaver hacks";

  inputs = {
    nixpkgs = {
      url = "nixpkgs";
    };

    flake-parts = {
      url = "flake-parts";
      inputs.nixpkgs-lib.follows = "nixpkgs";
    };

    systems = {
      url = "systems";
    };

    git-hooks-nix = {
      url = "github:cachix/git-hooks.nix";
      inputs.nixpkgs.follows = "nixpkgs";
    };

    treefmt-nix = {
      url = "github:numtide/treefmt-nix";
      inputs.nixpkgs.follows = "nixpkgs";
    };
  };

  outputs =
    inputs: inputs.flake-parts.lib.mkFlake { inherit inputs; } { imports = [ ./flake-module.nix ]; };
}
