// 包装ptrace底层接口

#pragma once

#include <string>

/**
 * @brief 获取进程中原函数地址
 * note 使用 dlsym 函数 RELD_NEXT 参数获取
 * @param[in] pid,func 进程id/函数名
 * @return 获取失败时返回空地址
 */
extern "C" uintptr_t get_function_addr_in_proc(int pid, const std::string &func);

/**
 * @brief 获取进程中原函数地址
 * @param[in] pid,func 进程id/函数名
 * @return 获取失败时返回空地址
 */

