# 使用
- **基本用法**
```
./hotpatcher <pid> <patch_lib>
```

- **详细模式**
```
./hotpatcher <pid> <patch_lib> --verbose
```

- **强制模式 (即使部分函数失败也继续)**
```
./hotpatcher <pid> <patch_lib> --force
```

- **示例**
```
./hotpatcher 1234 ./libmypatch.so --verbose
```

# 原理

- windows: 在目标进程创建线程执行补丁工具进行函数替换
- linux: 当前实现为封装 ptrace 的操作，函数注入等需要直接修改寄存器，后续可参考 win 原理，在目标进程拉起一个线程来完成热补丁加载

当前实现方式：
```
查找目标进程原始函数地址 
  -> 查找 dlopen 函数地址 -> 将补丁文件链接进目标进程 
  -> 查找dlsym地址  -> 查找补丁函数在目标进程和补丁文件中的位置
  -> 将目标函数的入口指令替换为补丁中函数的地址
```

下面以linux上 x86_64 架构为例：
## 内存操作：在目标进程读写数据
- 分配内存
```cpp
// 获取原始寄存器内容保存
orig_regs = ptrace(PTRACE_GETREGS,...）

// 修改寄存器内容，此处使用 SYS_mmap 系统调用分配空间
// 与具体架构有关，此处使用 x86_64 架构为例
regs.rax = SYS_mmap; // rax 寄存器存储系统调用号
regs.rdi = 0;        // 地址 (0 = 由内核选择); rdi 存储系统调用第一个参数，下面的寄存器递加
regs.rsi = size;     // 大小
regs.rdx = PROT_READ | PROT_WRITE | PROT_EXEC; // 保护标志
regs.r10 = MAP_PRIVATE | MAP_ANONYMOUS; // 标志
regs.r8 = -1;        // 文件描述符
regs.r9 = 0;         // 偏移

// 设置寄存器，执行系统调用
ptrace(PTRACE_SETREGS,...)
ptrace(PTRACE_SYSCALL,...)

// 获取结果，恢复寄存器
ptrace(PTRACE_GETREGS,...)
ptrace(PTRACE_SETREGS, ..., &orig_regs)
```

- 向目标进程写入数据
ptrace 写入的数据必须是对齐的，使用long进行对齐
```
// 1、对齐内存
size_t aligned_size = (size + sizeof(long) - 1) &  ~(sizeof(long) - 1)
// 2、循环使用 ptrace(PTRACE_POKEDATA) 写入对齐后的数据
```

## 函数调用：在目标进程调用函数

- 后去目标进程中 `dlsym` 地址，后续其他函数地址可通过 `dlsym` 获取
```
// 读取 `/proc/pid/maps` 中内存空间映射信息

// 读取 
```

- 目标进程调用函数
```cpp
// 1、获取目标进程上下文
ptrace(PTRACE_ATTACH,..)
ptrace(PTRACE_GETREGS,...） 

// 2、修改指令指针指向目标函数
resg.rip = (unsign long)target_func

// 3、恢复上下文
ptrace(PTRACE_SETREGS,...)
ptrace(PTRACE_DETACH,...)
```

- 通过 `dlsym` 获取函数地址
- 修改内存保护
调用 mprotect 函数，
```
#include <sys/mman.h>
int mprotect(void *addr, size_t len, int prot);
```

## 代码注入：修改进程中函数入口指令（由上述工具函数实现）
- 注入共享库
  - 1、在目标进程分配空间，将热补丁写入目标进程中进程
  - 2、获取目标进程 `dlopen` 函数地址
  - 3、调用 `dlopen` 加载热补丁

- 修改目标进程中目标函数指令
- 修改目标进程中目标函数所在页的权限为可写
- 写入跳转指令，跳转到热补丁中的函数地址
- 恢复内存保护

### 

### 
在目标进程中查找函数
### 


目标进程执行函数
当目标进程较大时链接动态库过多时，由本工具解析目标进程全部符号耗时会过长，可能导致目标进程心跳超时产生异常。
目前方案是先解析出目标进程 `dlsym` 函数地址，调用目标进程本地的 `dlsym` 函数解析需要打补丁的函数地址。

### 

### 

## 底层函数

### 系统调用
- syscall: 调用系统调用
```
syscall
```

- mmap: 将文件或设备的内存映射到进程的地址空间。多个进程可以直接访问同一块物理内存区域，而无需通过复制数据的方式进行通信。
```
#include <sys/mman.h>
void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset);

addr：映射的起始地址，通常为 NULL，表示由系统选择。
length：映射的长度。
prot：内存保护标志，如 PROT_READ、PROT_WRITE。
flags：控制映射的类型，如 MAP_SHARED、MAP_PRIVATE。
fd：文件描述符，用于指定要映射的文件。
offset：文件中的偏移量，通常为 0。
```

### process_vm_readv: 从另一个进程的虚拟内存空间读取数据的系统调用
```
#include <sys/uio.h>
#include <sys/socket.h>
ssize_t process_vm_readv(pid_t pid, const struct iovec *local_iov,
                         unsigned long liovcnt, const struct iovec *remote_iov,
                         unsigned long riovcnt, unsigned long flags);

pid：目标进程的进程 ID。
local_iov：指向本地 iovec 结构体数组的指针，用于存储从目标进程读取的数据。
liovcnt：本地 iovec 数组的元素个数。
remote_iov：指向远程（目标进程）iovec 结构体数组的指针，指定要读取的内存区域。
riovcnt：远程 iovec 数组的元素个数。
flags：目前必须设置为 0。
```

- sysconf: 获取系统配置信息
    _SC_PAGESIZE: 获取系统中内存页的大小

    
  

### ptrace
tracer: 本地热补丁工具生成的控制进程
tracee: 被控制进程(原进程）

- ptrace(PTRACE_ATTACH): 会向目标进程发送 SIGSTOP 信号，建立跟踪:
  进程在从内核空间返回用户空间时会检查是否有挂起信号，如果有的话，调用do_signal进行处理。do_signal需要调用get_signal来填充一个ksignal结构体。在get_signal过程中，会检查当前进程是否处于被ptrace跟踪的状态，如果是的话，且当前信号不是SIGKILL，则会调用ptrace_signal。ptrace_signal调用ptrace_stop来使当前进程停下来。
  ptrace_stop首先将进程状态设置为TASK_TRACED，这样的话，下次进行进程调度时就不会调度该进程了。设置完进程状态后，会发送SIGCHLD信号通知当前进程的parent和real_parent。最后调用freezable_schedule进行进程调度，将该进程停下来。
- PTRACE_GETREGSET: 获取寄存器信息：可替换当前寄存器信息，在目标进程内执行函数
- PTRACE_CONT： 使用 PTRACE_ATTACH 附加到 tracee 并暂停其执行后，让被附加并暂停的进程（tracee）继续执行；
- PTRACE_PEEKTEXT	读取目标进程内存	查看变量值
- PTRACE_SETOPTIONS: 设置追踪选项
  PTRACE_O_TRACEEXIT： 被跟踪进程在退出是停止其执行
  PTRACE_O_EXITKILL： 跟踪进程退出时，向所有被跟踪进程发送SIGKILL信号将其退出
  PTRACE_O_TRACEFORK： 被跟踪进程在下次调用fork()时停止执行，并自动跟踪新产生的进程
- PTRACE_SYSCALL: 在调试的进程中跟踪系统调用。当使用 PTRACE_SYSCALL 时，调试器会在每次系统调用的进入和退出时暂停进程，从而允许调试器捕获和检查系统调用的详细信息
- PTRACE_POKETEXT: 将数据写入目标进程的内存空间
- PTRACE_POKEDATA: 一次写入一个字(word)的长度(与系统位宽有关)
## 其他使用到的 linux 函数
### waitpid： ptrace attach目标进程后，目标进程会处于停止态，调用 WIFSTOPPED 判断目标进程状态
  WIFSTOPPED(status)	如果当前子进程被暂停了，则返回真；否则返回假；

### 
