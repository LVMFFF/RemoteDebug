#!/bin/bash

if [ $# -ne 2 ]; then
    echo "Usage: $0 <PID> <PATCH_PATH>"
    exit 1;
fi

PID=$1
PATCH_PATH=$(realpath "$2") # 使用绝对路径
echo "注入动态库到进程 $PID"
echo "库路径: $PATCH_PATH"
ls -la "$PATCH_PATH"

if ! ps -p "$PID" > /dev/null; then
    echo "Error: Process $PID not found"
    exit 1
fi

if [ ! -f "$PATCH_PATH" ]; then
    echo "Error: Library $PATCH_PATH not found"
    exit 1
fi

TMP_OUTPUT=$(mktemp)
sudo gdb -q -batch \
    -ex "attach $PID" \
    -ex "set scheduler-locking on" \
    -ex "p (void*) dlopen(\"$PATCH_PATH\", 1)" \
    -ex "p (char*) dlerror()" \
    -ex "set scheduler-locking off" \
    -ex "detach" \
    -ex "quit" > "$TMP_OUTPUT" 2>&1

# 处理输出并保存
cat "$TMP_OUTPUT"
rm "$TMP_OUTPUT"