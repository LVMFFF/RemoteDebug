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

#define MAX_STRING_LEN 256

// 打印错误信息并退出
void handle_error(const char *msg) {
    perror(msg);
    exit(EXIT_FAILURE);
}

// 查找目标进程中 libc 的基地址
unsigned long find_libc_base(pid_t pid) {
    char maps_path[64];
    snprintf(maps_path, sizeof(maps_path), "/proc/%d/maps", pid);

    FILE *maps = fopen(maps_path, "r");
    if (!maps) handle_error("Failed to open maps");

    unsigned long base = 0;
    char line[512];

    while (fgets(line, sizeof(line), maps)) {
        if (strstr(line, "libc.so") && strstr(line, "r-xp")) {
            base = strtoul(line, NULL, 16);
            break;
        }
    }

    fclose(maps);
    if (!base) {
        fprintf(stderr, "Failed to find libc base\n");
        exit(EXIT_FAILURE);
    }
    return base;
}

// 获取本地 libc 文件路径
char *get_libc_path(pid_t pid) {
    static char libc_path[256];
    char maps_path[64];
    snprintf(maps_path, sizeof(maps_path), "/proc/%d/maps", pid);

    FILE *maps = fopen(maps_path, "r");
    if (!maps) handle_error("Failed to open maps");

    char line[512];
    while (fgets(line, sizeof(line), maps)) {
        if (strstr(line, "libc.so") && strstr(line, "r-xp")) {
            char *path = strchr(line, '/');
            if (path) {
                char *newline = strchr(path, '\n');
                if (newline) *newline = '\0';
                strncpy(libc_path, path, sizeof(libc_path) - 1);
                break;
            }
        }
    }

    fclose(maps);
    if (!*libc_path) {
        fprintf(stderr, "Failed to get libc path\n");
        exit(EXIT_FAILURE);
    }
    return libc_path;
}

// 在 ELF 文件中查找符号偏移
unsigned long find_symbol_offset(const char *libc_path, const char *symbol_name) {
    int fd = open(libc_path, O_RDONLY);
    if (fd < 0) handle_error("Failed to open libc");

    struct stat st;
    if (fstat(fd, &st) != 0) handle_error("fstat failed");

    void *map = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (map == MAP_FAILED) handle_error("mmap failed");

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
        fprintf(stderr, "Failed to find .dynsym or .dynstr\n");
        munmap(map, st.st_size);
        close(fd);
        exit(EXIT_FAILURE);
    }

    Elf64_Sym *syms = (Elf64_Sym *)((char *)map + dynsym->sh_offset);
    char *strtab = (char *)map + dynstr->sh_offset;

    unsigned long offset = 0;
    int num_syms = dynsym->sh_size / sizeof(Elf64_Sym);
    for (int i = 0; i < num_syms; i++) {
        if (strcmp(strtab + syms[i].st_name, symbol_name) == 0) {
            offset = syms[i].st_value;
            break;
        }
    }

    munmap(map, st.st_size);
    close(fd);

    if (!offset) {
        fprintf(stderr, "Failed to find symbol offset for %s\n", symbol_name);
        exit(EXIT_FAILURE);
    }
    return offset;
}

int write_data(pid_t pid, unsigned long addr, const void *data, size_t len) {
    size_t i;
    for (i = 0; i + sizeof(long) <= len; i += sizeof(long)) {
        long word;
        memcpy(&word, (char *)data + i, sizeof(long));
        if (ptrace(PTRACE_POKEDATA, pid, addr + i, word) == -1) {
            perror("PTRACE_POKEDATA");
            return -1;
        }
    }

    // 处理剩余字节（非对齐部分）
    if (i < len) {
        long word = ptrace(PTRACE_PEEKTEXT, pid, (void *)(addr + i), NULL);
        if (word == -1 && errno) {
            perror("PTRACE_PEEKTEXT");
            return -1;
        }
        memcpy(&word, (char *)data + i, len - i);
        if (ptrace(PTRACE_POKEDATA, pid, addr + i, word) == -1) {
            perror("PTRACE_POKEDATA tail");
            return -1;
        }
    }

    // 验证字符串是否成功写入
    for (size_t i = 0; i < len; i += sizeof(long)) {
        long word = ptrace(PTRACE_PEEKDATA, pid, (void *)(addr + i), NULL);
        if (word == -1 && errno) {
            perror("PTRACE_PEEKDATA");
            ptrace(PTRACE_DETACH, pid, NULL, NULL);
            return 1;
        }
        printf("Data at 0x%lx: 0x%lx\n", addr + i, word);
    }

    return 0;
}

// 调用目标进程的函数
unsigned long remote_call(pid_t pid, unsigned long func_addr,
                          unsigned long arg1, unsigned long arg2, unsigned long arg3) {
    struct user_regs_struct regs, original_regs;
    if (ptrace(PTRACE_GETREGS, pid, NULL, &original_regs) < 0)
        handle_error("PTRACE_GETREGS");

    regs = original_regs;
    // regs.rsp -= 0x100; // 减小栈指针

    regs.rsp -= 0x100;        // 分配足够空间（对齐可选）
    // 在栈顶压入返回地址（这里写入 0，使 printf 返回后触发 SIGSEGV）
    regs.rsp &= ~0xF; // 16 字节对齐
    regs.rsp -= sizeof(long);
    if (ptrace(PTRACE_POKEDATA, pid, (void*)regs.rsp, (long)0) == -1)
        handle_error("PTRACE_POKEDATA return");
    regs.rdi = arg1;
    regs.rsi = arg2;
    regs.rip = arg3;
    regs.rip = func_addr;

    if (ptrace(PTRACE_SETREGS, pid, NULL, &regs) < 0)
        handle_error("PTRACE_SETREGS");

    if (ptrace(PTRACE_CONT, pid, NULL, NULL) < 0)
        handle_error("PTRACE_CONT");

    waitpid(pid, NULL, 0);
    if (ptrace(PTRACE_GETREGS, pid, NULL, &regs) < 0)
        handle_error("PTRACE_GETREGS after call");

    if (ptrace(PTRACE_SETREGS, pid, NULL, &original_regs) < 0)
        handle_error("PTRACE_SETREGS restore");

    return regs.rax;
}

int is_address_in_stack(pid_t pid, unsigned long addr) {
    char maps_path[256];
    snprintf(maps_path, sizeof(maps_path), "/proc/%d/maps", pid);

    FILE *maps = fopen(maps_path, "r");
    if (!maps) {
        perror("Failed to open maps");
        return 0;
    }

    char line[512];
    while (fgets(line, sizeof(line), maps)) {
        if (strstr(line, "[stack]")) {
            unsigned long start, end;
            if (sscanf(line, "%lx-%lx", &start, &end) == 2) {
                fclose(maps);
                return addr >= start && addr < end;
            }
        }
    }

    fclose(maps);
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <pid>\n", argv[0]);
        return EXIT_FAILURE;
    }

    pid_t pid = atoi(argv[1]);
    if (ptrace(PTRACE_ATTACH, pid, NULL, NULL) == -1)
        handle_error("PTRACE_ATTACH");

    waitpid(pid, NULL, 0);

    unsigned long libc_base = find_libc_base(pid);
    char *libc_path = get_libc_path(pid);

    struct user_regs_struct regs;
    if (ptrace(PTRACE_GETREGS, pid, NULL, &regs) == -1) {
        perror("ptrace getregs");
        ptrace(PTRACE_DETACH, pid, NULL, NULL);
        return 1;
    }
    unsigned long remote_stack = regs.rsp;

    const char *message = "print_special_symblo";
    unsigned long message_addr = remote_stack - 2 * MAX_STRING_LEN;

    // 调试输出，确认地址是否在合法堆栈范围
    printf("Remote stack address: 0x%lx\n", remote_stack);
    printf("Writing message at address: 0x%lx\n", message_addr);

    if (!is_address_in_stack(pid, message_addr)) {
        fprintf(stderr, "Address 0x%lx is not in stack range\n", message_addr);
        ptrace(PTRACE_DETACH, pid, NULL, NULL);
        return 1;
    }

    // 写入消息(已验证写入正确)
    if (write_data(pid, message_addr, message, strlen(message) + 1) == -1) {
        fprintf(stderr, "Failed to write message string\n");
        ptrace(PTRACE_DETACH, pid, NULL, NULL);
        return 1;
    }
    // 下面的方法写入失败
    // 调用 write(fd=1, buf=message_addr, count=message_len)
    // unsigned long write_offset = find_symbol_offset(libc_path, "write");
    // unsigned long write_addr = libc_base + write_offset;
    // remote_call(pid, write_addr, 1, message_addr, strlen(message) + 1);


    unsigned long dlsym_offset = find_symbol_offset(libc_path, "dlsym");
    remote_call(pid, libc_base + dlsym_offset, 0, message_addr, 0);

    unsigned long printf_offset = find_symbol_offset(libc_path, "printf");
    unsigned long fflush_offset = find_symbol_offset(libc_path, "fflush");
    unsigned long printf_addr = libc_base + printf_offset;
    unsigned long fflush_addr = libc_base + fflush_offset;

    // 验证函数地址
    printf("printf address: 0x%lx\n", printf_addr);
    printf("fflush address: 0x%lx\n", fflush_addr);

    // 调用 printf
    remote_call(pid, printf_addr, message_addr + 2, 0, 0);

    // 调用 fflush(NULL)
    remote_call(pid, fflush_addr, 0, 0, 0);

    // // 写入字符串到目标进程
    // if (write_data(pid, message_addr, message, strlen(message) + 1) == -1) {
    //     fprintf(stderr, "Failed to write message string\n");
    //     ptrace(PTRACE_DETACH, pid, NULL, NULL);
    //     return 1;
    // }

    // unsigned long write_offset = find_symbol_offset(libc_path, "write");
    // remote_call(pid, libc_base + write_offset, 1, message_addr, strlen(message) + 1);
    // ptrace(PTRACE_DETACH, pid, NULL, NULL);

    // unsigned long fflush_offset = find_symbol_offset(libc_path, "fflush");
    // unsigned long fflush_addr = libc_base + fflush_offset;

    // // stdout 指针地址，这个获取稍复杂，简化起见，传 NULL 表示刷新所有缓冲区
    // remote_call(pid, fflush_addr, 0, 0, 0);  // fflush(NULL)
    return 0;
}