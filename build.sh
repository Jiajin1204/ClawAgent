#!/bin/bash

# ClawAgent 编译脚本
# 支持多平台编译: android, linux

set -e

# 颜色输出
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# 默认设置
PLATFORM="linux"
BUILD_TYPE="Release"
BUILD_TESTS="OFF"
CLEAN=""

# 解析参数
usage() {
    echo "用法: ./build.sh [选项] [平台]"
    echo ""
    echo "平台:"
    echo "  android          编译 Android APK/JNI 库"
    echo "  linux            编译 Linux 可执行文件 (默认)"
    echo ""
    echo "选项:"
    echo "  -c, --clean      编译前清理"
    echo "  -d, --debug      Debug 模式"
    echo "  -t, --tests      编译测试"
    echo "  -h, --help       显示帮助"
    echo ""
    echo "示例:"
    echo "  ./build.sh linux          # 编译 Linux 版本"
    echo "  ./build.sh android        # 编译 Android 版本"
    echo "  ./build.sh -c -t linux    # 清理后编译 Linux 版本并包含测试"
    exit 1
}

log_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

log_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# 解析命令行参数
while [[ $# -gt 0 ]]; do
    case $1 in
        android|linux)
            PLATFORM="$1"
            shift
            ;;
        -c|--clean)
            CLEAN="yes"
            shift
            ;;
        -d|--debug)
            BUILD_TYPE="Debug"
            shift
            ;;
        -t|--tests)
            BUILD_TESTS="ON"
            shift
            ;;
        -h|--help)
            usage
            ;;
        *)
            log_error "未知参数: $1"
            usage
            ;;
    esac
done

# 获取脚本目录
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# 创建构建目录
BUILD_DIR="$SCRIPT_DIR/build/$PLATFORM"

log_info "开始编译 ClawAgent ($PLATFORM, $BUILD_TYPE)"

# 创建构建目录
mkdir -p "$BUILD_DIR"

# 清理
if [ "$CLEAN" = "yes" ]; then
    log_info "清理构建目录..."
    rm -rf "$BUILD_DIR"
    mkdir -p "$BUILD_DIR"
fi

# Android 编译配置
if [ "$PLATFORM" = "android" ]; then
    log_info "Android 编译需要 Android NDK"
    log_info "请确保设置 ANDROID_NDK_HOME 环境变量"

    # 检查 NDK
    if [ -z "$ANDROID_NDK_HOME" ]; then
        if [ -d "$HOME/Android/Sdk/ndk" ]; then
            ANDROID_NDK_HOME="$HOME/Android/Sdk/ndk"
        elif [ -d "/opt/android-ndk" ]; then
            ANDROID_NDK_HOME="/opt/android-ndk"
        else
            log_error "未找到 Android NDK，请设置 ANDROID_NDK_HOME"
            exit 1
        fi
    fi

    log_info "使用 NDK: $ANDROID_NDK_HOME"

    # 获取 NDK 版本（可能是目录名或完整路径）
    if [ -d "$ANDROID_NDK_HOME/android-ndk-r27d" ]; then
        NDK_VERSION="android-ndk-r27d"
    else
        NDK_VERSION=$(ls "$ANDROID_NDK_HOME" | tail -1)
    fi
    log_info "NDK 版本: $NDK_VERSION"

    # 设置 toolchain
    ANDROID_TOOLCHAIN="$ANDROID_NDK_HOME/$NDK_VERSION/toolchains/llvm/prebuilt/linux-x86_64"

    if [ ! -d "$ANDROID_TOOLCHAIN" ]; then
        ANDROID_TOOLCHAIN="$ANDROID_NDK_HOME/$NDK_VERSION/toolchains/llvm/prebuilt/darwin-x86_64"
        if [ ! -d "$ANDROID_TOOLCHAIN" ]; then
            log_error "未找到 Android toolchain"
            exit 1
        fi
    fi

    # Android ABI 配置
    ANDROID_ABI="arm64-v8a"
    ANDROID_API=24

    log_info "Android ABI: $ANDROID_ABI, API: $ANDROID_API"

    # CMake 工具链文件
    TOOLCHAIN_FILE="$ANDROID_NDK_HOME/$NDK_VERSION/build/cmake/android.toolchain.cmake"

    if [ ! -f "$TOOLCHAIN_FILE" ]; then
        log_error "未找到 CMake 工具链文件: $TOOLCHAIN_FILE"
        exit 1
    fi

    # 检查是否有预编译的 libcurl
    CURL_FOUND="OFF"
    # 首先检查 /tmp/curl-install (我们编译的版本)
    if [ -f "/tmp/curl-install/lib/libcurl.a" ]; then
        CURL_LIBRARY="/tmp/curl-install/lib/libcurl.a"
        CURL_INCLUDE_DIR="/tmp/curl-install/include"
        # Android zlib 静态库路径 (arm64-v8a)
        ZLIB_LIBRARY="/home/jason/android-ndk-r27d/android-ndk-r27d/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/lib/aarch64-linux-android/libz.a"
        ZLIB_INCLUDE_DIR="/home/jason/android-ndk-r27d/android-ndk-r27d/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/include"
        CURL_FOUND="ON"
        log_info "使用编译的 libcurl: /tmp/curl-install"
    elif [ -f "$ANDROID_NDK_HOME/$NDK_VERSION/prebuilt/linux-x86_64/lib/libcurl.so" ]; then
        CURL_LIBRARY="$ANDROID_NDK_HOME/$NDK_VERSION/prebuilt/linux-x86_64/lib/libcurl.so"
        CURL_INCLUDE_DIR="$ANDROID_NDK_HOME/$NDK_VERSION/prebuilt/linux-x86_64/include"
        CURL_FOUND="ON"
        log_info "使用预编译的 libcurl"
    else
        log_warn "未找到预编译的 libcurl，Android 编译将不包含网络功能"
    fi

    # 配置 CMake
    log_info "配置 CMake for Android..."
    if [ "$CURL_FOUND" = "ON" ]; then
        cmake -B "$BUILD_DIR" \
            -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN_FILE" \
            -DANDROID_ABI="$ANDROID_ABI" \
            -DANDROID_PLATFORM=android-$ANDROID_API \
            -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
            -DBUILD_TESTS="$BUILD_TESTS" \
            -DCMAKE_MAKE_PROGRAM=make \
            -DCURL_LIBRARY="$CURL_LIBRARY" \
            -DCURL_INCLUDE_DIR="$CURL_INCLUDE_DIR" \
            -DZLIB_LIBRARY="$ZLIB_LIBRARY" \
            -DZLIB_INCLUDE_DIR="$ZLIB_INCLUDE_DIR"
    else
        # 不使用 curl（适用于纯框架编译）
        cmake -B "$BUILD_DIR" \
            -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN_FILE" \
            -DANDROID_ABI="$ANDROID_ABI" \
            -DANDROID_PLATFORM=android-$ANDROID_API \
            -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
            -DBUILD_TESTS="$BUILD_TESTS" \
            -DCMAKE_MAKE_PROGRAM=make \
            -DNO_CURL=ON
    fi

    MAKE_TARGET="clawagent"

# Linux 编译配置
elif [ "$PLATFORM" = "linux" ]; then
    log_info "配置 CMake for Linux..."

    # 检查依赖
    if ! command -v cmake &> /dev/null; then
        log_error "cmake 未安装"
        exit 1
    fi

    if ! command -v curl-config &> /dev/null; then
        log_warn "libcurl 开发库可能未安装"
    fi

    # 配置 CMake
    cmake -B "$BUILD_DIR" \
        -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
        -DBUILD_TESTS="$BUILD_TESTS" \
        -G "Unix Makefiles"

    MAKE_TARGET="clawagent"

    if [ "$BUILD_TESTS" = "ON" ]; then
        MAKE_TARGET="clawagent clawagent_tests"
    else
        MAKE_TARGET="clawagent"
    fi
else
    log_error "不支持的平台: $PLATFORM"
    usage
fi

# 编译
log_info "编译中..."
cd "$BUILD_DIR"

if command -v ninja &> /dev/null; then
    ninja $MAKE_TARGET
else
    make -j$(nproc) $MAKE_TARGET
fi

# 完成
log_info "编译完成!"
log_info "产物目录: $BUILD_DIR"

if [ "$PLATFORM" = "linux" ]; then
    if [ "$BUILD_TESTS" = "ON" ]; then
        log_info "可执行文件: $BUILD_DIR/clawagent"
        log_info "测试程序: $BUILD_DIR/clawagent_tests"
    else
        log_info "可执行文件: $BUILD_DIR/clawagent"
    fi
elif [ "$PLATFORM" = "android" ]; then
    log_info "JNI 库: $BUILD_DIR/libclawagent.so"
fi

# 运行测试
if [ "$BUILD_TESTS" = "ON" ] && [ "$PLATFORM" = "linux" ]; then
    log_info ""
    log_info "运行测试..."
    "$BUILD_DIR/clawagent_tests"
fi
