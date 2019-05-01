{ stdenv, libev, libvncserver, cmake, ninja, pkgconfig }:

stdenv.mkDerivation {
  name = "aten-proxy";
  version = "0.0.1";

  src = stdenv.lib.cleanSource ./.;

  nativeBuildInputs = [ cmake pkgconfig ninja ];
  buildInputs = [ libev libvncserver ];

  installPhase = ''
    mkdir -p $out/bin
    cp aten-proxy $out/bin
  '';
}
