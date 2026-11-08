---
name: capture-project-knowledge
description: Capture or update durable project knowledge when a user correction, technical decision, verified lesson, or documentation review warrants a persistent record.
---

# Capture project knowledge

Use this workflow for reusable project knowledge, not every correction or
task-specific detail. Follow the user's authorization rules for file changes;
invoking this skill does not itself authorize unrelated edits.

1. Identify the candidate knowledge and why it will help future work. Check
   relevant code, tests, and documentation for evidence and existing coverage.
   Distinguish observed behavior, confirmed decisions, proposals, and unknowns;
   do not invent historical rationale or convert an isolated correction into a
   broad rule.
2. Choose the narrowest suitable home using the repository's `docs/README.md`:
   update existing project context, architecture, test guidance, or a decision
   record as appropriate. Keep temporary task details out of permanent guidance.
   Add a separate lessons document only when existing homes are insufficient.
3. Propose or make a concise, focused update as authorized. Preserve decision
   history and resolve or flag contradictions instead of silently adding a
   competing rule. Obtain approval before storing personal preferences or
   changing global instructions. Observe any stricter file-change requirements.
4. Check links and claims relevant to the update. Report what was recorded,
   the evidence or remaining uncertainty, and any proposed change awaiting
   authorization. If nothing is sufficiently reusable, explain that instead
   of creating a record.
