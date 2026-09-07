#!/usr/bin/env bash
# ============================================================
# 生成源码归档（评审 #1：GPL Corresponding Source）
# 内容：全部 C++ 源码、CMake、打包脚本（含 piper_server.py）、
#       设计文档、README/LICENSE、CI 工作流。
# 排除：dist/（发布二进制）、build/（构建产物）、.git/。
# 用法：make_source_archive.sh [VERSION]
# ============================================================
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PKGNAME="cspdfreader"
VERSION="${1:-$(sed -n 's/^project(CSPDFREADER VERSION \([0-9.]*\).*/\1/p' "$ROOT/CMakeLists.txt" | head -1)}"
[ -n "$VERSION" ] || VERSION="1.0.2"

OUT_DIR="$ROOT/dist/release"
OUT="$OUT_DIR/${PKGNAME}-${VERSION}-src.tar.gz"
mkdir -p "$OUT_DIR"

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

echo "==> 生成源码归档 ${PKGNAME}-${VERSION}-src.tar.gz"
cd "$ROOT"
# 以 git 跟踪清单为准（含 packaging/piper_server.py），排除发布产物与构建目录
git archive --format=tar --prefix="${PKGNAME}-${VERSION}/" HEAD -- . \
    ':(exclude)dist' ':(exclude)build' | tar -x -C "$TMP"

# 关键校验：piper_server.py 必须在源码包中（可审计，GPL Corresponding Source）
if [ ! -f "$TMP/${PKGNAME}-${VERSION}/packaging/piper_server.py" ]; then
  echo "警告: piper_server.py 未纳入 git 跟踪，已手动补入源码包"
  mkdir -p "$TMP/${PKGNAME}-${VERSION}/packaging"
  cp "$ROOT/packaging/piper_server.py" "$TMP/${PKGNAME}-${VERSION}/packaging/"
fi
# 未提交的 CI 工作流补入
if [ -d "$ROOT/.github" ] && [ ! -d "$TMP/${PKGNAME}-${VERSION}/.github" ]; then
  cp -r "$ROOT/.github" "$TMP/${PKGNAME}-${VERSION}/"
fi

tar -czf "$OUT" -C "$TMP" "${PKGNAME}-${VERSION}"
echo "完成: $OUT"
sha256sum "$OUT"
