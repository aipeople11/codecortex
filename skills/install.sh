#!/usr/bin/env bash
# Install codecortex's agent skills (symlinks back to this repo's skills/, so they stay version-controlled and
# edits here take effect immediately). Default: Claude. Codex: skills/install.sh --codex installs to the
# current cross-agent ~/.agents/skills discovery root; --codex-legacy retains the older CODEX_HOME/skills
# destination. Hermes: skills/install.sh --hermes installs to ${HERMES_HOME:-~/.hermes}/skills (the same
# Agent-Skills-standard SKILL.md files Hermes loads natively, plus the Hermes-native skills under
# skills/hermes/; no `--hook` support there yet). An explicit path remains supported for CI and other clients:
# skills/install.sh PATH.
# Add --hook explicitly to install the observation-only lifecycle hooks for the selected client:
# skills/install.sh --hook (Claude) or skills/install.sh --codex --hook (Codex). These hooks record bounded local telemetry and never route prompts or choose tools. --openclaw installs to
# that same cross-agent root (openclaw's own "compatibility skill root"); it has no hook slot.
set -eu
src="$( cd "$( dirname "$0" )" && pwd )"


# Observation hooks are configured by the native CodeCortex binary.
# They never classify prompts, route work, block tools, or inject context.

resolve_codecortex_binary()
{
    if [ -n "${CODECORTEX_BINARY:-}" ] && [ -x "${CODECORTEX_BINARY}" ]; then printf '%s\n' "$CODECORTEX_BINARY"; return; fi
    if command -v codecortex >/dev/null 2>&1; then command -v codecortex; return; fi
    root="$( dirname "$src" )"
    candidate="$( cd "$root/../.." 2>/dev/null && pwd || true )/bin/codecortex"
    [ -x "$candidate" ] && { printf '%s\n' "$candidate"; return; }
    return 1
}

install_claude_hook()
{
    bin="$(resolve_codecortex_binary)" || { echo "skills/install.sh --hook needs the installed codecortex binary" >&2; exit 1; }
    "$bin" configure-hooks --host claude
}

install_codex_hook()
{
    bin="$(resolve_codecortex_binary)" || { echo "skills/install.sh --codex --hook needs the installed codecortex binary" >&2; exit 1; }
    "$bin" configure-hooks --host codex
}

mode="claude"
explicitMode=0
wantHook=0
wantContributor=0
explicitPath=""
for arg in "$@"; do
    case "$arg" in
        --hook) wantHook=1 ;;
        --contributor) wantContributor=1 ;;
        --codex) mode="codex"; explicitMode=1 ;;
        --openclaw) mode="openclaw"; explicitMode=1 ;;
        --codex-legacy) mode="codex-legacy"; explicitMode=1 ;;
        --claude) mode="claude"; explicitMode=1 ;;
        --hermes) mode="hermes"; explicitMode=1 ;;
        --*) echo "skills/install.sh: unknown option $arg" >&2; exit 2 ;;
        *) [ -z "$explicitPath" ] || { echo "skills/install.sh: only one destination path is allowed" >&2; exit 2; }
           explicitPath="$arg"; mode="path"; explicitMode=1 ;;
    esac
done

# Preserve the established hook-only invocation: `--hook` changes settings but does not also install skills.
if [ "$wantHook" -eq 1 ] && [ "$explicitMode" -eq 0 ]; then
    install_claude_hook
    exit 0
fi

case "$mode" in
    codex) dst="${AGENTS_HOME:-$HOME/.agents}/skills" ;;
    # openclaw resolves to the SAME root Codex uses -- openclaw's docs call it a "compatibility skill
    # root". A repeated value, not a second code path. But it is conditional, and the condition is worth
    # a line of output rather than a support thread: openclaw skips this root entirely unless its state
    # dir is the default. Verified against docs.openclaw.ai, 2026-09-08.
    openclaw) dst="$HOME/.agents/skills"
              # NOT ${AGENTS_HOME:-...}: that is Codex's variable. openclaw honours no such override, so a
              # user who relocated AGENTS_HOME for Codex would be told to install where openclaw never looks.
              if [ -n "${AGENTS_HOME:-}" ] && [ "${AGENTS_HOME}" != "$HOME/.agents" ]; then
                  echo "skills/install.sh: note — AGENTS_HOME is set to '${AGENTS_HOME}', but openclaw does not read it." >&2
                  echo "  Installing to $dst, which is where openclaw actually looks." >&2
              fi
              if [ -n "${OPENCLAW_STATE_DIR:-}" ] && [ "${OPENCLAW_STATE_DIR}" != "$HOME/.openclaw" ]; then
                  echo "skills/install.sh: WARNING — OPENCLAW_STATE_DIR is set to '${OPENCLAW_STATE_DIR}'." >&2
                  echo "  openclaw only reads $dst when its state dir is the default $HOME/.openclaw." >&2
                  echo "  Installing there anyway; openclaw will not discover these skills until that is unset." >&2
              fi ;;
    codex-legacy) dst="${CODEX_HOME:-$HOME/.codex}/skills" ;;
    claude) dst="${CLAUDE_CONFIG_DIR:-$HOME/.claude}/skills" ;;
    hermes) dst="${HERMES_HOME:-$HOME/.hermes}/skills" ;;
    path) dst="$explicitPath" ;;
esac
mkdir -p "$dst"

# PRUNE first: remove any installed codecortex-* skill that this repo no longer ships (deleted or renamed) —
# otherwise a dangling symlink (e.g. a skill removed in a consolidation) lingers forever and an agent
# routing to it hits an error and learns to distrust the whole family. `-L` also catches BROKEN symlinks
# (whose target dir was deleted), which `-e` alone would miss.
# 2026-09-06 (stranger audit): a skill whose SKILL.md front matter says `audience: contributor` is about
# working ON codecortex (codecortex-opt-remarks: clang optimization remarks while editing this tree's C++). It is
# shipped so a contributor can activate it, but it is NOT activated for a user of the tool — the release
# installer runs this script for every agent it detects on a stranger's machine. Pass --contributor to link
# those too; without it a previously linked contributor skill is pruned, so a checkout that stops being a
# contributor setup does not keep one forever.
is_contributor_skill() { grep -q '^audience: contributor' "$1/SKILL.md" 2>/dev/null; }
wanted_skill() { [ "$wantContributor" -eq 1 ] || ! is_contributor_skill "$1"; }

pruned=0
for existing in "$dst"/codecortex-*; do
    [ -e "$existing" ] || [ -L "$existing" ] || continue      # skip the literal glob when nothing matches
    name="$( basename "$existing" )"
    if [ ! -d "$src/$name" ]; then
        # Hermes-native skills live at skills/hermes/<name>, not skills/<name>: a linked one is still
        # shipped, so it is kept — unless it stopped being wanted (contributor-only without
        # --contributor), in which case it is pruned like any other.
        if [ "$mode" = "hermes" ] && [ -d "$src/hermes/$name" ] && wanted_skill "$src/hermes/$name"; then
            continue
        fi
        rm -f "$existing"
        echo "pruned stale $name (no longer shipped)"
        pruned=$(( pruned + 1 ))
    elif ! wanted_skill "$src/$name"; then
        rm -f "$existing"
        echo "pruned $name (contributor-only; pass --contributor to activate it)"
        pruned=$(( pruned + 1 ))
    fi
done

count=0
skipped=0
for d in "$src"/codecortex-*/; do
    name="$( basename "$d" )"
    if ! wanted_skill "$d"; then
        echo "skipped $name (contributor-only: about working on codecortex itself; pass --contributor to activate it)"
        skipped=$(( skipped + 1 ))
        continue
    fi
    ln -sfn "$d" "$dst/$name"
    echo "installed $name -> $dst/$name"
    count=$(( count + 1 ))
done

# Hermes loads the flat Agent-Skills-standard set AND Hermes-native skills (skills/hermes/codecortex-*, e.g.
# the codecortex-repo-map skill purpose-built for Hermes) side by side out of one directory — verified live:
# both formats index together, so --hermes deploys both and no prefer/fallback logic is needed.
# The codecortex-* glob is the SAME name scope the flat install loop and the prune loop above use, and it is
# load-bearing: this loop `ln -sfn`s each entry into the user's skill home under its own name, `ln -sfn`
# unlinks an existing regular file first, and the prune loop only ever looks at codecortex-*. A hermes/ entry
# without the prefix would therefore delete a same-named USER skill and then be impossible to prune. Only
# codecortex-repo-map lives there today, so this is the asymmetry being closed, not a bug being observed.
# Gate: test/hermesinstallcheck.sh arm 7.
if [ "$mode" = "hermes" ]; then
    for nd in "$src"/hermes/codecortex-*/; do
        [ -d "$nd" ] || continue                                  # skip the literal glob when nothing matches
        [ -f "$nd/SKILL.md" ] || continue                        # a Hermes-native skill is a dir with SKILL.md
        nname="$( basename "$nd" )"
        [ -d "$src/$nname" ] && continue                         # flat set already linked it above; one link wins
        if ! wanted_skill "$nd"; then
            echo "skipped $nname (contributor-only: about working on codecortex itself; pass --contributor to activate it)"
            skipped=$(( skipped + 1 ))
            continue
        fi
        ln -sfn "$nd" "$dst/$nname"
        echo "installed $nname -> $dst/$nname (Hermes-native skill)"
        count=$(( count + 1 ))
    done
fi

# The active skill directory is an agent-facing API surface, not a bag of best-effort links. Record the
# exact shipped set only after every link succeeds so `codecortex --doctor --agent=codex` can distinguish a
# complete install from a stale/missing/extra skill without trusting the checkout it came from.
manifestTmp="$( mktemp "$dst/.codecortex-manifest-v1.tmp.XXXXXX" )"
{
    echo 'version=1'
    for d in "$src"/codecortex-*/; do
        wanted_skill "$d" && echo "skill=$( basename "$d" )"
    done
    if [ "$mode" = "hermes" ]; then
        for nd in "$src"/hermes/codecortex-*/; do        # same name scope as the install loop above
            [ -d "$nd" ] || continue
            [ -f "$nd/SKILL.md" ] || continue
            nname="$( basename "$nd" )"
            [ -d "$src/$nname" ] && continue
            wanted_skill "$nd" && echo "skill=$nname"
        done
    fi
} >"$manifestTmp"
mv "$manifestTmp" "$dst/.codecortex-manifest-v1"
echo "done. $count codecortex skills active in every session (${pruned} pruned, ${skipped} contributor-only skipped) — every codecortex-* installed above."

if [ "$wantHook" -eq 1 ]; then
    case "$mode" in
        codex|codex-legacy) install_codex_hook ;;
        claude) install_claude_hook ;;
                hermes) echo "skills/install.sh: --hook is not ported to the Hermes target yet (Hermes exposes hooks: pre_tool_call in config.yaml, but hooks/codecortex-nudge.sh still switches on Claude tool names); codecortex works via the CLI/MCP server there." >&2; exit 2 ;;
        openclaw) echo "skills/install.sh: --hook is not supported for the openclaw target (openclaw's before_tool_call is a plugin API, not a shell hook slot); codecortex works via the CLI/MCP server there." >&2; exit 2 ;;
        path) echo "skills/install.sh: --hook needs --claude or --codex, not an explicit skill path" >&2; exit 2 ;;
    esac
fi
