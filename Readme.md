# 使用



# 原理
## ptrace
tracer: 本地热补丁工具生成的控制进程
tracee: 被控制进程(原进程）

- ptrace(PTRACE_ATTACH): 会向目标进程发送 SIGSTOP 信号，建立跟踪:
  进程在从内核空间返回用户空间时会检查是否有挂起信号，如果有的话，调用do_signal进行处理。do_signal需要调用get_signal来填充一个ksignal结构体。在get_signal过程中，会检查当前进程是否处于被ptrace跟踪的状态，如果是的话，且当前信号不是SIGKILL，则会调用ptrace_signal。ptrace_signal调用ptrace_stop来使当前进程停下来。
  ptrace_stop首先将进程状态设置为TASK_TRACED，这样的话，下次进行进程调度时就不会调度该进程了。设置完进程状态后，会发送SIGCHLD信号通知当前进程的parent和real_parent。最后调用freezable_schedule进行进程调度，将该进程停下来。
- PTRACE_GETREGSET: 获取寄存器信息：可替换当前寄存器信息，在目标进程内执行函数
- PTRACE_CONT： 使用 PTRACE_ATTACH 附加到 tracee 并暂停其执行后，让被附加并暂停的进程（tracee）继续执行；
- PTRACE_SETOPTIONS: 设置追踪选项
  PTRACE_O_TRACEEXIT： 被跟踪进程在退出是停止其执行
  PTRACE_O_EXITKILL： 跟踪进程退出时，向所有被跟踪进程发送SIGKILL信号将其退出
  PTRACE_O_TRACEFORK： 被跟踪进程在下次调用fork()时停止执行，并自动跟踪新产生的进程
## 其他使用到的 linux 函数
### waitpid： ptrace attach目标进程后，目标进程会处于停止态，调用 WIFSTOPPED 判断目标进程状态
  WIFSTOPPED(status)	如果当前子进程被暂停了，则返回真；否则返回假；

### 
