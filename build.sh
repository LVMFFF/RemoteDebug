#!/bin/bash

# 设置默认构建类型
BUILD_TYPE="${1:-Release}"

# 检测操作系统
OS_NAME="$(uname -s)"
case "${OS_NAME}" in
    Linux*)     OS=Linux;;
    MINGW*)     OS=MinGW;;
    CYGWIN*)    OS=Cygwin;;
    MSYS*)      OS=MSYS;;
    *)          OS="UNKNOWN:${OS_NAME}"
esac

echo "======================================="
echo "操作系统: ${OS}"
echo "构建类型: ${BUILD_TYPE}"
echo ""

# 清理旧构建
echo "=== 清理构建目录 ==="
rm -rf build
mkdir -p build
cd build || { echo "错误: 无法进入 build 目录"; exit 1; }

# 根据操作系统配置构建
if [[ "${OS}" == "MinGW" || "${OS}" == "MSYS" || "${OS}" == "Cygwin" ]]; then
    # 设置 MinGW 路径 - 修改为实际路径
    MINGW_PATH="D:/chrome/x86_64-13.2.0-release-posix-seh-ucrt-rt_v11-rev0/mingw64"
    echo "=== 使用 MinGW 工具链 ==="
    echo "MinGW 路径: ${MINGW_PATH}"

    export PATH="${MINGW_PATH}/bin:$PATH"

    echo "=== 验证编译器工具链 ==="
    gcc --version || { echo "错误: gcc 未找到"; exit 1; }
    g++ --version || { echo "错误: g++ 未找到"; exit 1; }
    mingw32-make --version || { echo "错误: mingw32-make 未找到"; exit 1; }
    echo ""
    
    echo "=== 生成构建系统 ==="
    cmake .. -G "MinGW Makefiles" \
        -DCMAKE_C_COMPILER="${MINGW_PATH}/bin/gcc.exe" \
        -DCMAKE_CXX_COMPILER="${MINGW_PATH}/bin/g++.exe" \
        -DCMAKE_MAKE_PROGRAM="${MINGW_PATH}/bin/mingw32-make.exe" \
        -DCMAKE_BUILD_TYPE=${BUILD_TYPE} || { echo "错误: CMake 配置失败"; exit 1; }
    
    echo ""
    echo "=== 开始编译 ==="
    mingw32-make.exe -j$(nproc) || { echo "错误: 编译失败"; exit 1; }
    
    # 查找可执行文件
    echo ""
    echo "=== 查找生成的可执行文件 ==="
    if [ -f "RemoteDebug.exe" ]; then
        echo "可执行文件: RemoteDebug.exe"
        echo ""
        echo "=== 运行程序 ==="
        ./RemoteDebug.exe
    else
        echo "错误: 未找到可执行文件 RemoteDebug.exe"
        exit 1
    fi
else
    # Linux 环境 (包括WSL)
    echo "=== 使用 Linux 工具链 ==="
    
    # 验证工具链
    echo "=== 验证编译器工具链 ==="
    gcc --version || { echo "错误: gcc 未找到"; exit 1; }
    g++ --version || { echo "错误: g++ 未找到"; exit 1; }
    make --version || { echo "错误: make 未找到"; exit 1; }
    cmake --version || { echo "错误: cmake 未找到"; exit 1; }
    echo ""
    
    # 使用 Unix Makefiles
    echo "=== 生成构建系统 ==="
    cmake .. -G "Unix Makefiles" \
        -DCMAKE_BUILD_TYPE=${BUILD_TYPE} || { echo "错误: CMake 配置失败"; exit 1; }
    
    echo ""
    echo "=== 开始编译 ==="
    make -j$(nproc) || { echo "错误: 编译失败"; exit 1; }
    
    # 查找可执行文件
    echo ""
    echo "=== 查找生成的可执行文件 ==="
    # 尝试通过find查找
    find . -executable -type f | while read file; do
        echo "发现可执行文件: ${file}"
        echo "=== 运行程序 ==="
        ${file}
    done
    if [ $? -ne 0 ]; then
        echo "错误: 未找到可执行文件"
        exit 1
    fi
fi