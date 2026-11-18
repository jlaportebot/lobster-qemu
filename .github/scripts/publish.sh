#!/usr/bin/env bash
# Publish archives as a GitHub release.
# usage: publish.sh <tag> <rolling|release> <dir-with-archives>
# Needs GH_TOKEN, GITHUB_REPOSITORY and GITHUB_SHA in the environment.
set -euo pipefail
TAG=$1; MODE=$2; DIR=$3
cd "$DIR"
sha256sum lobster-qemu-* > SHA256SUMS
cat SHA256SUMS

if [ "$MODE" = rolling ]; then
    # Re-create so the tag moves to the commit that was just built and old
    # assets are replaced.
    gh release delete "$TAG" --cleanup-tag --yes || true
    gh release create "$TAG" --target "$GITHUB_SHA" --prerelease \
        --title "LobsterOS QEMU ($TAG, ${GITHUB_SHA::7})" \
        --notes "Rolling build of commit $GITHUB_SHA. Self-contained qemu-system-aarch64 + qemu-img per platform." \
        lobster-qemu-* SHA256SUMS
else
    gh release create "$TAG" --target "$GITHUB_SHA" --generate-notes \
        lobster-qemu-* SHA256SUMS
fi
