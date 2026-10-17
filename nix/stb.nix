{ stb, fetchFromGitHub }:
stb.overrideAttrs (_: {
  version = "unstable-2025-10-26";
  src = fetchFromGitHub {
    owner = "nothings";
    repo = "stb";
    rev = "f1c79c02822848a9bed4315b12c8c8f3761e1296";
    hash = "sha256-BlyXJtAI7WqXCTT3ylww8zoG0hBxaojJnQDvdQOXJPE=";
  };
})
