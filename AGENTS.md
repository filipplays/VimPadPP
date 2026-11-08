# Repository guidance

This repository contains a Windows Notepad++ plugin and a standalone Scintilla
host for Vim-style editing, with native and Neovim backends. The project lives
under `plugin/`.

Read [docs/README.md](docs/README.md) to find the relevant project knowledge.
For substantial changes, consult the applicable architecture notes, decisions,
and existing implementation before choosing an approach. Preserve the global
instructions and the user's explicit authorization requirements.

Build and test targets are documented in
[plugin/makefile-commands.md](plugin/makefile-commands.md). From the repository
root they can be invoked with `make -C plugin <target>`. Builds and tests write
files: in particular, functional test targets format all JSON fixtures, and
fuzzing replaces its temporary output and may remove `.nvimlog`. Obtain any
authorization required by the global instructions before running them.

Running clang-format on C/C++ files edited for an authorized task does not
require separate permission, including when the formatter writes those files.

Use relevant verification for the change and report what ran, its result, and
any material limitations. Update project knowledge when a change makes it
outdated or establishes a useful, verified lesson, subject to the user's
authorization. Record significant decisions without inventing historical
rationale. Delegate independent work only when supported and permitted, and
avoid overlapping edits.
