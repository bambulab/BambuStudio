# Contributing

Thanks for helping make the open networking integration easier to build and use. The most useful contributions right now are clean-machine build fixes, runtime staging improvements, and carefully recorded printer tests.

## Before changing code

Read [the architecture](docs/ARCHITECTURE.md) and [current verification limits](docs/STATUS.md). Keep fixes focused on the fork's integration where possible. Preserve upstream copyright notices and follow the surrounding code style.

Clone with submodules. Use compatible toolchains for Studio and the plugin. Keep the application in a separate directory and use a dedicated profile for testing.

## Pull requests

Explain the problem, how your change addresses it, and what you tested. For build changes, include OS, compiler/toolset, configuration, and exact commands. For printer tests, include model, firmware, operating mode, and the outcome of each tested action.

Keep `FULLY_OPEN_BUILD=ON` for open-build testing. Check that stock plugin download/install/OTA paths remain disabled and that the loader uses the intended bundled libraries. A successful compile is valuable; distinguish it from runtime or physical-printer validation.

Changes to the networking implementation itself are best developed with [Open Bamboo Networking](https://github.com/ClusterM/open-bamboo-networking). Link upstream fixes before updating this fork's pinned revision.

## Bug reports

Use the issue form. Provide a minimal reproducer and the first relevant error. Remove account tokens, printer access codes, serial numbers, and private network information from logs before posting them.

## Licensing

Contributions must be compatible with the existing AGPL-3.0 license. Preserve dependency licenses and include provenance for any runtime packaging change. Do not commit profiles, credentials, backups, proprietary plugin packages, or local build output.
