# Contributing to Cressida

Keep contributions focused, use C++23 and preserve the native Wayland renderer
and independent Discord backend. External pages open in the user's browser;
no GTK or embedded browser dependencies.

Install the compiler, build tools and development libraries listed in
[PACKAGING.md](PACKAGING.md#dependencies) using your distribution's package
manager. Install `just` for the convenience commands, then run from the checkout:

```sh
just build
just test
just format
just lint
```

`just build` and `just test` use Meson and Ninja directly; Nix is optional.
`just format` needs clang-format 22 and nixfmt. `just lint` also needs actionlint,
ShellCheck and Python 3.11+; it checks formatting, workflow syntax,
shell helpers and automation tests without rewriting files. For C++ changes,
`clang-format --dry-run -Werror path/to/changed.cpp` checks individual files if
you do not have the full repository-tooling set installed. Explain any omitted
checks in the PR. Use `just test asan` for memory-sensitive changes; the sanitizer
recipes also need `llvm-symbolizer` on PATH.

If you use Nix, `nix develop path:.` supplies the dependencies and developer tools,
and `just check` builds and tests the locked Nix package. You can also build,
test and stage packages with plain Meson; see [PACKAGING.md](PACKAGING.md#build-and-install).

For UI changes, run `just demo`. Set `CRESSIDA_DEMO_VIEW` to select a preview,
for example `CRESSIDA_DEMO_VIEW=themes just demo`.
`just smoke-headless build-debug/cressida` runs representative UI replays under
a private Weston compositor with software rendering; install Weston and Mesa
for this optional check. Its helper lives alongside the workflow scripts in
`.github/workflows/scripts/`. Check relevant dark/light themes, narrow windows
and interface scaling, and attach redacted screenshots.
These checks do not establish live Discord compatibility.

Use the shared spacing, font, radius and control tokens in `src/ui/style.h`.
Keep application geometry in `src/app/layout_metrics.h`, derive related sizes
from those tokens, and share the resulting measurements with scrolling and hit
targets. Component-specific values should have names that explain their purpose.

Open a draft PR while work or validation is incomplete. Keep the required PR
template headings and Checklist wording; ready PRs need a filled summary,
motivation, testing notes, a selected change type and every Checklist item
checked. The description check reports missing items without editing or closing
the PR. Explain omitted tests in the Testing section.

Use synthetic fixtures and local peers in automated tests. Do not include login
tokens, account state, core dumps, private conversations or real account
mutations. Bug reports should include reproduction steps, build version,
compositor, GPU/driver and relevant redacted terminal output. `just debug` and
`just run asan` provide crash diagnostics; inspect output before sharing it.

Keep README.md as a short landing page. Update relevant documentation when
behavior, build commands or dependencies change, and preserve third-party notices.
For new features, describe the proposed behavior in an issue before starting a
large implementation.

User-facing guides live in [`docs/user/`](docs/user/) on `main`; navigation lives
in `docs/sidebar.json`. Edit them alongside client changes, using relative `.md`
links. The Astro/Starlight site lives on `website` and imports this documentation
at build time. Documentation PRs run site checks without deploying. Only changes
under `docs/` on `main`, any push to `website`, or an explicit manual run deploy
the site. See [`docs/README.md`](docs/README.md) for authoring and local previews.

CI checks formatting and automation, builds/tests glibc packages on x86_64 and
aarch64 Linux, tests musl on x86_64 Alpine edge, and runs offline UI replays
against the glibc package. All three checks contribute to `CI passed`.
The musl workflow can also be dispatched separately. To reproduce it with Docker:

```sh
docker run --rm --env CI_UID="$(id -u)" --env CI_GID="$(id -g)" \
  --volume "$PWD:/workspace" --workdir /workspace \
  alpine:edge /bin/sh .github/workflows/scripts/build-musl.sh
```

The helper installs Alpine dependencies and checksum-verified stb headers at the
revision used by Nix, then builds and runs the native Meson suite as your UID.
It uses `build-musl`, leaving existing build directories alone. Setup requires
network access; tests use local fixtures. CI retains build and Meson logs for
seven days, including failures. This covers dynamic musl builds, not static
binary distribution.

Maintainers can require `CI passed` and
`PR description` in repository rules. The weekly flake updater opens draft PRs;
enable **Allow GitHub Actions to create and approve pull requests** in Actions
settings for it. Tag releases create drafts with source archives and checksums
after CI passes; publishing remains a maintainer action.
