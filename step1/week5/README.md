# 阶段 1 · 第 5 周：进程、文件描述符与管道

本周目标：理解进程与线程的根本区别，掌握 Unix 进程启动的
`fork()` → `exec()` → `waitpid()` 流程，以及文件描述符和管道的所有权规则。

## 文件与学习顺序

| 文件 | 主题 | 运行方式 |
|---|---|---|
| `01_fork_identity.c` | `fork()`、PID、写时复制和 `waitpid()` | `./01_fork_identity` |
| `02_exec_wait.c` | `exec` 替换子进程、退出状态 | `./02_exec_wait` |
| `03_pipe_parent_child.c` | `pipe()`、`read`/`write`、关闭无用端 | `./03_pipe_parent_child` |
| `04_exec_pipeline.c` | `dup2()` 与真正的 `ls | wc -l` | `./04_exec_pipeline` |

## 构建与运行

```bash
cd /mnt/d/claude/miniOS/step1/week5
make

./01_fork_identity
./02_exec_wait
./03_pipe_parent_child
./04_exec_pipeline
```

观察第四个程序执行的系统调用：

```bash
strace -f -e trace=process,pipe,pipe2,dup2,close ./04_exec_pipeline
```

## 心智模型

一个 shell 启动外部命令时，核心步骤通常是：

```text
shell
  │ fork()                 父、子进程从这里分开执行
  ├─ 父进程：waitpid()      等待并回收子进程
  └─ 子进程：重定向 fd
             exec()        用目标程序完整替换自己
```

`fork()` 后父子进程各有独立的用户态地址空间；修改普通变量不会彼此可见。
但它们会继承指向相同“打开文件描述”的文件描述符，因此文件偏移等内核状态可能共享。

文件描述符（fd）是进程中的小整数句柄：`0` 是标准输入、`1` 是标准输出、`2` 是标准错误。
`pipe()` 返回两个 fd：`pipefd[0]` 用于读，`pipefd[1]` 用于写。

管道读端只有在**所有**写端都关闭后才会得到 EOF。因此 `fork()` 后，父子双方都必须关闭自己不用的那一端；否则读者可能永远等不到 EOF。

## 阅读问题

1. `fork()` 的返回值为什么让同一行之后出现父、子两条执行路径？
2. 为什么 `fork()` 后子进程中 `counter += 1` 不会改变父进程的 `counter`？
3. `exec` 成功后，为什么 `execlp()` 后面的语句绝不执行？
4. 为什么 `exec` 失败时子进程应调用 `_exit(127)`，而不是从 `main` 正常返回？
5. 若父进程忘记关闭 `pipefd[1]`，子进程的读循环为什么无法遇到 EOF？
6. 为什么 shell 必须先启动管道两端，最后才能 `waitpid()`？

完成标准：能说明 shell 的 `fork`/`exec`/`waitpid` 流程，能解释 fd 继承与管道 EOF，
并能用 `WIFEXITED`/`WEXITSTATUS` 正确读取子进程状态。
