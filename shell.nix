{
  pkgs ? import <nixpkgs> { },
}:
pkgs.callPackage ./nix/devshell.nix {
  cressida = pkgs.callPackage ./nix/package.nix { };
}
