# SPDX-FileCopyrightText: 2025 AstroSnail <astrosnail@protonmail.com>
#
# SPDX-License-Identifier: Apache-2.0

{
  flake.overlays =

    let
      maintainers = import ./maintainers.nix;

      wayland-protocols-client = final: _prev: {
        wayland-protocols-client = final.callPackage ./wayland-protocols-client.nix {
          inherit maintainers;
        };
      };

      wsstest = final: _prev: { wsstest = final.callPackage ./wsstest.nix { inherit maintainers; }; };

      wsstest-clang = final: _prev: {
        wsstest-clang = final.wsstest.override { stdenv = final.clangStdenv; };
      };

      wsstest-dev = final: _prev: {
        wsstest-dev = final.mkShell {
          packages = final.wsstest.buildInputs ++ [
            final.clang-tools
            final.gdb
            final.lldb
            final.reuse
            final.shellcheck
            final.shfmt
            final.statix
            final.valgrind
          ];
          env = {
            HACKS = "${final.xscreensaver}/libexec/xscreensaver";
            WAYLAND_DEBUG = 1;
          };
        };
      };

    in
    {
      inherit
        wayland-protocols-client
        wsstest
        wsstest-clang
        wsstest-dev
        ;
    };
}
