import gdb

def load_and_call(lib_path, func_name):
    # 加载动态库
    dlopen_cmd = f'call (void*)dlopen("{lib_path}", 1)'
    gdb.execute(dlopen_cmd)
    
    # 获取函数指针
    gdb.execute(f"dlsym $dlerror, {func_name}")  # 使用dlerror避免符号冲突
    func_ptr = gdb.parse_and_eval("$rax")
    
    # 调用函数
    if func_ptr:
        gdb.execute(f"call (void)({func_ptr})()")
    else:
        print(f"函数 {func_name} 未找到！")

# 示例：加载libexample.so并调用hello()
load_and_call("/tmp/libexample.so", "hello")