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
root use `make -C plugin <target>` consistently. Builds and tests write
files: in particular, functional test targets format all JSON fixtures, and
fuzzing replaces its temporary output and may remove `.nvimlog`.

As a project-specific exception to the global command-approval instructions,
once implementation is explicitly authorized, run relevant documented builds,
tests, fuzzing, and Wine test runners without asking again. This authorization
includes their documented fixture formatting, generated outputs, temporary-output
replacement, `.nvimlog` removal, and Wine runtime writes. Prefer running Wine
through the documented make targets. Discussion alone does not authorize
implementation. Installing into Notepad++ still requires an explicit request.
This authorization does not bypass sandbox restrictions or execution approval
rules; request execution approval when the environment requires it.

Use the repository-root [.clang-format](.clang-format) for C/C++ formatting.
Before finishing an authorized C/C++ change, format only the files edited for
that task with `clang-format --style=file -i <edited-files>`, run from the
repository root. The formatter discovers this configuration for files below
the root, so no machine-specific configuration path is needed. Do not format
unrelated files. Report the formatting command used, or explain why formatting
could not run.

Running clang-format on C/C++ files edited for an authorized task does not
require separate permission, including when the formatter writes those files.

Use relevant verification for the change and report what ran, its result, and
any material limitations. Update project knowledge when a change makes it
outdated or establishes a useful, verified lesson, subject to the user's
authorization. Record significant decisions without inventing historical
rationale. Delegate independent work only when supported and permitted, and
avoid overlapping edits.
