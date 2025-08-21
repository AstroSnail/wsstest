# SPDX-FileCopyrightText: 2025 AstroSnail <astrosnail@protonmail.com>
#
# SPDX-License-Identifier: Apache-2.0

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
      # TODO: spaces
      qvalue = lib.map (v: "'" + lib.escape [ "'" ] v + "'") value;
      arraystring = "[" + lib.concatStringsSep "," qvalue + "]";
    in
    lib.mesonOption name arraystring;

in
stdenv.mkDerivation {
  pname = "wayland-client-protocols";
  inherit (wayland-protocols) version;

  src = ./wayland-client-protocols;
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
    # the output of this package is substancially derived from wayland-protocols
    inherit (wayland-protocols.meta) license platforms;
    description = "Wayland protocol extensions client side library";
    maintainers = [ maintainers.AstroSnail ];
  };
}
