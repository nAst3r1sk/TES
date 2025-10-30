#!/bin/bash

# 构建脚本
set -e

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"

echo "=== Building Binance WebSocket API ==="
echo "Project Root: ${PROJECT_ROOT}"
echo "Build Directory: ${BUILD_DIR}"

# 创建构建目录
if [ ! -d "${BUILD_DIR}" ]; then
    echo "Creating build directory..."
    mkdir -p "${BUILD_DIR}"
fi

cd "${BUILD_DIR}"

# 配置项目
echo "Configuring project..."
cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="${PROJECT_ROOT}/install"

# 编译项目
echo "Building project..."
make -j$(nproc)

echo "Build completed successfully!"
echo "Executable: ${BUILD_DIR}/bin/BinanceWebSocketAPI"

# 复制配置文件到构建目录
if [ ! -d "${BUILD_DIR}/config" ]; then
    echo "Copying config files..."
    cp -r "${PROJECT_ROOT}/config" "${BUILD_DIR}/"
fi

echo "Ready to run: cd ${BUILD_DIR} && ./bin/BinanceWebSocketAPI"