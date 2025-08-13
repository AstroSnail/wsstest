# SPDX-FileCopyrightText: 2025 AstroSnail <astrosnail@protonmail.com>
#
# SPDX-License-Identifier: MIT

{
  lib,
  maintainers,
  stdenv,
  meson,
  ninja,
  pkg-config,
  wayland,
  wayland-scanner,
  wayland-protocols,
  extensions ? [ ],
}:

let
  mesonArray =
    name: value:
    assert (lib.isString name);
    assert (lib.isList value);
    let
      # not sure if this is a correct escape function for meson
      qvalue = lib.map (v: "'" + lib.escape [ "'" ] v + "'") value;
      arraystring = "[" + lib.concatStringsSep ", " qvalue + "]";
    in
    lib.mesonOption name arraystring;

in
stdenv.mkDerivation {
  pname = "wayland-protocols-client";
  inherit (wayland-protocols) version;

  src = ./wayland-protocols-client;
  nativeBuildInputs = [
    meson
    ninja
    pkg-config
    wayland-scanner
  ];
  buildInputs = [
    wayland
    wayland-protocols
  ];

  mesonFlags = [ (mesonArray "extensions" extensions) ];

  meta = {
    description = "Wayland protocol extensions client side library";
    license = lib.licenses.mit;
    maintainers = [ maintainers.AstroSnail ];
    platforms = lib.platforms.all;
  };
}
