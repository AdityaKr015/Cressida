# Cressida documentation

The website's editable documentation lives here on `main`. Start at
[Welcome to Cressida](user/index.md) to read the guides.

## Edit or add a guide

- Edit Markdown files in `user/`. Keep filenames lowercase with hyphens.
- Keep `title` and `description` in each page's frontmatter.
- Link to another guide with its relative filename, such as
  `[Installation](installation.md)`. Anchors work too. The website converts
  these links to its public routes during rendering.
- Add or rearrange sidebar entries in `sidebar.json`. Slugs start with `docs/`;
  for example, `user/manual-installation.md` uses `docs/manual-installation`.
- Put guide images beside the pages, for example in `user/assets/`, and use
  relative image links. Keep website logos and homepage artwork on `website`.
- Write for users, document current behavior and do not use em dashes.

The website code stays on the `website` branch. Contributors do not need to
switch branches just to update a guide or its sidebar entry.

## Preview and validate locally

Use a local website checkout with Node.js 24 and npm. In that checkout, set the
absolute path to this directory, then run:

```sh
export CRESSIDA_DOCS_DIR=/absolute/path/to/client/docs
npm ci
npm run dev
```

To check formatting, links and the production build:

```sh
npm run docs:format
npm run check
npm run build
```

`docs:format` formats this source directory. The website's own `format` command
formats its code. `dev`, `check` and `build` import fresh docs automatically. If
you edit docs while the development server is running, use `npm run docs:sync`
and restart the server to reload content and navigation. Generated content lives
under the website's ignored `.docs/` directory and is never committed.

## Publishing

- A push to `main` deploys only when a file under `docs/` changes.
- A documentation PR targeting `main` builds against the website branch without
  deploying. It checks formatting, navigation, links, search and browser
  behavior.
- Every push to `website` rebuilds with current `main` documentation.
- Maintainers can manually run **Documentation website** to rebuild both
  sources.

GitHub Actions combines the branches and publishes static output to GitHub
Pages. It does not copy documentation between branches or create synchronization
commits. Builds resolve both source revisions once so checks and deployment use
the same content. The `github-pages` environment must allow deployments from
both `main` and `website`.

In **Settings > Pages > Build and deployment**, select **GitHub Actions** as the
source. Selecting a source branch runs the default Jekyll pipeline against Astro
files and can report invalid YAML frontmatter. The workflow must compile Astro
and publish `dist/`; the `website` branch contains source code, not built HTML.
