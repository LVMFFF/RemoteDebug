#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <sys/user.h>
#include <sys/stat.h>
#include <elf.h>
#include <dlfcn.h>
#include <sys/mman.h>
#include <errno.h>

// 查找目标进程中的 libc 基地址
unsigned long find_libc_base(pid_t pid) {
    char maps_path[256];
    snprintf(maps_path, sizeof(maps_path), "/proc/%d/maps", pid);

    FILE* maps = fopen(maps_path, "r");
    if(!maps) {
        perror("Failed to open maps");
        return 0;
    }

    unsigned long base = 0;
    char          line[512];

    while(fgets(line, sizeof(line), maps)) {
        if(strstr(line, "libc.so") && strstr(line, "r-xp")) {
            char* dash = strchr(line, '-');
            if(dash) {
                *dash = '\0';
                base  = strtoul(line, NULL, 16);
                break;
            }
        }
    }

    fclose(maps);
    return base;
}

// 获取本地 libc 文件路径
char* get_libc_path(pid_t pid) {
    static char libc_path[256] = { 0 };
    char        maps_path[256];
    snprintf(maps_path, sizeof(maps_path), "/proc/%d/maps", pid);

    FILE* maps = fopen(maps_path, "r");
    if(!maps) {
        perror("Failed to open maps");
        return NULL;
    }

    char line[512];
    while(fgets(line, sizeof(line), maps)) {
        if(strstr(line, "libc.so") && strstr(line, "r-xp")) {
            char* path = strstr(line, "/");
            if(path) {
                char* newline = strchr(path, '\n');
                if(newline)
                    *newline = '\0';
                strncpy(libc_path, path, sizeof(libc_path) - 1);
                break;
            }
        }
    }

    fclose(maps);
    return libc_path;
}

// 在 ELF 文件中查找符号偏移
unsigned long find_symbol_offset(const char* libc_path, const char* symbol_name) {
    int fd = open(libc_path, O_RDONLY);
    if(fd < 0) {
        perror("Failed to open libc");
        return 0;
    }

    struct stat st;
    if(fstat(fd, &st)) {
        perror("fstat failed");
        close(fd);
        return 0;
    }

    void* map = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if(map == MAP_FAILED) {
        perror("mmap failed");
        close(fd);
        return 0;
    }

    Elf64_Ehdr* ehdr     = (Elf64_Ehdr*)map;
    Elf64_Shdr* shdrs    = (Elf64_Shdr*)((char*)map + ehdr->e_shoff);
    Elf64_Shdr* shstrtab = &shdrs[ehdr->e_shstrndx];
    char*       shstr    = (char*)map + shstrtab->sh_offset;

    Elf64_Shdr *dynsym = NULL, *dynstr = NULL;
    for(int i = 0; i < ehdr->e_shnum; i++) {
        char* name = shstr + shdrs[i].sh_name;
        if(strcmp(name, ".dynsym") == 0)
            dynsym = &shdrs[i];
        if(strcmp(name, ".dynstr") == 0)
            dynstr = &shdrs[i];
    }

    if(!dynsym || !dynstr) {
        fprintf(stderr, "Failed to find .dynsym or .dynstr sections\n");
        munmap(map, st.st_size);
        close(fd);
        return 0;
    }

    Elf64_Sym*    syms     = (Elf64_Sym*)((char*)map + dynsym->sh_offset);
    char*         strtab   = (char*)map + dynstr->sh_offset;
    unsigned long offset   = 0;
    int           num_syms = dynsym->sh_size / sizeof(Elf64_Sym);

    for(int i = 0; i < num_syms; i++) {
        char* name = strtab + syms[i].st_name;
        if(strcmp(name, symbol_name) == 0) {
            offset = syms[i].st_value;
            break;
        }
    }

    munmap(map, st.st_size);
    close(fd);
    return offset;
}

// 在目标进程中分配内存
long allocate_memory(pid_t pid, size_t size) {
    struct user_regs_struct regs;
    
    // 获取当前寄存器状态
    if (ptrace(PTRACE_GETREGS, pid, NULL, &regs) < 0) {
        perror("ptrace getregs");
        return -1;
    }
    
    // 保存原始寄存器状态
    struct user_regs_struct orig_regs = regs;
    
    // 设置调用mmap的参数
    regs.rax = SYS_mmap; // mmap系统调用号
    regs.rdi = 0;        // 地址 (0 = 由内核选择)
    regs.rsi = size;     // 大小
    regs.rdx = PROT_READ | PROT_WRITE | PROT_EXEC; // 保护标志
    regs.r10 = MAP_PRIVATE | MAP_ANONYMOUS; // 标志
    regs.r8 = -1;        // 文件描述符
    regs.r9 = 0;         // 偏移
    
    // 设置寄存器
    if (ptrace(PTRACE_SETREGS, pid, NULL, &regs) < 0) {
        perror("ptrace setregs");
        return -1;
    }
    
    // 执行系统调用
    if (ptrace(PTRACE_SYSCALL, pid, NULL, NULL) < 0) {
        perror("ptrace syscall");
        return -1;
    }
    
    int status;
    waitpid(pid, &status, 0);
    
    // 获取系统调用结果
    if (ptrace(PTRACE_GETREGS, pid, NULL, &regs) < 0) {
        perror("ptrace getregs");
        return -1;
    }
    
    // 恢复原始寄存器
    ptrace(PTRACE_SETREGS, pid, NULL, &orig_regs);
    
    // 检查mmap是否成功 (rax包含返回值)
    if (regs.rax > 0xfffffffffffff000) {
        // 错误 (返回值为负数)
        errno = -regs.rax;
        perror("目标进程中的mmap失败");
        return -1;
    }
    
    return regs.rax;
}

// 向目标进程内存写入数据
void write_to_memory(pid_t pid, unsigned long addr, const void* data, size_t size) {
    size_t   aligned_size = (size + sizeof(long) - 1) & ~(sizeof(long) - 1);
    uint8_t* aligned_data = (uint8_t*)calloc(1, aligned_size);
    if(!aligned_data) {
        perror("calloc failed");
        return;
    }
    memcpy(aligned_data, data, size);

    for(size_t i = 0; i < aligned_size; i += sizeof(long)) {
        long val = *(long*)(aligned_data + i);
        if(ptrace(PTRACE_POKETEXT, pid, (void*)(addr + i), (void*)val) == -1) {
            perror("ptrace(POKETEXT)");
            break;
        }
    }

    free(aligned_data);
}

// 在目标进程中调用函数
unsigned long
call_function(pid_t pid, unsigned long func_addr, unsigned long arg1, unsigned long arg2) {
    struct user_regs_struct old_regs;
    struct user_regs_struct regs;

    if(ptrace(PTRACE_GETREGS, pid, NULL, &old_regs) == -1) {
        perror("ptrace(GETREGS)");
        return 0;
    }

    // 分配内存用于断点指令
    unsigned long int3_addr = allocate_memory(pid, 1);
    if(!int3_addr) {
        fprintf(stderr, "Failed to allocate memory for int3\n");
        return 0;
    }

    // 写入断点指令 (0xCC)
    if(ptrace(PTRACE_POKETEXT, pid, (void*)int3_addr, (void*)0xCC) == -1) {
        perror("ptrace(POKETEXT for int3)");
        return 0;
    }

    // 准备新栈和返回地址
    unsigned long new_rsp = old_regs.rsp - sizeof(unsigned long);
    if(ptrace(PTRACE_POKETEXT, pid, (void*)new_rsp, (void*)int3_addr) == -1) {
        perror("ptrace(POKETEXT for stack)");
        return 0;
    }

    // 设置调用参数
    memcpy(&regs, &old_regs, sizeof(regs));
    regs.rdi = arg1;
    regs.rsi = arg2;
    regs.rip = func_addr;
    regs.rsp = new_rsp;

    if(ptrace(PTRACE_SETREGS, pid, NULL, &regs) == -1) {
        perror("ptrace(SETREGS)");
        return 0;
    }

    if(ptrace(PTRACE_CONT, pid, NULL, NULL) == -1) {
        perror("ptrace(CONT)");
        return 0;
    }

    int status;
    waitpid(pid, &status, 0);
    if(!WIFSTOPPED(status) || WSTOPSIG(status) != SIGTRAP) {
        fprintf(stderr, "Unexpected stop signal\n");
        return 0;
    }

    if(ptrace(PTRACE_GETREGS, pid, NULL, &regs) == -1) {
        perror("ptrace(GETREGS after call)");
        return 0;
    }

    unsigned long result = regs.rax;

    // 恢复原始寄存器状态
    if(ptrace(PTRACE_SETREGS, pid, NULL, &old_regs) == -1) {
        perror("ptrace(SETREGS restore)");
        return 0;
    }

    return result;
}

// 获取目标进程中函数地址
unsigned long find_function_address(pid_t pid, const char* function_name) {
    unsigned long libc_base = find_libc_base(pid);
    if(!libc_base) {
        fprintf(stderr, "Failed to find libc base address\n");
        return 0;
    }

    const char* libc_path = get_libc_path(pid);
    if(!libc_path || !*libc_path) {
        fprintf(stderr, "Failed to find libc path\n");
        return 0;
    }

    unsigned long symbol_offset = find_symbol_offset(libc_path, function_name);
    if(!symbol_offset) {
        fprintf(stderr, "Failed to find symbol offset for '%s'\n", function_name);
        return 0;
    }

    return libc_base + symbol_offset;
}

int main(int argc, char* argv[]) {
    if(argc != 2) {
        fprintf(stderr, "Usage: %s <pid>\n", argv[0]);
        return 1;
    }

    pid_t pid = atoi(argv[1]);

    if(ptrace(PTRACE_ATTACH, pid, NULL, NULL) == -1) {
        perror("ptrace(ATTACH)");
        return 1;
    }

    int status;
    waitpid(pid, &status, 0);
    if(!WIFSTOPPED(status)) {
        fprintf(stderr, "Process did not stop\n");
        ptrace(PTRACE_DETACH, pid, NULL, NULL);
        return 1;
    }

    // 获取 dlsym 地址
    unsigned long dlsym_addr = find_function_address(pid, "dlsym");
    if(!dlsym_addr) {
        fprintf(stderr, "Failed to find dlsym address\n");
        ptrace(PTRACE_DETACH, pid, NULL, NULL);
        return 1;
    }
    printf("dlsym address in target process: 0x%lx\n", dlsym_addr);

    // 分配内存并写入字符串 "printf"
    unsigned long printf_name_addr = allocate_memory(pid, 10);
    if(!printf_name_addr) {
        fprintf(stderr, "Failed to allocate memory for printf name\n");
        ptrace(PTRACE_DETACH, pid, NULL, NULL);
        return 1;
    }
    printf("printf_name_addr address in target process: 0x%lx\n", printf_name_addr);

    write_to_memory(pid, printf_name_addr, "printf", 7);

    // 使用 dlsym 获取 printf 地址
    unsigned long printf_addr = call_function(pid, dlsym_addr, 0, printf_name_addr);
    if(!printf_addr) {
        fprintf(stderr, "Failed to get printf address via dlsym\n");
        ptrace(PTRACE_DETACH, pid, NULL, NULL);
        return 1;
    }
    printf("printf address: 0x%lx\n", printf_addr);

    // 分配内存并写入输出字符串
    unsigned long hello_str_addr = allocate_memory(pid, 128);
    if(!hello_str_addr) {
        fprintf(stderr, "Failed to allocate memory for hello string\n");
        ptrace(PTRACE_DETACH, pid, NULL, NULL);
        return 1;
    }
    write_to_memory(pid, hello_str_addr, "Hello from injected code!\n", 27);

    // 调用 printf
    call_function(pid, printf_addr, hello_str_addr, 0);

    ptrace(PTRACE_DETACH, pid, NULL, NULL);
    return 0;
}