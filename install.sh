#!/usr/bin/env bash
# jptxt installer for macOS / Linux
#   curl -fsSL https://raw.githubusercontent.com/juricap/jptxt/main/install.sh | bash
# Pin:  curl -fsSL ... | JPTXT_VERSION=v0.1.2 bash
set -euo pipefail

REPO="${JPTXT_REPO:-juricap/jptxt}"
PREFIX="${JPTXT_PREFIX:-$HOME/.local}"
BIN_DIR="${PREFIX}/bin"
VERSION="${JPTXT_VERSION:-}"

os=$(uname -s)
arch=$(uname -m)
case "$os" in
  Linux)  os_tag=linux ;;
  Darwin) os_tag=macos ;;
  *) echo "jptxt: unsupported OS $os" >&2; exit 1 ;;
esac
case "$arch" in
  x86_64|amd64) arch_tag=x64 ;;
  arm64|aarch64) arch_tag=arm64 ;;
  *) echo "jptxt: unsupported arch $arch" >&2; exit 1 ;;
esac

ASSET="jptxt-${os_tag}-${arch_tag}"
if [[ -n "$VERSION" ]]; then
  [[ "$VERSION" == v* ]] || VERSION="v$VERSION"
  URL="https://github.com/${REPO}/releases/download/${VERSION}/${ASSET}"
else
  URL="https://github.com/${REPO}/releases/latest/download/${ASSET}"
fi

mkdir -p "$BIN_DIR"
DEST="${BIN_DIR}/jptxt"
TMP="${DEST}.tmp.$$"
echo "Downloading $URL"
if command -v curl >/dev/null 2>&1; then
  curl -fL --retry 3 -o "$TMP" "$URL"
elif command -v wget >/dev/null 2>&1; then
  wget -O "$TMP" "$URL"
else
  echo "jptxt: need curl or wget" >&2
  exit 1
fi
chmod +x "$TMP"
# sanity: not an HTML error page
if head -c 20 "$TMP" | grep -qi '<html'; then
  echo "jptxt: download looks like HTML — no release asset $ASSET yet?" >&2
  rm -f "$TMP"
  exit 1
fi
mv -f "$TMP" "$DEST"

echo
echo "Installed $DEST"
case ":$PATH:" in
  *":$BIN_DIR:"*) ;;
  *)
    echo "Add this to your shell rc so 'jptxt' is on PATH:"
    echo "  export PATH=\"$BIN_DIR:\$PATH\""
    ;;
esac
echo "Run:  jptxt"

