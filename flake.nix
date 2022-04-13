{
  description = "A very basic flake";

  outputs = { self, nixpkgs }: {

    packages.aarch64-darwin.aten-proxy = nixpkgs.legacyPackages.aarch64-darwin.callPackage ./aten-proxy.nix {};

    defaultPackage.aarch64-darwin = self.packages.aarch64-darwin.aten-proxy;

  };
}
