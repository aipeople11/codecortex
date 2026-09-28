# Skills consolidation — 30 → 17 (Wave 3, 2026-07)

The skills audit found 30 user-facing skills collapsing into
~8 jobs with mutually indistinguishable descriptions — a routing hazard and a token tax. This pass merged
them into **17 routable skills**, each answerable to "when would an agent pick THIS one over every other".

No content was invented; every merged skill preserves the best material of its sources (the concrete
use-when cues + honesty calibration of the 2025-refreshed skills), and every command line runs against this
repo. Absorbed skill directories were deleted — their content lives on in the merged skill named below.

## Old skill → new home

| Old skill            | New home                     | Notes |
|----------------------|------------------------------|-------|
| codecortex-orient       | **codecortex-orient**           | kept; now the escalation-ladder base |
| codecortex-tour         | **codecortex-orient**           | folded in as the report→deps→communities→hotspots ladder |
| codecortex-zoom         | **codecortex-orient**           | folded in as the nested-hierarchy + --mermaid/--html rungs |
| codecortex-navigate     | **codecortex-navigate**         | kept; --query-vs---for + graph-query pointer retained |
| codecortex-explain      | **codecortex-navigate**         | folded in as the one-symbol deep-dive section |
| codecortex-triage       | **codecortex-find-bug**         | → Branch A ("symptom, no idea where") |
| codecortex-bisect       | **codecortex-find-bug**         | → Branch B ("suspect a subsystem, narrow it") |
| codecortex-diagnose     | **codecortex-find-bug**         | → Branch C ("I changed X and it broke" — --situ) |
| codecortex-design       | **codecortex-before-you-build** | → feasibility spike / reusable blocks |
| codecortex-plan         | **codecortex-before-you-build** | → plan section |
| codecortex-spike        | **codecortex-before-you-build** | → feasibility-spike section |
| codecortex-scope        | **codecortex-before-you-build** | → sizing-rubric section |
| codecortex-interface    | **codecortex-before-you-build** | → interface section (design-time boundary work) |
| codecortex-pr-impact    | **codecortex-change-check**     | keeps --map-diff + --rank-by=churn |
| codecortex-pre-pr       | **codecortex-change-check**     | keeps --map-diff + --rank-by=churn |
| codecortex-fresh-eyes   | **codecortex-fresh-eyes**       | kept; now the repo-wide once-over base |
| codecortex-clone-hunt   | **codecortex-fresh-eyes**       | folded into duplicate-bodies pass |
| codecortex-owners       | **codecortex-fresh-eyes**       | folded into ownership / bus-factor pass |
| codecortex-dead-code    | **codecortex-fresh-eyes**       | folded in; dead-code's honest caveats preserved verbatim |
| codecortex-coupling     | **codecortex-fresh-eyes**       | folded into the hidden-coupling (co-change) pass |
| codecortex-audit-skill  | **codecortex-security-scan**    | → skill-file scanner section |
| codecortex-mcp-audit    | **codecortex-security-scan**    | → .mcp.json manual-checklist section |
| codecortex-efficient    | **codecortex-efficient**        | kept standalone |
| codecortex-quality-bar  | **codecortex-quality-bar**      | kept; cross-ref updated pre-pr → change-check |
| codecortex-reuse-first  | **codecortex-reuse-first**      | kept standalone |
| codecortex-review       | **codecortex-change-check** + **codecortex-fresh-eyes** | **DELETED** — split by moment (see 2026-07 MOMENT pass below) |
| codecortex-compress     | **codecortex-compress**         | kept standalone (the detail-ladder rewrite); later merged into codecortex-efficient, see 2026-07-05 below |
| codecortex-handoff      | **codecortex-handoff**          | kept standalone |
| codecortex-layers       | **codecortex-layers**           | kept standalone (--arch gate + baseline workflow) |
| codecortex-mcp          | **codecortex-mcp**              | kept; cross-ref updated mcp-audit → security-scan |
| codecortex-graph-query  | **codecortex-graph-query**      | kept standalone |
| codecortex-perf-target  | **codecortex-perf-target**      | kept standalone; description sharpened + routing header added |
| —                    | **codecortex-write-tests**      | NEW (S4, 2026-07-10) — no predecessor; untested-code coverage moment had no home until then |

## Judgment calls beyond the target taxonomy

- **interface → before-you-build** and **coupling → fresh-eyes** (the audit flagged both as optional). Both
  moves make descriptions MORE distinguishable: interface is a design-*time* boundary activity alongside
  spike/plan/scope; coupling is a hidden-dependency *sweep* over the same descriptive facts fresh-eyes
  already surfaces. This left `interface`/`coupling` from having near-duplicate "assess structure" triggers.
- **layers kept standalone** rather than folded into fresh-eyes: it owns the CI-enforceable `--arch=rules.txt`
  gate + baseline workflow (referenced elsewhere, e.g. from orient) — a distinct "is my architecture drifting"
  moment vs fresh-eyes' "what's the state of this code" once-over.
- **perf-target kept standalone** (it was the 30th skill, unplaced by the target taxonomy): "where should I
  optimize before profiling" is a moment no other skill claims. Description sharpened + routing header added.

Net: 17 skills (7 merged homes + 10 kept), each with a discriminating description and a routing header naming
its nearest neighbours.

## MOMENT pass (2026-07) — route by moment, not by feature

The 17 were well-written individually but agents **route by MOMENT**, and three moments were co-claimed by
near-synonym descriptions (router stalls → agent falls back to grep). This pass rewrote DESCRIPTIONS to be
pickable from the description alone, split `codecortex-review` by moment, and added an entry-point router.

- **WRITE moment de-collided.** `codecortex-reuse-first` = "about to write ONE symbol (fn/class/helper)";
  `codecortex-before-you-build` = "about to start a FEATURE (multi-symbol, needs a plan/interface/sizing)";
  `codecortex-efficient` reframed as a pure cross-cutting DISCIPLINE ("token+accuracy discipline for any read"),
  dropping "about to author" so it stops competing at the write moment. `--exemplar` now surfaced in
  before-you-build too (was reachable only from reuse-first).
- **`codecortex-review` DELETED, split by moment:** its diff-review material (blast radius, tests, hotspots,
  `--metrics` interpretation) → **codecortex-change-check**; its unfamiliar-subsystem-risk + refactor-planning
  material → **codecortex-fresh-eyes** (now scope-to-a-subsystem, not repo-only). change-check + quality-bar are
  now an explicit CHAIN ("did the code get worse" → "is it safe to merge").
- **UNDERSTAND de-duped:** the verbatim "how does X work / where is Y" now lives only on `codecortex-orient`;
  `codecortex-efficient` dropped it.
- **Stranded features got interpretation homes:** `--metrics` attrs (`cbo`/`lcom4`/`nest`/`loc`/`params`/
  `tested`) interpreted with thresholds+actions in change-check; refactor/god-object trigger + `--communities`/
  `--cochange` in fresh-eyes; portable `--cache=FILE` one-liner in efficient.
- **Router added:** `codecortex-router` = the moment→skill map (10+ moments → one skill each) + the two-reflex
  primer (before you write → `--exemplar`; before done → `--quality-delta`). Every skill also gained a
  one-line routing header so a wrong route self-corrects in one hop.

Net: 17 skills (review deleted, router added). The count is a rounding error; the win is every recognized
moment has exactly one clear entry.

## 2026-07-05 — codecortex-compress merged into codecortex-efficient (17 → 16)

`codecortex-compress`'s detail ladder (map → `--pack-signatures` → `--outline` → `--expand` → `--pack-top-n`)
and its `--compress` scope/when-NOT-to-compress guidance never had its own MOMENT — agents already land in
`codecortex-efficient` first (the map-before-you-read discipline), and `--compress` is the next question once
you're already reading a body `--expand`/`--outline` surfaced. Moved verbatim (plus the redaction/
`--no-redact` note, A3-S5) into `skills/codecortex-efficient/compress-ladder.md`, a companion file loaded on
demand — same pattern as `codecortex-quality-bar/quality-metrics.md`. `codecortex-efficient/SKILL.md` gained one
pointer line; every `codecortex-compress` reference elsewhere (router's reference-skills list) was repointed.
Directory `skills/codecortex-compress/` deleted.

Net: 16 skills (compress folded into efficient as a companion file, no moment lost).

## 2026-07-10 — codecortex-write-tests added (S4) (16 → 17)

The untested-code-coverage moment ("this is untested, add coverage") had no home — it's distinct from
judging your own diff (`codecortex-change-check`) or your own code's quality (`codecortex-quality-bar`). New
standalone skill `codecortex-write-tests`: ranks candidates by `--seams` (untested cross-module edges) and the
`tested=1` coverage lens, pulls the symbol's outside contract via `--callers`, verifies the new test registers with
`--affected`. No skill was merged or deleted for this one — it's a genuinely new moment.

Net: 17 skills (write-tests added, no other change).

## 2026-09-07 — the efficient skill folded into codecortex-orient (18 → 17 dirs; 16 routable + router)

The description-budget round (issue #49; `docs/EVALS.md` "Skill descriptions under a client budget") put
every description in front of three blind LLM raters — full text, head-cut at 350, and the rewrite. The one
loss that survived in every model and every arm was the efficient skill's rows routing to `codecortex-orient`:
raters could not see the boundary even from the full 977-character description, because "about to open
several files to answer one question" is the same moment as "understand this fast". The fold arm had zero
such misses. So the directory is gone: its `SKILL.md` became `skills/codecortex-orient/map-before-you-read.md`
(the discipline, verbatim), `compress-ladder.md` moved beside it, orient's description claims the moment, the
router's four rows point at orient, the corpus's 12 rows carry the mechanical relabel, and `--help-task`'s
`compact-legend` intent names orient.

Net: 17 skill directories (16 routable + the router; `codecortex-opt-remarks` stays contributor-gated at install).
