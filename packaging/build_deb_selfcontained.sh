#!/usr/bin/env bash
# ============================================================
# 官方打包脚本（Deepin 官方方案，支持多架构）
# 自包含 deb：应用 + 声音引擎 + 运行时库全部内置，纯离线开箱即用。
#
# 评审修复（新建文本.txt）：
#   #1 源码归档：make_source_archive.sh 生成含 piper_server.py 的源码包，
#      满足 GPL Corresponding Source 可审计要求
#   #2 生成 DEBIAN/copyright：Qt6/Poppler/espeak-ng/onnxruntime/numpy/piper
#      三方版权与许可声明
#   #3 去除 /usr/lib/x86_64-linux-gnu 硬编码与 Architecture=amd64 限制：
#      ARCH/MULTIARCH 动态检测，支持 amd64/arm64/loong64/riscv64
#   #5 统一版本号（CMake/control/deb 文件名均取自 CMakeLists VERSION，
#      与 git tag 1.0.2 / cspdf12 对齐）；Release 附 SHA256 校验
#   #6 pylib 打包源改为实际安装目录 /usr/share/cspdfreader/pylib
# ============================================================
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PKGNAME="cspdfreader"

# ---- #5 版本号唯一来源：CMakeLists.txt，避免 CMake/control 不一致 ----
VERSION="$(sed -n 's/^project(CSPDFREADER VERSION \([0-9.]*\).*/\1/p' "$ROOT/CMakeLists.txt" | head -1)"
[ -n "$VERSION" ] || VERSION="1.0.2"

# ---- #3 架构自适应（可用 ARCH 环境变量覆盖，便于交叉构建） ----
ARCH="${ARCH:-$(dpkg --print-architecture)}"
MULTIARCH="$(dpkg-architecture -qDEB_HOST_MULTIARCH 2>/dev/null || true)"
[ -n "$MULTIARCH" ] || MULTIARCH="${ARCH}-linux-gnu"
DEB="$ROOT/dist/${PKGNAME}_${VERSION}_${ARCH}.deb"

BUILD_DIR="$ROOT/build"
STAGE="$BUILD_DIR/package"
LIBDIR="usr/lib/$PKGNAME/lib"
PLUGINDIR="usr/lib/$PKGNAME/qt6/plugins"
SHARE="usr/share/$PKGNAME"

# 声音引擎打包输入：
#  1) 优先使用系统安装目录 /usr/share/cspdfreader（安装过旧版 deb 即有）
#  2) 缺失时自动从既有 deb 提取（自举），保证可重复构建、无需预装
SYS_PLUGINS="/usr/lib/$MULTIARCH/qt6/plugins"

echo "==> 目标架构: $ARCH (multiarch: $MULTIARCH)  版本: $VERSION"
echo "==> 准备输出目录"
mkdir -p "$ROOT/dist"

SRC_SOUND="/usr/share/cspdfreader"
if [ ! -d "$SRC_SOUND" ]; then
  echo "==> /usr/share/cspdfreader 不存在，尝试从既有 deb 提取声音引擎（自举）"
  BOOT_DEB="$(ls -1 "$ROOT"/dist/cspdfreader_*.deb 2>/dev/null | head -1 || true)"
  if [ -z "$BOOT_DEB" ]; then
    echo "错误: 缺少声音引擎输入，且 dist/ 中无既有 deb 可供提取。"
    echo "请先安装旧版 deb（sudo apt install ./dist/*.deb）或手动放置 /usr/share/cspdfreader。"
    exit 1
  fi
  BOOT_DIR="$BUILD_DIR/sound-bootstrap"
  rm -rf "$BOOT_DIR"
  mkdir -p "$BOOT_DIR"
  dpkg-deb -x "$BOOT_DEB" "$BOOT_DIR"
  SRC_SOUND="$BOOT_DIR/usr/share/cspdfreader"
  echo "   已从 $BOOT_DEB 提取声音引擎"
fi

echo "==> 校验声音引擎输入"
for f in "$SRC_SOUND/piper/zh_CN-huayan-medium.onnx" \
         "$SRC_SOUND/piper/zh_CN-huayan-medium.onnx.json" \
         "$SRC_SOUND/piper_server.py" \
         "$SRC_SOUND/espeak-ng-data/voices/!v/f5" \
         "$SYS_PLUGINS/sqldrivers/libqsqlite.so"; do
  [ -f "$f" ] || { echo "缺少打包输入: $f"; exit 1; }
done

echo "==> 构建 Release"
cmake -S "$ROOT" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build "$BUILD_DIR" -j"$(nproc)" >/dev/null

echo "==> 组装 deb 目录"
rm -rf "$STAGE"
mkdir -p "$STAGE/usr/bin" \
         "$STAGE/usr/share/applications" \
         "$STAGE/usr/share/icons/hicolor/64x64/apps" \
         "$STAGE/$LIBDIR" \
         "$STAGE/$PLUGINDIR/platforms" \
         "$STAGE/$PLUGINDIR/platforminputcontexts" \
         "$STAGE/$PLUGINDIR/sqldrivers" \
         "$STAGE/$SHARE/piper" \
         "$STAGE/DEBIAN"

# ---------- 1. 应用二进制 ----------
cp "$BUILD_DIR/CSPDFREADER" "$STAGE/usr/bin/$PKGNAME"

# ---------- 2. 递归收集全部动态库闭包（排除核心系统库，避免 ABI 冲突） ----------
echo "==> 递归收集动态库依赖闭包 ..."
collect() {   # $1=文件, 输出其所有动态依赖绝对路径
  ldd "$1" 2>/dev/null | awk '/=> \//{print $3}'
}
seen=()
queue=("$BUILD_DIR/CSPDFREADER")
while [ ${#queue[@]} -gt 0 ]; do
  f="${queue[0]}"; queue=("${queue[@]:1}")
  [ -f "$f" ] || continue
  while IFS= read -r dep; do
    case "$dep" in
      */libc.so.6|*/libm.so.6|*/libdl.so.2|*/libpthread.so.0|*/librt.so.1|*/libutil.so.1|*/libresolv.so.2|*/ld-linux*|*/libstdc++.so.6|*/libgcc_s.so.1|*/libnss_*|*/libanl.so.1) continue ;;
    esac
    if ! printf '%s\n' "${seen[@]}" | grep -qxF "$dep"; then
      seen+=("$dep")
      queue+=("$dep")   # 递归：该库的依赖也收集
    fi
  done <<< "$(collect "$f")"
done
echo "  共收集 $((${#seen[@]})) 个动态库"
for lib in "${seen[@]}"; do
  [ -f "$lib" ] && cp -L -f "$lib" "$STAGE/$LIBDIR/"
done
# ---------- 3. Qt 平台插件 + 输入法 + SQL 驱动 ----------
cp "$SYS_PLUGINS/platforms/libdxcb.so" "$STAGE/$PLUGINDIR/platforms/" 2>/dev/null || \
  cp "$SYS_PLUGINS/platforms/libqxcb.so" "$STAGE/$PLUGINDIR/platforms/"
cp -r "$SYS_PLUGINS/platforminputcontexts/." "$STAGE/$PLUGINDIR/platforminputcontexts/" 2>/dev/null || true
cp "$SYS_PLUGINS/sqldrivers/libqsqlite.so" "$STAGE/$PLUGINDIR/sqldrivers/"

# ---------- 4. 声音引擎：Piper 模型 + 服务脚本 + espeak-ng 清洗数据 ----------
cp "$SRC_SOUND/piper/zh_CN-huayan-medium.onnx" "$STAGE/$SHARE/piper/"
cp "$SRC_SOUND/piper/zh_CN-huayan-medium.onnx.json" "$STAGE/$SHARE/piper/"
cp "$SRC_SOUND/piper_server.py" "$STAGE/$SHARE/"
cp -r "$SRC_SOUND/espeak-ng-data" "$STAGE/$SHARE/"

# ---------- 5. Piper Python 运行时（优先取已打包目录，缺失再回退 pip 目录） ----------
echo "==> 精简并复制 Piper Python 运行时 ..."
PYLIBDIR="$SHARE/pylib"
mkdir -p "$STAGE/$PYLIBDIR"
if [ -d "$SRC_SOUND/pylib" ]; then
  cp -r "$SRC_SOUND/pylib/." "$STAGE/$PYLIBDIR/"
else
  SITE="$HOME/.local/lib/python3.12/site-packages"
  for pkg in piper onnxruntime onnxruntime/capi numpy numpy/core numpy/lib numpy/fft numpy.libs \
             flatbuffers protobuf packaging pyparsing colorama; do
    [ -e "$SITE/$pkg" ] && cp -r "$SITE/$pkg" "$STAGE/$PYLIBDIR/" || true
  done
fi
find "$STAGE/$PYLIBDIR" -type d -name "__pycache__" -exec rm -rf {} + 2>/dev/null || true
rm -rf "$STAGE/$PYLIBDIR/piper/train" 2>/dev/null || true
# onnxruntime 精简：移除 GPU/CUDA 等非 CPU provider（CPU 推理不需要）
find "$STAGE/$PYLIBDIR/onnxruntime/capi" -maxdepth 1 \
     \( -name "*cuda*" -o -name "*tensorrt*" -o -name "*dnnl*" \) -exec rm -f {} + 2>/dev/null || true

# ---------- 6. 启动脚本（统一入口：库路径/插件/PYTHONPATH） ----------
cat > "$STAGE/usr/bin/$PKGNAME-launcher.sh" <<LAUNCH
#!/usr/bin/env bash
# CSPDFReader 启动器（自包含版）：设置内置库与 Python 环境后 exec 二进制
export LD_LIBRARY_PATH="/usr/lib/$PKGNAME/lib:\${LD_LIBRARY_PATH:-}"
export QT_PLUGIN_PATH="/usr/lib/$PKGNAME/qt6/plugins"
export QT_QPA_PLATFORM="dxcb"
export PYTHONPATH="/usr/share/$PKGNAME/pylib:\${PYTHONPATH:-}"
exec /usr/bin/$PKGNAME "\$@"
LAUNCH
chmod +x "$STAGE/usr/bin/$PKGNAME-launcher.sh"

# ---------- 7. desktop / 图标 ----------
cat > "$STAGE/usr/share/applications/$PKGNAME.desktop" <<DESK
[Desktop Entry]
Type=Application
Name=老崔PDF阅读工具
Name[en]=CSPDFReader
Comment=Deepin 文档阅读器 PDF 跟随朗读工具
Exec=$PKGNAME-launcher.sh
Icon=$PKGNAME
Terminal=false
Categories=Utility;Audio;
DESK

python3 - "$STAGE/usr/share/icons/hicolor/64x64/apps/$PKGNAME.png" <<'PY'
import struct, sys, zlib
out = sys.argv[1]
W = H = 64
rows = []
def px(x, y):
    cx, cy = x - 2, y - 2
    if 0 <= cx < 60 and 0 <= cy < 60:
        return (30, 120, 230, 255)
    return (0, 0, 0, 0)
glyph = [
    "..##....", "..##....", "..#.....", "..#.##..",
    "..##..#.", ".#...#..", ".#...#..", "..#.....",
]
for y in range(H):
    row = b'\x00'
    for x in range(W):
        r, g, b, a = px(x, y)
        gx, gy = x - 22, y - 22
        if 0 <= gx < 8 and 0 <= gy < 8 and glyph[gy][gx] == '#':
            r, g, b, a = 255, 255, 255, 255
        row += bytes((r, g, b, a))
    rows.append(row)
raw = b''.join(rows)
def chunk(t, d):
    c = t + d
    return struct.pack('>I', len(d)) + c + struct.pack('>I', zlib.crc32(c) & 0xffffffff)
png = b'\x89PNG\r\n\x1a\n'
png += chunk(b'IHDR', struct.pack('>IIBBBBB', W, H, 8, 6, 0, 0, 0))
png += chunk(b'IDAT', zlib.compress(raw))
png += chunk(b'IEND', b'')
open(out, 'wb').write(png)
PY
# ---------- 8. control（#3：Architecture 动态化） ----------
cat > "$STAGE/DEBIAN/control" <<CTL
Package: $PKGNAME
Version: $VERSION
Section: utils
Priority: optional
Architecture: $ARCH
Depends: libc6, libstdc++6, libgcc-s1, python3 (>= 3.12), alsa-utils
Maintainer: Lao Cui <cspdfreader@example.com>
Description: Deepin PDF reader companion with page-by-page TTS reading
 托盘常驻的 PDF 跟随朗读工具，配合 deepin-reader 使用。
 自包含版：Piper 模型/运行时/全部动态库内置，安装即用、无需联网。
CTL

# ---------- 9. copyright（#2：三方版权与许可声明，Debian 机器可读格式） ----------
cat > "$STAGE/DEBIAN/copyright" <<'CPY'
Format: https://www.debian.org/doc/packaging-manuals/copyright-format/1.0/
Upstream-Name: cspdfreader
Upstream-Contact: Lao Cui <cspdfreader@example.com>
Source: https://github.com/CSPDFREADER

Disclaimer: 本包为自包含分发（应用 + 三方动态库 + Python 运行时内置），
 以下第三方组件随包以二进制形式分发；其完整源代码见各上游项目，
 本文件作为 GPL Corresponding Source 的版权与许可声明（评审 #2）。

Files: *
Copyright: 2026 Lao Cui <cspdfreader@example.com>
License: GPL-3.0-or-later

Files: usr/lib/cspdfreader/lib/libQt6*.so*
Copyright: 2026 The Qt Company Ltd.
License: LGPL-3.0-or-later
Comment: Qt6 基础库（Widgets/Sql 及 Qt 平台/SQLite 驱动插件）。
 上游: https://www.qt.io / https://code.qt.io

Files: usr/lib/cspdfreader/lib/libpoppler*.so*
Copyright: 2002-2026 Poppler authors
License: GPL-2.0-or-later
Comment: Poppler PDF 渲染库（Poppler-Qt6 绑定）。
 上游: https://poppler.freedesktop.org

Files: usr/lib/cspdfreader/lib/libespeak-ng*.so*
       usr/share/cspdfreader/espeak-ng-data/*
Copyright: 2002-2026 espeak-ng contributors
License: GPL-3.0-or-later
Comment: espeak-ng 语音合成库与语音数据。
 上游: https://github.com/espeak-ng/espeak-ng

Files: usr/share/cspdfreader/pylib/onnxruntime/*
Copyright: 2017-2026 Microsoft Corporation
License: MIT
Comment: onnxruntime 推理引擎（CPU）。
 上游: https://github.com/microsoft/onnxruntime

Files: usr/share/cspdfreader/pylib/numpy/*
Copyright: 2005-2026 NumPy developers
License: BSD-3-Clause
Comment: 上游: https://numpy.org

Files: usr/share/cspdfreader/pylib/piper/*
       usr/share/cspdfreader/piper/*
Copyright: 2022-2026 Piper contributors
License: MIT
Comment: Piper TTS 神经语音合成器与 zh_CN-huayan-medium 中文模型。
 上游: https://github.com/rhasspy/piper

Files: usr/share/cspdfreader/piper_server.py
Copyright: 2026 Lao Cui <cspdfreader@example.com>
License: GPL-3.0-or-later
Comment: 本项目自有脚本（随包分发，可审计）。

License: GPL-3.0-or-later
 This program is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.
 .
 On Debian/Deepin systems, the complete text of the GNU GPL v3 can be
 found in /usr/share/common-licenses/GPL-3.

License: LGPL-3.0-or-later
 This library is free software: you can redistribute it and/or modify
 it under the terms of the GNU Lesser General Public License as
 published by the Free Software Foundation, either version 3 of the
 License, or (at your option) any later version.
 .
 On Debian/Deepin systems, the complete text of the GNU LGPL v3 can be
 found in /usr/share/common-licenses/LGPL-3.

License: GPL-2.0-or-later
 This program is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 2 of the License, or
 (at your option) any later version.
 .
 On Debian/Deepin systems, the complete text of the GNU GPL v2 can be
 found in /usr/share/common-licenses/GPL-2.

License: MIT
 Permission is hereby granted, free of charge, to any person obtaining a
 copy of this software and associated documentation files (the
 "Software"), to deal in the Software without restriction, including
 without limitation the rights to use, copy, modify, merge, publish,
 distribute, sublicense, and/or sell copies of the Software.
 .
 On Debian/Deepin systems, the complete text of the MIT license can be
 found in /usr/share/common-licenses/Expat.

License: BSD-3-Clause
 Redistribution and use in source and binary forms, with or without
 modification, are permitted provided that the following conditions are
 met: redistribution of source code retains the above copyright notice,
 redistribution in binary form reproduces the notice, and neither the
 name of the copyright holder nor the names of its contributors may be
 used to endorse or promote products derived from this software without
 specific prior written permission.
 .
 On Debian/Deepin systems, the complete text of the BSD license can be
 found in /usr/share/common-licenses/BSD.
CPY

echo "==> 打包"
dpkg-deb --build --root-owner-group "$STAGE" "$DEB"

# ---------- 10. Release 产物：SHA256 校验 + 分卷 + 合并脚本（#5） ----------
echo "==> 生成 Release SHA256 校验"
mkdir -p "$ROOT/dist/release"
SHA256="$(sha256sum "$DEB" | awk '{print $1}')"
cat > "$ROOT/dist/release/SHA256SUMS.md" <<EOF
# CSPDFREADER Release $VERSION ($ARCH)

- 文件: dist/${PKGNAME}_${VERSION}_${ARCH}.deb
- SHA256: $SHA256
- 生成时间: $(date '+%Y-%m-%d %H:%M:%S %Z')
- 对应 git tag: 1.0.2 / cspdf12

\`\`\`
$(sha256sum "$DEB")
\`\`\`
EOF

echo "==> 分卷（GitHub 附件 100MB 限制）"
rm -f "$ROOT/dist/release/${PKGNAME}_${VERSION}_${ARCH}.deb."*
split -b 90m -a 2 "$DEB" "$ROOT/dist/release/${PKGNAME}_${VERSION}_${ARCH}.deb."

cat > "$ROOT/dist/release/merge.sh" <<MERG
#!/usr/bin/env bash
# 合并分卷还原 deb 包（附 SHA256 校验）
set -e
cd "\$(dirname "\$0")"
cat ${PKGNAME}_${VERSION}_${ARCH}.deb.* > ${PKGNAME}_${VERSION}_${ARCH}.deb
echo "已生成 ${PKGNAME}_${VERSION}_${ARCH}.deb"
echo "校验:"
sha256sum ${PKGNAME}_${VERSION}_${ARCH}.deb
MERG
chmod +x "$ROOT/dist/release/merge.sh"

# ---------- 11. 源码归档（#1：GPL Corresponding Source，含 piper_server.py） ----------
echo "==> 生成源码归档"
"$ROOT/packaging/make_source_archive.sh" "$VERSION"

echo "完成: $DEB"
echo "校验文件: dist/release/SHA256SUMS.md"
echo "分卷: dist/release/${PKGNAME}_${VERSION}_${ARCH}.deb.aa / .ab（merge.sh 还原）"
echo "源码包: dist/release/${PKGNAME}-${VERSION}-src.tar.gz"
echo "提示: 官方 Deepin 自包含包，纯离线开箱即用；其他发行版请源码自行构建"
