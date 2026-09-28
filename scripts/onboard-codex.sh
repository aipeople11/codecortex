#!/usr/bin/env bash
# Compatibility entrypoint. The unified installer owns host setup now.
set -eu
dir="$(cd "$(dirname "$0")" && pwd)"
exec "$dir/onboard.sh" --host codex "$@"
