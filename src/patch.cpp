#include <cstdint>
#include <cstring>
#include <sys/mman.h>
#include <unistd.h>
#include <dlfcn.h>
#include <iostream>
#include <vector>
#include <cassert>

// x86-64 架构的指令集
#ifdef __x86_64__
constexpr uint8_t JMP_OPCODE = 0xE9;
constexpr uint8_t NOP_OPCODE = 0x90;
constexpr size_t SHORT_JMP_SIZE = 5;
constexpr size_t LONG_JMP_SIZE = 14;

#elif defined(__aarch64__)
// ARM64 架构实现
constexpr uint32_t BR_OPCODE = 0xD61F0000;
constexpr size_t BR_SIZE = 4;
constexpr size_t NOP_SIZE = 4;
#endif

// 跳转岛结构
struct JumpIsland {
    void* allocated_memory = nullptr;
    size_t size = 0;
    uintptr_t original_function = 0;
    uintptr_t patch_function = 0;
    std::vector<uint8_t> original_prologue;
};

class FunctionPatcher {
public:
    FunctionPatcher() = default;
    ~FunctionPatcher() {
        // 清理所有跳转岛
        for (auto& island : islands) {
            uninstall_patch(island);
        }
    }

    // 安装函数补丁
    bool install_patch(void* original_func, void* patch_func) {
        JumpIsland island;
        island.original_function = reinterpret_cast<uintptr_t>(original_func);
        island.patch_function = reinterpret_cast<uintptr_t>(patch_func);

        // 保存原始函数入口代码
        if (!save_original_prologue(island)) {
            std::cerr << "保存原始函数入口失败" << std::endl;
            return false;
        }

        // 创建跳转岛
        if (!create_jump_island(island)) {
            std::cerr << "创建跳转岛失败" << std::endl;
            return false;
        }

        // 修改原始函数入口
        if (!patch_original_function(island)) {
            std::cerr << "修改原始函数入口失败" << std::endl;
            uninstall_patch(island);
            return false;
        }

        islands.push_back(island);
        return true;
    }

    // 卸载函数补丁
    bool uninstall_patch(JumpIsland& island) {
        if (!restore_original_prologue(island)) {
            std::cerr << "恢复原始函数入口失败" << std::endl;
            return false;
        }

        // 释放跳转岛内存
        if (island.allocated_memory) {
            munmap(island.allocated_memory, island.size);
            island.allocated_memory = nullptr;
        }

        return true;
    }

private:
    // 保存原始函数入口代码
    bool save_original_prologue(JumpIsland& island) {
        #ifdef __x86_64__
        // 保存足够的指令以覆盖短跳转
        island.original_prologue.resize(SHORT_JMP_SIZE);
        memcpy(island.original_prologue.data(), 
               reinterpret_cast<void*>(island.original_function), 
               SHORT_JMP_SIZE);
        return true;
        
        #elif defined(__aarch64__)
        // ARM64 需要保存4字节指令
        island.original_prologue.resize(BR_SIZE);
        memcpy(island.original_prologue.data(), 
               reinterpret_cast<void*>(island.original_function), 
               BR_SIZE);
        return true;
        
        #else
        std::cerr << "不支持的架构" << std::endl;
        return false;
        #endif
    }

    // 创建跳转岛
    bool create_jump_island(JumpIsland& island) {
        #ifdef __x86_64__
        // 计算所需空间: 短跳转 + 长跳转 + 原始入口代码
        island.size = SHORT_JMP_SIZE + LONG_JMP_SIZE + island.original_prologue.size();
        
        // 分配可执行内存 (靠近原始函数地址)
        island.allocated_memory = allocate_near(island.original_function, island.size);
        if (!island.allocated_memory) return false;
        
        uint8_t* island_ptr = static_cast<uint8_t*>(island.allocated_memory);
        
        // 1. 跳转到补丁函数 (长跳转)
        assemble_long_jump(island_ptr, island.patch_function);
        island_ptr += LONG_JMP_SIZE;
        
        // 2. 原始入口代码 (将被短跳转覆盖的部分)
        memcpy(island_ptr, island.original_prologue.data(), island.original_prologue.size());
        island_ptr += island.original_prologue.size();
        
        // 3. 跳回原始函数 (在原始入口代码之后)
        uintptr_t return_address = island.original_function + island.original_prologue.size();
        assemble_short_jump(island_ptr, return_address);
        
        return true;
        
        #elif defined(__aarch64__)
        // ARM64 实现
        island.size = BR_SIZE + island.original_prologue.size();
        island.allocated_memory = allocate_near(island.original_function, island.size);
        if (!island.allocated_memory) return false;
        
        uint32_t* island_ptr = static_cast<uint32_t*>(island.allocated_memory);
        
        // 跳转到补丁函数
        assemble_arm64_jump(island_ptr, island.patch_function);
        island_ptr++;
        
        // 原始入口代码
        memcpy(island_ptr, island.original_prologue.data(), island.original_prologue.size());
        
        return true;
        
        #else
        return false;
        #endif
    }

    // 修改原始函数入口
    bool patch_original_function(JumpIsland& island) {
        #ifdef __x86_64__
        // 设置内存可写
        if (!set_memory_protection(reinterpret_cast<void*>(island.original_function), 
                                  SHORT_JMP_SIZE, PROT_READ | PROT_WRITE | PROT_EXEC)) {
            return false;
        }
        
        // 写入短跳转到跳转岛
        uint8_t* func_ptr = reinterpret_cast<uint8_t*>(island.original_function);
        assemble_short_jump(func_ptr, reinterpret_cast<uintptr_t>(island.allocated_memory));
        
        // 用NOP填充剩余空间 (如果有)
        for (size_t i = SHORT_JMP_SIZE; i < island.original_prologue.size(); ++i) {
            func_ptr[i] = NOP_OPCODE;
        }
        
        // 刷新指令缓存
        flush_instruction_cache(reinterpret_cast<void*>(island.original_function), 
                                island.original_prologue.size());
        return true;
        
        #elif defined(__aarch64__)
        if (!set_memory_protection(reinterpret_cast<void*>(island.original_function), 
                                  BR_SIZE, PROT_READ | PROT_WRITE | PROT_EXEC)) {
            return false;
        }
        
        uint32_t* func_ptr = reinterpret_cast<uint32_t*>(island.original_function);
        assemble_arm64_jump(func_ptr, reinterpret_cast<uintptr_t>(island.allocated_memory));
        
        flush_instruction_cache(reinterpret_cast<void*>(island.original_function), BR_SIZE);
        return true;
        
        #else
        return false;
        #endif
    }

    // 恢复原始函数入口
    bool restore_original_prologue(JumpIsland& island) {
        #ifdef __x86_64__
        if (!set_memory_protection(reinterpret_cast<void*>(island.original_function), 
                                  island.original_prologue.size(), 
                                  PROT_READ | PROT_WRITE | PROT_EXEC)) {
            return false;
        }
        
        memcpy(reinterpret_cast<void*>(island.original_function), 
               island.original_prologue.data(), 
               island.original_prologue.size());
        
        flush_instruction_cache(reinterpret_cast<void*>(island.original_function), 
                                island.original_prologue.size());
        return true;
        
        #elif defined(__aarch64__)
        if (!set_memory_protection(reinterpret_cast<void*>(island.original_function), 
                                  BR_SIZE, PROT_READ | PROT_WRITE | PROT_EXEC)) {
            return false;
        }
        
        memcpy(reinterpret_cast<void*>(island.original_function), 
               island.original_prologue.data(), 
               BR_SIZE);
        
        flush_instruction_cache(reinterpret_cast<void*>(island.original_function), BR_SIZE);
        return true;
        
        #else
        return false;
        #endif
    }

    // 分配靠近指定地址的内存
    void* allocate_near(uintptr_t target_address, size_t size) {
        // 尝试在目标地址附近分配内存
        const size_t allocation_size = sysconf(_SC_PAGESIZE);
        const uintptr_t start = target_address - 0x10000000; // ±256MB
        const uintptr_t end = target_address + 0x10000000;
        
        for (uintptr_t addr = start; addr < end; addr += allocation_size) {
            void* result = mmap(reinterpret_cast<void*>(addr), size,
                                PROT_READ | PROT_WRITE | PROT_EXEC,
                                MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
            
            if (result != MAP_FAILED) {
                return result;
            }
        }
        
        // 回退到常规分配
        return mmap(nullptr, size, PROT_READ | PROT_WRITE | PROT_EXEC,
                    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    }

    // 设置内存保护
    bool set_memory_protection(void* address, size_t size, int protection) {
        long page_size = sysconf(_SC_PAGESIZE);
        uintptr_t start = reinterpret_cast<uintptr_t>(address);
        uintptr_t end = start + size;
        uintptr_t page_start = start & ~(page_size - 1);
        
        if (mprotect(reinterpret_cast<void*>(page_start), 
                     end - page_start, protection) == -1) {
            std::cerr << "mprotect 失败: " << strerror(errno) << std::endl;
            return false;
        }
        return true;
    }

    // 刷新指令缓存
    void flush_instruction_cache(void* address, size_t size) {
        #ifdef __linux__
        __builtin___clear_cache(reinterpret_cast<char*>(address), 
                               reinterpret_cast<char*>(address) + size);
        #elif defined(__APPLE__)
        sys_icache_invalidate(address, size);
        #endif
    }

    // x86-64 汇编辅助函数
    #ifdef __x86_64__
    void assemble_short_jump(uint8_t* buffer, uintptr_t target) {
        uintptr_t source = reinterpret_cast<uintptr_t>(buffer);
        int32_t offset = static_cast<int32_t>(target - (source + SHORT_JMP_SIZE));
        
        buffer[0] = JMP_OPCODE;
        *reinterpret_cast<int32_t*>(buffer + 1) = offset;
    }

    void assemble_long_jump(uint8_t* buffer, uintptr_t target) {
        // movabs rax, target
        buffer[0] = 0x48; // REX.W prefix
        buffer[1] = 0xB8; // MOV RAX, imm64
        *reinterpret_cast<uint64_t*>(buffer + 2) = target;
        
        // jmp rax
        buffer[10] = 0xFF;
        buffer[11] = 0xE0;
    }
    #endif

    // ARM64 汇编辅助函数
    #ifdef __aarch64__
    void assemble_arm64_jump(uint32_t* buffer, uintptr_t target) {
        uintptr_t source = reinterpret_cast<uintptr_t>(buffer);
        int64_t offset = target - source;
        
        if (llabs(offset) < (1 << 26)) {
            // 使用 B 指令 (26位有符号偏移)
            uint32_t imm26 = (offset >> 2) & 0x3FFFFFF;
            *buffer = 0x14000000 | imm26;
        } else {
            // 使用绝对地址加载
            uint32_t high = (target >> 32) & 0xFFFF;
            uint32_t low = target & 0xFFFF;
            
            // MOVZ X17, #high, LSL #48
            *buffer++ = 0xD2800000 | (17 << 5) | ((high >> 12) & 0xFFFF);
            
            // MOVK X17, #low, LSL #32
            *buffer++ = 0xF2A00000 | (17 << 5) | (low & 0xFFFF);
            
            // BR X17
            *buffer = BR_OPCODE | (17 << 5);
        }
    }
    #endif

private:
    std::vector<JumpIsland> islands;
};

// 使用示例
void original_function() {
    std::cout << "原始函数被调用" << std::endl;
}

void patched_function() {
    std::cout << "补丁函数被调用" << std::endl;
}

int main() {
    FunctionPatcher patcher;
    
    std::cout << "安装补丁前:" << std::endl;
    original_function();
    
    // 安装补丁
    if (patcher.install_patch(reinterpret_cast<void*>(&original_function),
                             reinterpret_cast<void*>(&patched_function))) {
        std::cout << "\n安装补丁后:" << std::endl;
        original_function(); // 实际调用 patched_function
    }
    
    return 0;
}