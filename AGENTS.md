# Contributor rules

- Do not add Markdown files. Explain a module in a docstring or header comment
  of at most 10 lines at the top of its projector or source: what it changes,
  why, and the key constraint.
- Docs describe the current design only. Keep history and validation narratives
  out of them; do not describe a batch, pass, or round of work.
- Put working notes, measurements, investigations, and transfer notes in
  `archive/` or `scratch/`. Both directories are ignored by Git.
- Use plain words. Do not qualify nouns with actual, real, existing, canonical,
  or concrete unless contrasting with a named alternative.
- Use "manifest" or "build record" for generated inventories and results.
- Comments explain why in one or two lines; do not narrate changes.
- Put embedded C++ in multi-line triple-quoted blocks or template files.
  Use one list element per line whenever a line would exceed 160 characters.
- Run the hygiene check and host tests before every commit.
- Clear named DEFERRED entries whenever work touches and rebuilds those files.
