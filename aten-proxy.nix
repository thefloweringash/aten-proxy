{ stdenv, libev, libvncserver, cmake, pkgconfig }:

stdenv.mkDerivation {
  name = "aten-proxy";
  version = "0.0.1";

  src = stdenv.lib.cleanSource ./.;

  nativeBuildInputs = [ cmake pkgconfig ];
  buildInputs = [ libev libvncserver ];

  installPhase = ''
    mkdir -p $out/bin
    cp aten-proxy $out/bin
  '';
}
