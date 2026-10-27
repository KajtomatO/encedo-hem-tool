#!/usr/bin/env bash
#
# release-notes.sh — the GitHub Release text for a tag (REQ-PKG-003): the
# annotated tag's message, then the SHA256SUMS block. A lightweight tag has no
# message, so it fails here — before anything is published.
#
# implements: REQ-PKG-003
#
#   scripts/release/release-notes.sh <owner/repo> <tag> <SHA256SUMS> > notes.md
#
# Reads the tag through the GitHub REST API — authenticated with GH_TOKEN /
# GITHUB_TOKEN when set (the workflow passes github.token), anonymous otherwise
# (enough for a public repository). A signature block of a signed tag is not
# part of the notes.
set -euo pipefail

REPO="${1:?usage: release-notes.sh <owner/repo> <tag> <SHA256SUMS>}"
TAG="${2:?tag}"
SUMS="${3:?SHA256SUMS file}"
API="${GITHUB_API_URL:-https://api.github.com}"
TOKEN="${GH_TOKEN:-${GITHUB_TOKEN:-}}"
[ -f "$SUMS" ] || { echo "$SUMS missing" >&2; exit 1; }

AUTH=()
[ -z "$TOKEN" ] || AUTH=(-H "Authorization: Bearer $TOKEN")
api() {  # api <path below /repos/<repo>/> — GET, JSON on stdout
  curl -fsSL -H 'Accept: application/vnd.github+json' "${AUTH[@]}" "$API/repos/$REPO/$1"
}
json() {  # json <python expression over d> — reads JSON on stdin
  python3 -c "import json, sys; d = json.load(sys.stdin); print($1)"
}

REF="$(api "git/ref/tags/$TAG")"
if [ "$(json 'd["object"]["type"]' <<< "$REF")" != tag ]; then
  echo "$TAG is a lightweight tag — the release notes come from the tag annotation;" \
       "recreate it with: git tag -a $TAG -m '…'" >&2
  exit 1
fi
MSG="$(api "git/tags/$(json 'd["object"]["sha"]' <<< "$REF")" \
  | json 'd["message"].split("-----BEGIN ")[0].strip()')"
[ -n "$MSG" ] || { echo "$TAG has an empty annotation" >&2; exit 1; }

printf '%s\n\n### SHA256SUMS\n\n```\n' "$MSG"
cat "$SUMS"
printf '```\n'
