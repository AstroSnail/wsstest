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
