# Contributing to OpenStudio

Thanks for your interest in contributing! This project is a community fork of
[Bambu Studio](https://github.com/bambulab/BambuStudio) that stays synced with
official releases while layering on extra features and fixes. Contributions
of all sizes are welcome — bug fixes, new features, documentation, and CI/build
improvements.

## Before you start

- **Search existing issues and PRs first** to avoid duplicate work.
- **For larger features**, please open an issue to discuss the approach before
  investing significant time — this helps avoid wasted effort on changes that
  might not fit the project's direction.
- **If a bug also exists in official Bambu Studio**, consider reporting it
  upstream too (https://github.com/bambulab/BambuStudio/issues), since a fix
  there benefits far more users. Fixes specific to this fork's own
  build/release pipeline or fork-only features belong here.

## Branching model

- `dev` — target branch for most contributions. Prerelease builds are
  published automatically from every push here, so you can verify your
  change works in a real build before it reaches `master`.
- `master` — mirrors the latest official release plus this fork's merged
  changes. Full releases are published automatically from here.

Please branch off `dev` and open your pull request against `dev`, unless
you have a specific reason to target `master` directly (e.g. an urgent
fix that must ship immediately).

## Making a change

1. Fork this repository and create a branch off `dev`:
   `git checkout -b my-feature origin/dev`
2. Make your changes. Keep commits focused and use clear commit messages.
3. If you changed C++ source, try to build locally first using the platform
   guide that matches your OS (see the [How to compile](README.md#how-to-compile)
   section in the README) — CI will also build every PR, but a local build
   catches problems faster.
4. If you changed a GitHub Actions workflow (`.github/workflows/*.yml`),
   validate the YAML syntax before opening the PR, e.g.:
   `ruby -ryaml -e "YAML.load_file('.github/workflows/<file>.yml')"`
5. Open a pull request against `dev` describing:
   - what the change does and why
   - how you tested it
   - any follow-up work or known limitations

## Code style

Please try to match the existing style/formatting of the surrounding code
rather than introducing a new style in the same file. Avoid unrelated
reformatting in a PR that's meant to fix a specific issue — it makes the
diff harder to review.

## Reporting bugs / requesting features

Use the [issue tracker](../../issues) and pick the template that best matches
your report (bug report or feature request).

## Questions?

If anything about the contribution process is unclear, feel free to open an
issue and ask — we're happy to help.
