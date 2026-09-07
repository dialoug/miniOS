# Week 1：进程、文件描述符与管道

本周内容已经通过 [`step1/week5`](../../step1/week5/README.md) 的四个程序完成；这里作为阶段 2 的知识索引，不复制源码。

## 已掌握内容

### `fork()`

- 父进程得到子进程 PID，子进程得到 `0`，失败返回 `-1`。
- 父、子拥有独立虚拟地址空间；普通内存修改彼此不可见，初始页面通常使用写时复制。
- fd 表项被复制，但父、子的表项可引用同一个底层打开文件描述，因此文件偏移量等状态可能共享。

### `exec()` 与 `waitpid()`

- `exec` 不创建新进程，而是用新程序替换当前进程映像；PID 保持不变。
- `exec` 成功后不会返回；失败的子进程通常使用 `_exit(127)`。
- 父进程用 `waitpid()` 回收退出记录，避免僵尸进程。
- 必须先用 `WIFEXITED` 或 `WIFSIGNALED` 判断结束原因，再读取退出码或信号编号。

### fd、pipe 与 `dup2()`

```text
pipefd[1] → 内核管道缓冲区 → pipefd[0]
     写                           读
```

- 管道是字节流，不保存 `write()` 的消息边界；读写均可能只完成一部分。
- 管道为空且所有写端关闭时，`read()` 才返回 `0`（EOF）。
- `dup2(pipefd[1], STDOUT_FILENO)` 是 shell 输出重定向的核心。
- `fork()` 后每个进程必须关闭不用的管道端，避免 EOF 永远无法出现。

## 复习实验

```bash
cd /mnt/d/claude/miniOS/step1/week5
make
./01_fork_identity
./02_exec_wait
./03_pipe_parent_child
./04_exec_pipeline

strace -f -e trace=process,pipe,pipe2,dup2,close ./04_exec_pipeline
```

完成标准：能够画出 `ls | wc -l` 中父进程、两个子进程和管道端点的所有权关系，并解释每次 `close()` 的必要性。
