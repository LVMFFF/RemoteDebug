// 包装ptrace底层接口

#include "ptrace_util.h"

extern "C" uintptr_t get_function_addr_in_proc(int pid, const std::string &func)
{
    uintptr_t func_addr = dlsym(RELD_NEXT, func.c_str());
    if (func_addr == nullptr) {
        LOG_ERROR("get func:%s addr fail", func.c_str());
        return nullptr;
    }
    return func_addr
}

/**
 * @brief 获取进程中原函数地址
 * @param[in] pid,func 进程id/函数名
 * @return 获取失败时返回空地址
 */
