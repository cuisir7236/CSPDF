cmake_minimum_required(VERSION 3.16)
# 评审 #5：统一版本号 —— CMake / deb control / git tag 1.0.2 对齐（唯一版本源）
project(CSPDFREADER VERSION 1.0.2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Sql)
find_package(PkgConfig REQUIRED)
pkg_check_modules(POPPLER REQUIRED IMPORTED_TARGET poppler-qt6)
pkg_check_modules(ESPEAK REQUIRED IMPORTED_TARGET espeak-ng)

add_executable(CSPDFREADER
    src/main.cpp
    src/documentwatcher.cpp
    src/documentwatcher.h
    src/docmanager.cpp
    src/docmanager.h
    src/ttsengine.cpp
    src/ttsengine.h
    src/panel.cpp
    src/panel.h
    src/trayicon.cpp
    src/trayicon.h
)

target_include_directories(CSPDFREADER PRIVATE src)

# 评审 #3：按多架构目录注入 espeak-ng 数据路径（CMAKE_LIBRARY_ARCHITECTURE
# 在 Debian/Deepin 多架构系统上自动为 x86_64-linux-gnu / aarch64-linux-gnu 等），
# 消除 /usr/lib/x86_64-linux-gnu 硬编码，arm64/loong64/riscv64 交叉构建时由
# 工具链文件提供 CMAKE_LIBRARY_ARCHITECTURE 即可。
if(CMAKE_LIBRARY_ARCHITECTURE)
    target_compile_definitions(CSPDFREADER PRIVATE
        ESPEAK_DATA_DIR="/usr/lib/${CMAKE_LIBRARY_ARCHITECTURE}")
endif()

target_link_libraries(CSPDFREADER PRIVATE
    Qt6::Widgets
    Qt6::Sql
    PkgConfig::POPPLER
    PkgConfig::ESPEAK
)
