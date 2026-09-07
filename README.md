# 老崔PDF阅读工具 (CSPDFREADER)

Deepin 25 托盘常驻的 PDF 跟随朗读工具，配合系统文档阅读器（deepin-reader，玲珑版）使用。AI 辅助开发的完整开源项目，全部代码可由 Claude 等 AI 助手生成与维护。

> **官方支持**：Deepin 25。**多架构**：打包脚本按 `dpkg --print-architecture` 自适应，支持 amd64 / arm64 / loong64 / riscv64（在对应架构主机上执行脚本即可；amd64 + arm64 由 CI 自动构建验证）。

## 功能
- 自动识别 deepin-reader 当前打开的 PDF（/proc/<pid>/fd 轮询，兼容玲珑容器）
- 从 deepin-reader 的 user.db 读取上次阅读位置，打开即续读
- **Piper TTS 神经语音朗读（中文少女音，离线可用）**，模型缺失自动降级 espeak-ng
- **流水线预合成**：播放当前页时后台合成下一页，页间无缝衔接
- 托盘"读"字图标 + 右下角悬浮控制面板：播放/暂停、上页/下页、手动跳页、三档语速
- 面板两行对齐布局，位置可拖动并记忆，右上角一键退出

## 技术栈
- C++17 + Qt6 (Widgets/Sql) + Poppler-Qt6 + espeak-ng
- Piper TTS (onnxruntime) 神经语音，中文 huayan-medium 少女音模型（60MB，离线）
- deb 仅含应用本体（约 41KB），运行时依赖走系统包，声音引擎由安装脚本自动安装

## 评审优化记录（2026-09-07，对应"新建文本.txt"建议）
1. **源码归档**：`packaging/piper_server.py` 已纳入 git 版本库；新增 `make_source_archive.sh` 生成含全部源码（含 piper_server.py）的 `-src.tar.gz`，满足 GPL Corresponding Source 可审计要求。
2. **版权声明**：deb 内置 `DEBIAN/copyright`，附 Qt6 / Poppler / espeak-ng / onnxruntime / numpy / piper 的版权与许可声明（Debian 机器可读格式）。
3. **多架构**：去除 `/usr/lib/x86_64-linux-gnu` 硬编码与 `Architecture=amd64` 限制，改为 `ARCH`/`MULTIARCH` 动态检测 + CMake `CMAKE_LIBRARY_ARCHITECTURE` 注入；支持 arm64 / loong64 / riscv64 构建。
4. **QProcess 线程安全**：TtsEngine 重构——aplay 播放进程仅在 worker 线程操作；Piper 常驻服务进程由专用 piper 线程独占，实时合成与预合成请求经互斥队列串行投递，消除跨线程并发访问。
5. **CI 与版本统一**：新增 `.github/workflows/ci.yml`（amd64 + arm64 构建、合规检查、tag 触发源码归档）；版本号唯一来源 = `CMakeLists.txt`（1.0.2），与 deb control 及 git tag `1.0.2`/`cspdf12` 对齐；Release 产物附 SHA256 校验（`dist/release/SHA256SUMS.md`）。

## 安装
### 安装 deb（Deepin 25 官方方案，开箱即用）
```bash
sudo apt install ./cspdfreader_1.0.2_amd64.deb
```
`packaging/build_deb_selfcontained.sh` **完全自包含**打包（约 133MB）：
- 内置全部运行时：Qt6/Poppler 动态库、Qt 平台与 SQLite 驱动、espeak-ng（库+语音数据）、Piper 中文模型、Python 运行时（piper/onnxruntime/numpy）——**纯离线、开箱即用，与系统组件零冲突**
- 其他发行版/架构：请使用源码构建，或在对应架构主机上运行打包脚本

### 方式二：源码构建（需自行准备声音引擎）
```bash
git clone <本仓库地址>
cd CSPDFREADER
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
./build/CSPDFREADER                        # 开发模式运行
./packaging/build_deb_selfcontained.sh     # 生成 Deepin 官方 deb（133MB，架构自适应）
```
源码构建运行要求系统已装：`espeak-ng espeak-ng-data`；如需 Piper 神经语音，还需 `pip3 install piper-tts onnxruntime numpy` 并将模型放入 `/usr/share/cspdfreader/piper/`（或 `~/.local/share/cspdfreader/piper/`）。

> **其他发行版 / arm64 / loong64 / riscv64**：直接源码构建（cmake），或在目标架构主机上运行 `ARCH=xxx ./packaging/build_deb_selfcontained.sh`（脚本自动检测架构并生成对应 deb）。

## 环境要求
- Deepin 25（x86_64 / AMD64，官方包）；arm64 / loong64 / riscv64 请自行构建
- 安装 deb 时需联网（仅首次安装声音引擎；之后完全离线）
- espeak-ng 兜底：即使无网络、无 Piper，功能不中断

## 卸载
```bash
sudo apt purge cspdfreader
```

## 目录结构
```
src/                  C++ 源码（6 模块：watcher/docmanager/tts/panel/tray/main）
packaging/            deb 打包脚本 + piper_server.py + make_source_archive.sh
docs/                 设计文档 v0.4
.github/workflows/    CI（amd64+arm64 构建、合规检查、源码归档）
dist/                 构建产物（deb 包 + release/SHA256SUMS.md + 分卷 + 源码包）
```

## 开源协议
GPL-3.0-or-later
