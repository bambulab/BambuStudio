# Contributing to BambuStudio

Thank you for your interest in contributing! Please read this guide before opening issues or submitting pull requests.

## Reporting Bugs

Use the **Bug Report** or **Crash Report** issue template. Always include:
- BambuStudio version (`Help` → `About Bambu Studio`)
- Operating system and version
- Log files (`Help` → `Show Configuration Folder` → `log/`)

## Suggesting Features

Use the **Feature Request** issue template. Describe the problem you're trying to solve, not just the solution.

## Submitting Pull Requests

1. Fork the repository and create a feature branch
2. Make your changes — keep commits focused and atomic
3. Ensure the code compiles without warnings
4. Run clang-format on changed C/C++ files (see below)
5. Open a PR against `master` and fill in the PR template

### Code Style

C++ code is formatted with **clang-format-18**:

```bash
# Check
git diff --name-only HEAD | grep -E '\.(cpp|cc|h|hpp)$' | xargs clang-format-18 --dry-run --Werror

# Fix
git diff --name-only HEAD | grep -E '\.(cpp|cc|h|hpp)$' | xargs clang-format-18 -i
```

The `clang_format` CI check runs automatically on every PR.

### Commit Signing

All commits to `master` require a GPG signature:

```bash
git commit -S -m "your message"
```

## Development Setup

See the wiki for platform-specific build instructions:

- [Building on Linux](https://github.com/BenJule/BambuStudio/wiki/Building-from-source-Linux)
- [Building on macOS](https://github.com/BenJule/BambuStudio/wiki/Building-from-source-macOS)
- [Building on Windows](https://github.com/BenJule/BambuStudio/wiki/Building-from-source-Windows)

## Security Issues

Do **not** open public issues for security vulnerabilities. Use [GitHub's private advisory reporting](https://github.com/BenJule/BambuStudio/security/advisories/new) instead. See [SECURITY.md](SECURITY.md) for details.

## License

By contributing, you agree that your contributions will be licensed under the [AGPL-3.0 License](LICENSE).
