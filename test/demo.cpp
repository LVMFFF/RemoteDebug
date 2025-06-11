#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <sys/user.h>
#include <errno.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <elf.h>
#include <dlfcn.h>
#include <sys/mman.h>
#include <errno.h>

#define MAX_STRING_LEN 256
// 查找目标进程中的 libc 基地址
unsigned long find_libc_base(pid_t pid) {
    char maps_path[256];
    snprintf(maps_path, sizeof(maps_path), "/proc/%d/maps", pid);

    FILE *maps = fopen(maps_path, "r");
    if (!maps) {
        perror("Failed to open maps");
        return 0;
    }

    unsigned long base = 0;
    char line[512];

    while (fgets(line, sizeof(line), maps)) {
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
char *get_libc_path(pid_t pid) {
    static char libc_path[256] = {0};
    char maps_path[256];
    snprintf(maps_path, sizeof(maps_path), "/proc/%d/maps", pid);

    FILE *maps = fopen(maps_path, "r");
    if (!maps) {
        perror("Failed to open maps");
        return NULL;
    }

    char line[512];
    while (fgets(line, sizeof(line), maps)) {
        if (strstr(line, "libc.so") && strstr(line, "r-xp")) {
            char *path = strstr(line, "/");
            if (path) {
                char *newline = strchr(path, '\n');
                if (newline) *newline = '\0';
                strncpy(libc_path, path, sizeof(libc_path) - 1);
                break;
            }
        }
    }

    fclose(maps);
    return libc_path;
}

// 在 ELF 文件中查找符号偏移
unsigned long find_symbol_offset(const char *libc_path, const char *symbol_name) {
    int fd = open(libc_path, O_RDONLY);
    if (fd < 0) {
        perror("Failed to open libc");
        return 0;
    }

    struct stat st;
    if (fstat(fd, &st)) {
        perror("fstat failed");
        close(fd);
        return 0;
    }

    void *map = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (map == MAP_FAILED) {
        perror("mmap failed");
        close(fd);
        return 0;
    }

    Elf64_Ehdr *ehdr = (Elf64_Ehdr *)map;
    Elf64_Shdr *shdrs = (Elf64_Shdr *)((char *)map + ehdr->e_shoff);
    Elf64_Shdr *shstrtab = &shdrs[ehdr->e_shstrndx];
    char *shstr = (char *)map + shstrtab->sh_offset;

    Elf64_Shdr *dynsym = NULL, *dynstr = NULL;
    for (int i = 0; i < ehdr->e_shnum; i++) {
        char *name = shstr + shdrs[i].sh_name;
        if (strcmp(name, ".dynsym") == 0) dynsym = &shdrs[i];
        if (strcmp(name, ".dynstr") == 0) dynstr = &shdrs[i];
    }

    if (!dynsym || !dynstr) {
        fprintf(stderr, "Failed to find .dynsym or .dynstr sections\n");
        munmap(map, st.st_size);
        close(fd);
        return 0;
    }

    Elf64_Sym *syms = (Elf64_Sym *)((char *)map + dynsym->sh_offset);
    char *strtab = (char *)map + dynstr->sh_offset;
    unsigned long offset = 0;
    int num_syms = dynsym->sh_size / sizeof(Elf64_Sym);

    for (int i = 0; i < num_syms; i++) {
        char *name = strtab + syms[i].st_name;
        if (strcmp(name, symbol_name) == 0) {
            offset = syms[i].st_value;
            break;
        }
    }

    munmap(map, st.st_size);
    close(fd);
    return offset;
}

// 在目标进程中调用 dlsym
unsigned long call_dlsym(pid_t pid, unsigned long dlsym_addr, const char *symbol_name) {
    struct user_regs_struct regs, original_regs;
    if (ptrace(PTRACE_GETREGS, pid, NULL, &original_regs) < 0) {
        perror("ptrace(GETREGS)");
        return 0;
    }

    regs = original_regs;
    unsigned long stack_addr = regs.rsp - 512;
    unsigned long string_addr = stack_addr + 256;

    // 写入符号名到目标进程内存
    size_t name_len = strlen(symbol_name) + 1;
    for (size_t i = 0; i < name_len; i += sizeof(long)) {
        long data = *(long *)(symbol_name + i);
        if (ptrace(PTRACE_POKETEXT, pid, (void *)(string_addr + i), (void *)data) < 0) {
            perror("ptrace(POKETEXT)");
            return 0;
        }
    }

    // 设置寄存器参数
    regs.rdi = 0;
    regs.rsi = string_addr;
    regs.rip = dlsym_addr;
    regs.rsp = stack_addr;

    if (ptrace(PTRACE_SETREGS, pid, NULL, &regs) < 0) {
        perror("ptrace(SETREGS)");
        return 0;
    }
    if (ptrace(PTRACE_CONT, pid, NULL, NULL) < 0) {
        perror("ptrace(CONT)");
        return 0;
    }

    waitpid(pid, NULL, 0);

    if (ptrace(PTRACE_GETREGS, pid, NULL, &regs) < 0) {
        perror("ptrace(GETREGS)");
        return 0;
    }

    ptrace(PTRACE_SETREGS, pid, NULL, &original_regs);
    return regs.rax;
}

// 写数据到远程进程内存（字节对齐）
int write_data(pid_t pid, unsigned long addr, const void *data, size_t len) {
    size_t i = 0;
    for (; i + sizeof(long) <= len; i += sizeof(long)) {
        long val = *(long *)((char *)data + i);
        if (ptrace(PTRACE_POKETEXT, pid, (void *)(addr + i), (void *)val) == -1) {
            perror("PTRACE_POKETEXT");
            return -1;
        }
    }
    // 处理剩余字节
    if (i < len) {
        long val = ptrace(PTRACE_PEEKTEXT, pid, (void *)(addr + i), NULL);
        if (val == -1 && errno) return -1;
        memcpy(&val, (char *)data + i, len - i);
        if (ptrace(PTRACE_POKETEXT, pid, (void *)(addr + i), (void *)val) == -1) {
            perror("PTRACE_POKETEXT tail");
            return -1;
        }
    }
    return 0;
}

// 远程调用函数（调用约定为：rdi, rsi, rdx 参数，返回值在 rax）
// func_addr: 函数地址
// arg1, arg2: 参数
// 返回 rax
unsigned long remote_call(pid_t pid, unsigned long func_addr,
                         unsigned long arg1, unsigned long arg2) {
    struct user_regs_struct regs, original_regs;
    if (ptrace(PTRACE_GETREGS, pid, NULL, &original_regs) == -1) {
        perror("GETREGS");
        return 0;
    }

    regs = original_regs;

    // 分配远程栈空间（减小 rsp）
    regs.rsp -= 0x100;

    // 写返回地址为0，防止调用 ret 崩溃（简化处理）
    if (ptrace(PTRACE_POKETEXT, pid, (void *)regs.rsp, 0) == -1) {
        perror("POKETEXT return addr");
        return 0;
    }

    // 设置函数调用参数
    regs.rdi = arg1;
    regs.rsi = arg2;
    regs.rip = func_addr;

    if (ptrace(PTRACE_SETREGS, pid, NULL, &regs) == -1) {
        perror("SETREGS");
        return 0;
    }

    if (ptrace(PTRACE_CONT, pid, NULL, NULL) == -1) {
        perror("CONT");
        return 0;
    }

    int status;
    waitpid(pid, &status, 0);
    if (!WIFSTOPPED(status)) {
        fprintf(stderr, "Target did not stop after call\n");
        return 0;
    }

    if (ptrace(PTRACE_GETREGS, pid, NULL, &regs) == -1) {
        perror("GETREGS after call");
        return 0;
    }

    unsigned long ret = regs.rax;

    // 恢复寄存器
    if (ptrace(PTRACE_SETREGS, pid, NULL, &original_regs) == -1) {
        perror("SETREGS restore");
        return 0;
    }

    return ret;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <pid>\n", argv[0]);
        return 1;
    }

    pid_t pid = atoi(argv[1]);

    if (ptrace(PTRACE_ATTACH, pid, NULL, NULL) == -1) {
        perror("ptrace(ATTACH)");
        return 1;
    }

    int status;
    waitpid(pid, &status, 0);

    if (!WIFSTOPPED(status)) {
        fprintf(stderr, "Process did not stop\n");
        ptrace(PTRACE_DETACH, pid, NULL, NULL);
        return 1;
    }

    unsigned long libc_base = find_libc_base(pid);
    if (!libc_base) {
        fprintf(stderr, "Failed to find libc base\n");
        ptrace(PTRACE_DETACH, pid, NULL, NULL);
        return 1;
    }

    char *libc_path = get_libc_path(pid);
    if (!libc_path) {
        fprintf(stderr, "Failed to get libc path\n");
        ptrace(PTRACE_DETACH, pid, NULL, NULL);
        return 1;
    }

    unsigned long dlsym_offset = find_symbol_offset(libc_path, "dlsym");
    if (!dlsym_offset) {
        fprintf(stderr, "Failed to find dlsym offset\n");
        ptrace(PTRACE_DETACH, pid, NULL, NULL);
        return 1;
    }
    printf("libc_base address: 0x%lx\n", libc_base);

    unsigned long dlsym_addr = libc_base + dlsym_offset;
    printf("dlsym address: 0x%lx\n", dlsym_addr);



    struct user_regs_struct regs;
    if (ptrace(PTRACE_GETREGS, pid, NULL, &regs) == -1) {
        perror("ptrace getregs");
        ptrace(PTRACE_DETACH, pid, NULL, NULL);
        return 1;
    }
    unsigned long remote_stack = regs.rsp;

    // 写入字符串 "printf"
    const char *printf_str = "printf";
    unsigned long printf_str_addr = remote_stack - MAX_STRING_LEN;
    if (write_data(pid, printf_str_addr, printf_str, strlen(printf_str) + 1) == -1) {
        fprintf(stderr, "Failed to write string 'printf'\n");
        ptrace(PTRACE_DETACH, pid, NULL, NULL);
        return 1;
    }

    // 调用 dlsym(handle, "printf")
    unsigned long printf_addr = remote_call(pid, dlsym_addr, (unsigned long)RTLD_DEFAULT, printf_str_addr);
    if (!printf_addr) {
        fprintf(stderr, "dlsym failed to find 'printf'\n");
    } else {
        printf("printf address: 0x%lx\n", printf_addr);
    }


    // 写入字符串到目标进程栈
    const char *message = "Hello from remote printf!\n";
    unsigned long message_addr = remote_stack - 2 * MAX_STRING_LEN;
    if (write_data(pid, message_addr, message, strlen(message) + 1) == -1) {
        fprintf(stderr, "Failed to write message string\n");
        ptrace(PTRACE_DETACH, pid, NULL, NULL);
        return 1;
    }

    // 远程调用 printf
    // remote_call(pid, printf_addr, message_addr, 0);

    unsigned long printf_offset = find_symbol_offset(libc_path, "printf");
    printf("orign printf address: 0x%lx\n", libc_base + printf_offset);
    remote_call(pid, libc_base + dlsym_offset, message_addr, 0);
    // unsigned long printf_addr = call_dlsym(pid, dlsym_addr, "printf");
    // if (!printf_addr) {
    //     fprintf(stderr, "Failed to find printf address\n");
    //     ptrace(PTRACE_DETACH, pid, NULL, NULL);
    //     return 1;
    // }

    // printf("dlsym printf address: 0x%lx\n", printf_addr);

    // unsigned long printf_offset = find_symbol_offset(libc_path, "printf");
    // printf("printf address: 0x%lx\n", libc_base + printf_offset);

    ptrace(PTRACE_DETACH, pid, NULL, NULL);
    return 0;
}