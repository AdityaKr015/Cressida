set positional-arguments

mode := "debug"
prefix := "/usr/local"
cpp-std := "c++23"

default:
    @just --list

# Configure debug, release, or asan builds.
configure m=mode install_prefix=prefix:
    #!/usr/bin/env bash
    set -euo pipefail
    case "$1" in
        debug)   args=(--buildtype=debug) ;;
        release) args=(--buildtype=release -Db_lto=true) ;;
        asan)    args=(--buildtype=debug -Db_sanitize=address,undefined) ;;
        *) echo "Unknown mode: $1 (expected debug, release, or asan)" >&2; exit 1 ;;
    esac
    build_path="build-$1"
    if [[ -f "$build_path/meson-private/coredata.dat" ]]; then
        args+=(--reconfigure)
    fi
    meson setup "$build_path" "${args[@]}" -Dcpp_std={{cpp-std}} "-Dpkg_config_path=${PKG_CONFIG_PATH:-}" --prefix "$2"
    cp "$build_path/compile_commands.json" compile_commands.json

_ensure-configured m=mode:
    #!/usr/bin/env bash
    set -euo pipefail
    case "$1" in debug|release|asan) ;; *) echo "Unknown mode: $1" >&2; exit 1 ;; esac
    if [[ ! -f "build-$1/build.ninja" ]]; then
        just configure "$1"
    else
        meson setup "build-$1" --reconfigure -Dcpp_std={{cpp-std}} "-Dpkg_config_path=${PKG_CONFIG_PATH:-}"
    fi

# Build the desktop client and its libraries.
build m=mode: (_ensure-configured m)
    meson compile -C build-{{m}} -j 4 cressida
    cp build-{{m}}/compile_commands.json compile_commands.json

run m=mode *args: (build m)
    #!/usr/bin/env bash
    set -euo pipefail
    exec "./build-$1/cressida" "${@:2}"

demo m=mode: (build m)
    ./build-{{m}}/cressida --demo

# Render one demo frame on the current Wayland desktop and exit, without network access.
smoke m=mode: (build m)
    ./build-{{m}}/cressida --smoke-test

# Build and run text and image regression tests; accepts Meson test arguments.
test m=mode *args: (_ensure-configured m)
    #!/usr/bin/env bash
    set -euo pipefail
    meson test -C "build-$1" --print-errorlogs "${@:2}"

install m=mode: (build m)
    meson install --no-rebuild -C build-{{m}}

uninstall m=mode: (_ensure-configured m)
    ninja -C build-{{m}} uninstall

# Format project C++ and Nix files; external decoder code is excluded.
format:
    find src tests \( -name '*.cpp' -o -name '*.h' \) -print0 | xargs -0 clang-format -i
    nixfmt flake.nix shell.nix nix/*.nix

# Build and test the locked package in the Nix sandbox.
check:
    nix flake check path:.

clean m=mode:
    #!/usr/bin/env bash
    set -euo pipefail
    case "$1" in debug|release|asan) ;; *) echo "Unknown mode: $1" >&2; exit 1 ;; esac
    rm -rf -- "build-$1"

rebuild m=mode: (clean m) (build m)
