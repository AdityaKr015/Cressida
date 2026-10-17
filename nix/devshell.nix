{
  mkShell,
  meson,
  ninja,
  pkg-config,
  wayland-scanner,
  python3,
  gdb,
  just,
  nixfmt,
  llvmPackages_22,
  cressida,
  noto-fonts-color-emoji,
}:
mkShell {
  inputsFrom = [ cressida ];
  # Debug recipes use -O0, where Nix's Fortify define cannot take effect.
  # The optimized package build keeps its normal hardening.
  hardeningDisable = [ "fortify" ];
  nativeBuildInputs = [
    meson
    ninja
    pkg-config
    wayland-scanner
    python3
    gdb
    just
    nixfmt
    llvmPackages_22.clang-tools
  ];
  shellHook = ''
    export CRESSIDA_ASSETS_DIR="$PWD/assets"
    export CRESSIDA_EMOJI_FONT="${noto-fonts-color-emoji}/share/fonts/noto/NotoColorEmoji.ttf"
  '';
}
