# Project knowledge

Use these pages to find the information you need:

- [Project context](project-context.md): purpose, technology, constraints, and
  links to existing development documentation.
- [Architecture](architecture.md): components, integration paths, and testing
  boundaries.
- [Decisions](decisions/README.md): when and how to record significant
  architectural decisions.
- [Build and test commands](../plugin/makefile-commands.md): dependencies,
  build targets, installation, and replay.
- [Makefile map](../plugin/makefile-map.md): how the targets fit together.
- [Functional tests](../plugin/tests/README.md): JSON fixtures and focused
  Neovim checks.
- [Differential fuzzing](../plugin/tests_fuzzing/README.md): generated tests,
  reports, and limits of the comparison.
- [Neovim review findings](neovim-review-findings.md): findings from the
  2026-11-22 review, verification limits, and later status notes.

Check the code and tests when a page may be out of date. Keep confirmed behavior,
proposals, and open questions clearly separate. A review report records what was
found at the time; check its status notes before treating a finding as current.

Update an existing page when behavior changes. Add a page only when it makes the
information easier to find or maintain. Preserve the history of decision records.
Follow the user's approval rules for all file changes, including changes to
personal preferences or global instructions.
