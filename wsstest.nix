# SPDX-FileCopyrightText: 2025 AstroSnail <astrosnail@protonmail.com>
#
# SPDX-License-Identifier: Apache-2.0

{
  stdenv,
  cmake,
  xorg,
}:

let
  pname = "wsstest";
  version = "0.1";

in
stdenv.mkDerivation {
  inherit pname version;
  src = ./wsstest;
  nativeBuildInputs = [ cmake ];
  buildInputs = [
    xorg.libxcb
    xorg.xcbutil
  ];
  meta.description = "wayland screensaver test";
  meta.mainProgram = pname;
}
