{
  description = "FELF — Fat ELF: a self-contained Linux ELF container and toolchain";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  # Everything is defined in nix/ (see nix/default.nix); this file only
  # exposes it per system.  ../default.nix does the same without flakes.
  outputs = { self, nixpkgs }:
    let
      systems = [ "x86_64-linux" "aarch64-linux" ];
      forAllSystems = f: nixpkgs.lib.genAttrs systems (system:
        f (import ./nix { pkgs = import nixpkgs { inherit system; }; }));
    in {
      packages = forAllSystems (felf: felf.packages);
      checks = forAllSystems (felf: felf.checks);
      devShells = forAllSystems (felf: { default = felf.shell; });
    };
}
