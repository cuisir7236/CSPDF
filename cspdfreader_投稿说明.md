# 【deepin插件开发活动】老崔PDF阅读工具：Deepin 25 文档阅读器 Piper 语音跟随朗读（v1.0.2）

## 项目简介
一款托盘常驻的 PDF 跟随朗读工具，配合 Deepin 25 系统文档阅读器（deepin-reader，玲珑版）使用：
自动识别当前打开的 PDF、从上次阅读位置续读、Piper 神经语音逐页朗读（中文少女音），纯本地离线、无需联网。

- **参赛方向**：桌面效率工具（PDF 朗读助手）
- **使用 Skill**：Qt6/C++ 桌面开发、Poppler、Piper TTS
- **开发工具**：UOS AI 助手（小U同学）
- **开源协议**：GPL-3.0-or-later
- **GitHub**：https://github.com/cuisir7236/CSPDF

## 这是什么？
灵感来自"看 PDF 看到眼睛酸，想让它读出来"：
- **自动跟随**：deepin-reader 打开 PDF → 托盘工具自动识别文档（/proc fd 轮询，兼容玲珑容器）
- **位置续读**：从 deepin-reader 的 user.db 读取上次阅读位置，打开即续读
- **少女音朗读**：Piper TTS 神经语音（huayan-medium 中文女声模型），声音自然清亮、无回音
- **无缝衔接**：流水线预合成机制，播放当前页时后台合成下一页，页间无停顿
- **简洁面板**：右下角悬浮圆角卡片，两行布局（上页/播放/下页 + 页码框/语速），可拖动记忆位置
- **托盘标识**：蓝底白"读"字图标，与其他应用明显区分

## 技术亮点
1. **全自包含打包**（约 133MB deb）：Qt6/Poppler/espeak-ng（库+语音数据）/Piper 模型/Python 运行时（piper/onnxruntime/numpy）全部内置，**纯离线开箱即用，无需联网下载，也无需手工安装声音引擎**
2. **双引擎**：Piper 神经语音优先，模型缺失自动降级 espeak-ng，功能不中断
3. **C++17 + Qt6** 纯本地实现，无云依赖，断网可用

## 评审优化（v1.0.2，针对上架审核建议）
- **源码归档**：piper_server.py 已纳入版本库，随源码包一起发布，满足 GPL Corresponding Source 可审计要求
- **版权合规**：deb 内置 DEBIAN/copyright，附 Qt6/Poppler/espeak-ng/onnxruntime/numpy/piper 三方版权与许可声明（Debian 机器可读格式）
- **多架构**：去除 x86_64 硬编码与 amd64 限制，支持 arm64/loong64/riscv64 构建（架构自适应打包脚本）
- **线程安全**：修复 TtsEngine 播放/合成 QProcess 跨线程并发访问，改为同线程+加锁安全管理，运行更稳定
- **CI 与发布规范**：GitHub Actions 自动构建（amd64+arm64）；版本号统一 1.0.2；Release 附 SHA256 校验

## 安装
### 方式一：直接安装 deb（推荐，开箱即用）
```bash
sudo apt install ./cspdfreader_1.0.2_amd64.deb
```
（附件：cspdfreader_1.0.2_amd64.deb，约 133MB 全自包含，espeak-ng/Piper 均已内置，安装后无需手工装声音引擎）

### 方式二：源码构建
```bash
git clone https://github.com/cuisir7236/CSPDF.git CSPDFREADER
cd CSPDFREADER
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
./build/CSPDFREADER
```
（附件：cspdfreader-1.0.2-src.tar.gz 含全部源码、打包脚本（含 piper_server.py）、设计文档、CI 配置）

## 环境要求
- Deepin 25（x86_64 / AMD64，官方包）；arm64 / loong64 / riscv64 可源码构建或架构自适应打包
- 依赖：libc6, libstdc++6, libgcc-s1, python3 >= 3.12（系统自带）+ alsa-utils
- 配合 deepin-reader 使用；也支持手动打开任意 PDF
- 若系统缺少 espeak-ng 也可运行（包内已内置）；如需系统级兜底可执行 `sudo apt install libespeak-ng1`

## 卸载
```bash
sudo apt purge cspdfreader
```

## 截图
- 控制面板：右下角悬浮两行布局（[上页][播放][下页] / [页码框][语速]），右上角 ✕ 退出
- 托盘图标：蓝底白"读"字（展开任务栏托盘可见）

## 附件清单
| 文件 | 说明 |
|---|---|
| cspdfreader_1.0.2_amd64.deb | 安装包（约 133MB 全自包含） |
| cspdfreader-1.0.2-src.tar.gz | 完整源码（含 README/设计文档/打包脚本/CI） |
| SHA256SUMS.md | Release SHA256 校验（5eea8e8c…） |
