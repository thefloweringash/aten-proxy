with import <nixpkgs> {
  overlays = [ (self: super: {
    aten-proxy = super.callPackage ./aten-proxy.nix {};
  })];
}; aten-proxy
