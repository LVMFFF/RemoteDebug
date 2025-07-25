#!/bin/bash

# ======================================
# MinGW + CMake 构建脚本
# ======================================

# 设置 MinGW 路径 - 修改为您的实际路径
MINGW_PATH="D:/chrome/x86_64-13.2.0-release-posix-seh-ucrt-rt_v11-rev0/mingw64"

# 清理冲突环境变量
unset MAKE
unset MAKEFLAGS

# 将 MinGW 添加到 PATH
export PATH="${MINGW_PATH}/bin:$PATH"

echo "======================================="
echo "       构建脚本启动"
echo "======================================="
echo "MinGW 路径: ${MINGW_PATH}"
echo "当前 PATH: ${PATH}"
echo ""

# 验证工具链
echo "=== 验证编译器工具链 ==="
gcc --version || { echo "错误: gcc 未找到"; exit 1; }
g++ --version || { echo "错误: g++ 未找到"; exit 1; }
mingw32-make --version || { echo "错误: mingw32-make 未找到"; exit 1; }
echo ""

# 清理旧构建
echo "=== 清理构建目录 ==="
rm -rf build
mkdir -p build
cd build || { echo "错误: 无法进入 build 目录"; exit 1; }

# 使用 MinGW Makefiles
if [ ! -f CMakeCache.txt ]; then
    echo "=== 使用 MinGW Makefiles 生成器 ==="
    
    # 显式指定所有工具路径
    if cmake .. -G "MinGW Makefiles" \
        -DCMAKE_C_COMPILER="${MINGW_PATH}/bin/gcc.exe" \
        -DCMAKE_CXX_COMPILER="${MINGW_PATH}/bin/g++.exe" \
        -DCMAKE_MAKE_PROGRAM="${MINGW_PATH}/bin/mingw32-make.exe" \
        -DCMAKE_BUILD_TYPE=Release; then
        
        echo "=== CMake 配置成功 (MinGW Makefiles) ==="
        echo ""
        echo "=== 开始编译 (使用 Make) ==="
        "${MINGW_PATH}/bin/mingw32-make.exe" -j$(nproc)
        
        # 检查编译结果
        if [ $? -eq 0 ]; then
            echo ""
            echo "=== 编译成功 ==="
        else
            echo ""
            echo "错误: 编译失败"
            exit 1
        fi
    else
        echo ""
        echo "错误: CMake 配置失败"
        exit 1
    fi
fi

echo ""
echo "=== 查找生成的可执行文件 ==="
./RemoteDebug.exe