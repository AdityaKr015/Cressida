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
  tomlplusplus,
  curl,
  autoAddDriverRunpath,
  xdg-utils,
  noto-fonts-color-emoji,
  python3,
  dbus,
  md4c,
  libsecret,
  ffmpeg-headless,
  mpv-unwrapped,
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
    dbus
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
    tomlplusplus
    md4c
    libsecret
    ffmpeg-headless
    mpv-unwrapped
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
