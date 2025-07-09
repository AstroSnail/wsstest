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
  buildInputs = [ xorg.libxcb ];
  meta.description = "wayland screensaver test";
  meta.mainProgram = pname;
}
