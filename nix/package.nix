{
  lib,
  stdenv,
  callPackage,
  meson,
  ninja,
  pkg-config,
  wayland-scanner,
  wayland,
  wayland-protocols,
  libglvnd,
  libGL,
  cairo,
  pango,
  freetype,
  fontconfig,
  harfbuzz,
  librsvg,
  libwebp,
  libjxl,
  libxkbcommon,
  nlohmann_json,
  curl,
  autoAddDriverRunpath,
  xdg-utils,
  noto-fonts-color-emoji,
  python3,
  md4c,
  libsecret,
}:
stdenv.mkDerivation {
  pname = "cressida";
  version = "0.1.0";

  src = lib.cleanSourceWith {
    src = ../.;
    filter =
      path: type:
      lib.cleanSourceFilter path type
      && !(type == "directory" && lib.hasPrefix "build" (builtins.baseNameOf path))
      && !(builtins.elem (builtins.baseNameOf path) [
        ".direnv"
        ".agents"
        ".aws"
        ".codex"
      ]);
  };

  outputs = [
    "out"
    "dev"
  ];
  nativeBuildInputs = [
    meson
    ninja
    pkg-config
    wayland-scanner
    autoAddDriverRunpath
    xdg-utils
    python3
  ];
  buildInputs = [
    wayland
    wayland-protocols
    libglvnd
    libGL
    cairo
    pango
    freetype
    fontconfig
    harfbuzz
    librsvg
    libwebp
    libjxl
    libxkbcommon
    nlohmann_json
    md4c
    libsecret
    (curl.override { websocketSupport = true; })
    (callPackage ./stb.nix { })
  ];

  doCheck = true;
  mesonFlags = [ "-Demoji_font=${noto-fonts-color-emoji}/share/fonts/noto/NotoColorEmoji.ttf" ];

  meta = {
    description = "Cressida — native C++ Discord desktop client";
    mainProgram = "cressida";
    license = with lib.licenses; [
      mit
      asl20
    ];
    platforms = lib.platforms.linux;
  };
}
