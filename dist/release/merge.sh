#!/usr/bin/env bash
# 合并分卷还原 deb 包（附 SHA256 校验）
set -e
cd "$(dirname "$0")"
cat cspdfreader_1.0.2_amd64.deb.* > cspdfreader_1.0.2_amd64.deb
echo "已生成 cspdfreader_1.0.2_amd64.deb"
echo "校验:"
sha256sum cspdfreader_1.0.2_amd64.deb
