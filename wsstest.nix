# SPDX-FileCopyrightText: 2025 AstroSnail <astrosnail@protonmail.com>
#
# SPDX-License-Identifier: Apache-2.0

{
  lib,
  maintainers,
  stdenv,
  cmake,
  wayland,
  wayland-protocols-client,
  xorg,
}:

let
  wayland-protocols-lib = wayland-protocols-client.override {
    extensions = [
      "xdg-shell"
      "ext-session-lock-v1"
    ];
  };

in
stdenv.mkDerivation {
  pname = "wsstest";
  version = "0.1";

  src = ./wsstest;
  nativeBuildInputs = [ cmake ];
  buildInputs = [
    wayland
    wayland-protocols-lib
    xorg.libxcb
    xorg.xcbutil
  ];

  meta = {
    description = "Wayland screen locker that displays XScreenSaver hacks";
    license = lib.licenses.asl20;
    maintainers = [ maintainers.AstroSnail ];
    mainProgram = "wsstest";
    platforms = lib.platforms.all;
  };
}
