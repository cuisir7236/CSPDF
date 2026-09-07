#!/usr/bin/env bash
# ============================================================
# 一键重建（评审修复后）：安装构建依赖 -> 重新打包 deb
# 用法：sudo ./packaging/rebuild.sh
# 说明：
#  - 打包输入（声音引擎 /usr/share/cspdfreader）需已存在（本机已具备）
#  - 产物：dist/cspdfreader_1.0.2_amd64.deb
#          dist/release/SHA256SUMS.md + 分卷 .aa/.ab + merge.sh + 源码包
# ============================================================
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"

echo "==> 安装构建依赖（需 root 权限）"
apt-get update
apt-get install -y --no-install-recommends \
  cmake g++ pkg-config \
  qt6-base-dev libpoppler-qt6-dev libespeak-ng-dev

echo "==> 重新打包（架构自适应）"
"$ROOT/packaging/build_deb_selfcontained.sh"

echo
echo "==> 完成！校验："
sha256sum "$ROOT"/dist/cspdfreader_*_*.deb
echo "   校验文件: dist/release/SHA256SUMS.md"
