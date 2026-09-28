---
name: codecortex-repo-map
description: "Use codecortex CLI to map a repo before reading it."
version: 1.0.0
author: Ashutosh Singh
license: Apache-2.0
platforms: [linux, macos]
metadata:
  hermes:
    tags: [codecortex, repo-map, call-graph, context-engineering, code-navigation, static-analysis]
    related_skills: [systematic-debugging, codebase-inspection]
prerequisites:
  commands: [codecortex]
---

# codecortex — deterministic repo map before reading code

`codecortex` (v0.4.x at authoring time; the release binary must be on PATH) is a zero-dependency C++23
CLI that builds a ranked, deterministic call graph of any repo in ~1s (parses once, then auto-caches
warm). It answers "what to touch, what it breaks, which tests to run" for a fraction of the tokens of
grep + whole-file reads. No API key, no daemon, no index server, offline. Invoke it via the terminal
tool.

## When to reach for it (instead of blind grep / whole-file reads)

- Landing cold in a repo or subsystem — "what's here, how is it organized, what matters"
- "Where is the code for X" — a task, feature, or concept in your own words
- A change is proposed — what breaks, who calls it, which tests must run
- Resuming work after compaction / a new session on in-flight work
- Reviewing code you did not write (fresh eyes), or pre-commit change vetting

Do NOT use for trivial single-file lookups (plain `rg`/search_files wins) or broad common-word
questions — codecortex shines on specific technical asks.

## Core verbs (all take a repo dir; `--help` is authoritative)

```bash
codecortex <dir> --report            # architecture summary: modules, god-files, cycles, top symbols
codecortex <dir> --for="<task in your own words>"   # ranked task lens: what to touch first, with 1-hop edges
codecortex <dir> --callers=someFunc --legend=compact  # who calls it (blast radius)
codecortex <dir> --test-gate --legend=compact         # which tests must run before committing
codecortex <dir> --tree --legend=compact              # file-by-file: top symbols per file
codecortex <dir> --hotspots --legend=compact          # files ranked by churn × complexity
codecortex <dir> --communities --legend=compact       # cohesive modules (Louvain) + bridges — where a new feature belongs
codecortex <dir> --situ              # what the working tree already changed + its blast radius (resuming work)
codecortex <dir> --notes --legend=compact             # pinned gotchas from past sessions (field notes)
codecortex <git-url>                 # orient in a remote repo (shallow-clones into a cached temp dir; --refetch refreshes)
```

Read the map, then open only the files it surfaces (god-files and hotspots first). For a body of one
ranked symbol: `codecortex <dir> --for="..." --expand=path:name` (paste `p=`/`n=` off the row).

## Honesty signals — read them before trusting a result

- The `--for` lens reports `confidence=`/`margin_pct=` on its result root and the output carries
  `est_tokens=` — `confidence="low"` or a flat ranking means treat the set as a starting point, not an answer.
- `--for` bundles: `bodies="0"` = no full bodies shipped (`reason="compact-route"`; `--auto-bodies`
  restores them. A `reason="budget"` means the caller's token budget cut them — raise the budget instead).
- `--skipped` lists files the index dropped (oversize, or `.gitignore`d — default ignored; `--no-ignore`
  restores the full walk). A "not found" is only trustworthy after checking this.
- `amb="K"` on a symbol means K of its outbound calls matched several possible definitions — the
  resolver split the weight instead of choosing one. Read the source if the target matters.
  (`lpin=` is a separate, disclosed guess the resolver made; same remedy.)

## Pitfalls

1. **First call on a tree parses** (~1s at 1,500+ files; measured ≈1.2s wall for a 2,355-file /
   77k-edge checkout on Apple M2). Later calls are warm and near-instant — chain verbs freely.
2. `--top-k` is NOT read by `--for`'s own bundle (it shapes the default map and `--query`; it still
   applies to `--format=candidates` with `--for`).
3. Markdown/docs-only or shell-only repos yield few call-graph edges — `--report` is still useful, edges are not.
4. The tool indexes signatures, not necessarily full bodies, by default on conceptual queries — `--expand`
   or `--auto-bodies` when you need the body.
5. Cache/notes live in the repo tree (`.codecortex_notes` for `--note-add`); the notes file is meant to be committed.
6. codecortex output can be large on big repos — budget with `--max-tokens=8000` / `--top-k=50` where supported.

## Reference links

- CodeCortex repository map skill. See the local README and LICENSE files for installation and licensing.
- The project ships 18 agent skills (Claude Code/Codex format) under `skills/` in its repo — this Hermes
  skill is the distilled Hermes-format counterpart to that family (`codecortex-orient` is the closest base);
  read those upstream for deeper flows.
