{ config, inputs, ... }:

{
  imports = [
    "${inputs.nixpkgs}/nixos/modules/misc/lib.nix"
    ./lib.nix
    ./overlays.nix
    ./git-hooks.nix
    ./treefmt.nix
  ];

  systems = import inputs.systems;

  perSystem =
    { pkgs, system, ... }@perSystem:

    let
      wsstest = pkgs.callPackage ./wsstest.nix { };

      wsstest-dev = pkgs.mkShell {
        packages = wsstest.buildInputs ++ [
          pkgs.statix
          pkgs.valgrind
          pkgs.xscreensaver
        ];
        shellHook = perSystem.config.pre-commit.installationScript;
      };

      inherit (config.lib.util) appOutputs;
      inherit (config.flake) overlays;
      treefmt = perSystem.config.treefmt.build.wrapper;

    in
    {
      _module.args.pkgs = import inputs.nixpkgs {
        inherit system;
        #config.allowUnfree = true;
        overlays = [ overlays.default ];
      };

      apps = appOutputs {
        inherit wsstest treefmt;
        default = wsstest;
      };

      devShells = {
        inherit wsstest-dev;
        default = wsstest-dev;
      };

      formatter = treefmt;

      packages = {
        inherit wsstest;
        default = wsstest;
      };
    };
}
