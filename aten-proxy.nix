{ stdenv, libev, libvncserver, pkgconfig }:

stdenv.mkDerivation {
  name = "aten-proxy";
  version = "0.0.1";

  src = ./.;

  nativeBuildInputs = [ pkgconfig ];
  buildInputs = [ libev libvncserver ];

  installPhase = ''
    mkdir -p $out/bin
    cp aten-proxy $out/bin
  '';
}
