# Consumer selections

Shipyard main contains the maintained shared platform at the repository root.
Smash selects that root. OoT selects `3Shipsty`; Majora selects `2Ship`.
These directories preserve the consumer-specific production sources and their
tests from the previously selected worktrees without replacing another game's
renderer. Each selection has its own production baseline.

Consumer locks name the selection, pin the same Shipyard Git revision, and hash
that selection's baseline. Run its tests from the selection directory. Shared
corrections must be applied to every affected selection and their baselines
regenerated. Historical branches and worktrees remain available.
