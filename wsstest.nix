# SPDX-FileCopyrightText: 2025 AstroSnail <astrosnail@protonmail.com>
#
# SPDX-License-Identifier: Apache-2.0

{
  lib,
  stdenv,
  cmake,
  wayland,
  xorg,
}:

let
  pname = "wsstest";
  version = "0.1";

  AstroSnail = {
    name = "AstroSnail";
    email = "astrosnail@protonmail.com";
    matrix = "@astrosnail:matrix.icynet.eu";
    github = "AstroSnail";
    githubId = 15970994;
  };

in
stdenv.mkDerivation {
  inherit pname version;

  src = ./wsstest;
  nativeBuildInputs = [ cmake ];
  buildInputs = [
    wayland
    xorg.libxcb
    xorg.xcbutil
  ];

  meta = {
    description = "Wayland screen locker that displays XScreenSaver hacks";
    license = lib.licenses.asl20;
    maintainers = [ AstroSnail ];
    mainProgram = "wsstest";
    platforms = lib.platforms.all;
  };
}
