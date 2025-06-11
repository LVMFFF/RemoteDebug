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


// 查找目标进程中的 libc 基地址
unsigned long find_libc_base(pid_t pid)
{
    char maps_path[256];
    snprintf(maps_path, sizeof(maps_path), "/proc/%d/maps", pid);

    FILE *maps = fopen(maps_path, "r");
    if (!maps) {
        printf("Failed to open maps");
        return 0;
    }

    unsigned long base = 0;
    char line[512];

    while (fgets(line, sizeof(line), maps)) {
        // 查找 libc 映射 (r-xp 表示可执行段)
        if (strstr(line, "libc.so") && strstr(line, "r-xp")) {
            char *dash = strchr(line, '-');
            if (dash) {
                *dash = '\0';
                base = strtoul(line, NULL, 16);
                break;
            }
        }
    }

    fclose(maps);
    return base;
}

// 获取本地 libc 文件路径
char *get_libc_path(pid_t pid)
{
    static char libc_path[256] = { 0 };
    char maps_path[256];
    snprintf(maps_path, sizeof(maps_path), "/proc/%d/maps", pid);

    FILE *maps = fopen(maps_path, "r");
    if (!maps) {
        printf("Failed to open maps");
        return NULL;
    }

    char line[512];
    while (fgets(line, sizeof(line), maps)) {
        if (strstr(line, "libc.so") && strstr(line, "r-xp")) {
            // 提取完整路径
            char *path = strstr(line, "/");
            if (path) {
                // 移除换行符
                char *newline = strchr(path, '\n');
                if (newline)
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
unsigned long find_symbol_offset(const char *libc_path, const char *symbol_name)
{
    int fd = open(libc_path, O_RDONLY);
    if (fd < 0) {
        printf("Failed to open libc");
        return 0;
    }

    struct stat st;
    if (fstat(fd, &st)) {
        printf("fstat failed");
        close(fd);
        return 0;
    }

    // 映射整个文件到内存
    void *map = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (map == MAP_FAILED) {
        printf("mmap failed");
        close(fd);
        return 0;
    }

    Elf64_Ehdr *ehdr = (Elf64_Ehdr *)map;
    Elf64_Shdr *shdrs = (Elf64_Shdr *)((char *)map + ehdr->e_shoff);

    // 查找节头字符串表
    Elf64_Shdr *shstrtab = &shdrs[ehdr->e_shstrndx];
    char *shstr = (char *)map + shstrtab->sh_offset;

    // 查找 .dynsym 和 .dynstr
    Elf64_Shdr *dynsym = NULL, *dynstr = NULL;
    for (int i = 0; i < ehdr->e_shnum; i++) {
        char *name = shstr + shdrs[i].sh_name;
        // printf( "find seection: %s \n", name);
        if (strcmp(name, ".dynsym") == 0)
            dynsym = &shdrs[i];
        if (strcmp(name, ".dynstr") == 0)
            dynstr = &shdrs[i];
    }

    if (!dynsym || !dynstr) {
        printf("Failed to find .dynsym or .dynstr sections \n");
        munmap(map, st.st_size);
        close(fd);
        return 0;
    }

    Elf64_Sym *syms = (Elf64_Sym *)((char *)map + dynsym->sh_offset);
    char *strtab = (char *)map + dynstr->sh_offset;

    // 查找符号
    unsigned long offset = 0;
    int num_syms = dynsym->sh_size / sizeof(Elf64_Sym);

    for (int i = 0; i < num_syms; i++) {
        char *name = strtab + syms[i].st_name;
        // printf( "find symbol: %s \n", name);
        if (strcmp(name, symbol_name) == 0) {
            offset = syms[i].st_value;
            break;
        }
    }

    munmap(map, st.st_size);
    close(fd);
    return offset;
}

// 在目标进程中查找函数地址
unsigned long find_function_address(pid_t pid, const char *function_name)
{
    ptrace(PTRACE_ATTACH, pid, NULL, NULL);
    
    int status;
    waitpid(pid, &status, 0);

    // 获取 libc 基地址
    unsigned long libc_base = find_libc_base(pid);
    if (!libc_base) {
        printf("Failed to find libc base address\n");
        ptrace(PTRACE_DETACH, pid, NULL, NULL);
        return 0;
    }

    // 获取本地 libc 路径
    const char *libc_path = get_libc_path(pid);
    if (!libc_path || !*libc_path) {
        printf("Failed to find libc path\n");
        ptrace(PTRACE_DETACH, pid, NULL, NULL);
        return 0;
    }

    printf("libc_path: %s \n", libc_path);

    // 查找符号偏移
    unsigned long symbol_offset = find_symbol_offset(libc_path, function_name);
    if (!symbol_offset) {
        printf("Failed to find symbol offset for '%s'\n", function_name);
        ptrace(PTRACE_DETACH, pid, NULL, NULL);
        return 0;
    }

    // 计算函数地址
    unsigned long function_address = libc_base + symbol_offset;

    // 分离目标进程
    ptrace(PTRACE_DETACH, pid, NULL, NULL);

    return function_address;
}

// 获取目标进程中 libc 基地址
unsigned long get_libc_base(pid_t pid) {
    char path[256];
    snprintf(path, sizeof(path), "/proc/%d/maps", pid);
    
    FILE *fp = fopen(path, "r");
    if (!fp) return 0;
    
    char line[512];
    unsigned long base = 0;
    
    while (fgets(line, sizeof(line), fp)) {
        if (strstr(line, "libc.so") && strstr(line, "r-xp")) {
            char *addr = strtok(line, "-");
            base = strtoul(addr, NULL, 16);
            break;
        }
    }
    
    fclose(fp);
    return base;
}

// 在目标进程中分配内存
unsigned long allocate_memory(pid_t pid, size_t size) {
    struct user_regs_struct regs;
    
    // 保存原始寄存器
    ptrace(PTRACE_GETREGS, pid, NULL, &regs);
    struct user_regs_struct orig_regs = regs;
    
    // 设置 mmap 参数 (x86_64)
    regs.rdi = 0;                   // addr
    regs.rsi = size;                // length
    regs.rdx = PROT_READ | PROT_WRITE; // prot
    regs.r10 = MAP_PRIVATE | MAP_ANONYMOUS; // flags
    regs.r8 = -1;                   // fd
    regs.r9 = 0;                    // offset
    
    // 设置系统调用号 (mmap 的系统调用号在 x86_64 是 9)
    regs.rax = 9;
    
    // 模拟系统调用
    regs.rip = 0; // 设置 rip 为 0，确保系统调用被触发
    ptrace(PTRACE_SETREGS, pid, NULL, &regs);
    
    // 设置系统调用断点
    long original_data;
    long *rip_ptr = (long *)regs.rip;
    original_data = ptrace(PTRACE_PEEKTEXT, pid, rip_ptr, NULL);
    long new_data = 0x050f; // 'syscall' 指令的机器码
    ptrace(PTRACE_POKETEXT, pid, rip_ptr, new_data);
    
    // 继续执行
    ptrace(PTRACE_CONT, pid, NULL, NULL);
    waitpid(pid, NULL, 0);
    
    // 恢复原始指令
    ptrace(PTRACE_POKETEXT, pid, rip_ptr, original_data);
    
    // 获取结果
    ptrace(PTRACE_GETREGS, pid, NULL, &regs);
    unsigned long mem = regs.rax;
    
    // 恢复原始寄存器
    ptrace(PTRACE_SETREGS, pid, NULL, &orig_regs);
    
    return mem;
}

// 验证 dlsym 地址
int verify_dlsym_address(pid_t pid, unsigned long dlsym_addr) {
    // 分配内存用于测试代码
    unsigned long mem = allocate_memory(pid, 4096);
    if (!mem) {
        fprintf(stderr, "Failed to allocate memory\n");
        return 0;
    }
    
    // 测试代码：调用 dlsym 获取 printf 地址
    unsigned char code[] = {
        // 设置参数: dlsym(RTLD_DEFAULT, "printf")
        0x48, 0xc7, 0xc7, 0xff, 0xff, 0xff, 0xff, // mov rdi, -1 (RTLD_DEFAULT)
        0x48, 0x8d, 0x35, 0x0a, 0x00, 0x00, 0x00, // lea rsi, [rel str]
        // 调用 dlsym
        0xff, 0xd0,                               // call rax (dlsym)
        // 保存结果到已知地址
        0x48, 0xa3, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // mov [0x0], rax
        // 退出
        0xcc,                                     // int3 (断点)
        // 字符串 "printf"
        'p', 'r', 'i', 'n', 't', 'f', 0x00
    };
    
    // 设置结果存储地址 (使用分配内存的前8字节)
    *(unsigned long*)(code + 21) = mem;
    
    // 设置 dlsym 地址
    *(unsigned long*)(code + 11) = dlsym_addr - (mem + 11 + 7);
    
    // 写入代码到目标进程
    for (size_t i = 0; i < sizeof(code); i += sizeof(long)) {
        long data = 0;
        memcpy(&data, code + i, sizeof(long));
        ptrace(PTRACE_POKEDATA, pid, mem + i, data);
    }
    
    // 设置执行上下文
    struct user_regs_struct regs;
    ptrace(PTRACE_GETREGS, pid, NULL, &regs);
    struct user_regs_struct orig_regs = regs;
    
    regs.rip = mem;
    regs.rax = dlsym_addr; // 设置 dlsym 地址
    ptrace(PTRACE_SETREGS, pid, NULL, &regs);
    
    // 执行代码
    ptrace(PTRACE_CONT, pid, NULL, NULL);
    waitpid(pid, NULL, 0);
    
    // 读取结果
    long result = ptrace(PTRACE_PEEKDATA, pid, mem, NULL);
    unsigned long printf_addr = result;
    
    // 恢复原始上下文
    ptrace(PTRACE_SETREGS, pid, NULL, &orig_regs);
    
    // 验证结果
    if (printf_addr == 0 || printf_addr == -1) {
        fprintf(stderr, "dlsym returned invalid address: 0x%lx\n", printf_addr);
        return 0;
    }
    
    // 获取目标进程中 printf 的预期地址
    unsigned long expected_printf = find_function_address(pid, "printf");
    
    printf("dlsym returned printf address: 0x%lx\n", printf_addr);
    printf("Expected printf address: 0x%lx\n", expected_printf);
    
    return printf_addr == expected_printf;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <pid>\n", argv[0]);
        return 1;
    }
    
    pid_t pid = atoi(argv[1]);
    
    // 获取 dlsym 地址
    unsigned long dlsym_addr = find_function_address(pid, "dlsym");
    printf("dlsym address in target process: 0x%lx\n", dlsym_addr);
    
    // 验证地址
    if (verify_dlsym_address(pid, dlsym_addr)) {
        printf("SUCCESS: dlsym address is correct\n");
    } else {
        printf("FAILURE: dlsym address is incorrect\n");
    }
    
    // 分离进程
    ptrace(PTRACE_DETACH, pid, NULL, NULL);
    
    return 0;
}