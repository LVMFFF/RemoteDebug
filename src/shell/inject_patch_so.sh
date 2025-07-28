#!/bin/bash

if [ $# -ne 2 ]; then
    echo "Usage: $0 <PID> <PATCH_PATH>"
    exit 1;
fi

PID=$1
PATCH_PATH=$(realpath "$2") # 使用绝对路径

if ! ps -p "$PID" > /dev/null; then
    echo "Error: Process $PID not found"
    exit 1
fi

# 验证动态库存在
if [ ! -f "$LIB_PATH" ]; then
    echo "Error: Library $LIB_PATH not found"
    exit 1
fi

# 通过GDB注入动态库
gdb -n -q -batch \   # 不使用批处理模式防止挂死目标进程
    -ex "attach $PID" \
    -ex "set \$dlopen = (void*(*)(char*, int)) dlopen" \
    -ex "set \$dlerror = (char*(*)(void)) dlerror" \
    -ex "call \$dlopen(\"$LIB_PATH\", 1)" \        # 延迟绑定
    -ex "call \$dlerror()" \
    -ex "detach" \
    -ex "quit" > gdb_output.txt 2>&1

# 检查注入结果
if grep -q "= (void *) 0x0" gdb_output.txt; then
    echo -e "\033[31mError: Injection failed. Error message:\033[0m"
    grep -A 1 "call \$dlerror()" gdb_output.txt | tail -n 1
    exit 1
else
    echo -e "\033[32mSuccess: Library injected at address:\033[0m"
    grep -A 1 "call \$dlopen" gdb_output.txt | grep "(void *)"
    exit 0
fi

