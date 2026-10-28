#!/usr/bin/env bash
# Publish a release: create the GitHub repository on its first run, push main, and attach the staged zip to a
# release tagged with the version given. Run after stage.py, with everything committed. Needs the GitHub CLI, gh,
# logged in.
set -euo pipefail
REPO=$(cd "$(dirname "$0")/.." && pwd)
SLUG=USS-Mundi/lekmod-bazaar
VERSION=${1:?usage: publish.sh <version, for example v1.0.0>}
ZIP=$REPO/build/stage/lekmod-bazaar.zip
DESCRIPTION="LEKMOD v35.4 with Bazaar built in, plus the MCP server plugin, so you can play LEKMOD with AI in the loop."
[ -f "$ZIP" ] || { echo "no $ZIP; run generator/stage.py first" >&2; exit 1; }
gh auth status >/dev/null 2>&1 || { echo "gh is not logged in; run gh auth login" >&2; exit 1; }
cd "$REPO"
[ -z "$(git status --porcelain)" ] || { echo "uncommitted changes; commit them first" >&2; exit 1; }
if gh repo view "$SLUG" >/dev/null 2>&1; then
  git push origin main
else
  gh repo create "$SLUG" --public --description "$DESCRIPTION" --source "$REPO" --remote origin --push
  gh repo edit "$SLUG" --add-topic civilization-v,civ5,lekmod,bazaar,mcp,modding
fi
gh release create "$VERSION" "$ZIP" --repo "$SLUG" --title "$VERSION" \
  --notes "LEKMOD for Bazaar and MCP Server for LEKMOD, built from LEKMOD v35.4. Install steps are in the README."
echo "published https://github.com/$SLUG/releases/tag/$VERSION"
