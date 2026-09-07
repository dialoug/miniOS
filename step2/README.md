# 阶段 2：Linux 用户态编程

建议时长：8–12 周。本阶段把用户态 API 连接到可观察的内核行为，为后续沿系统调用纵向阅读 Linux Kernel 做准备。

## 当前进度

| 周 | 主题 | 状态 | 对应实验 |
|---|---|---|---|
| Week 1 | 进程、fd、pipe 与重定向 | 已学习 | `step1/week5` |
| Week 2 | 文件 I/O、错误处理与安全覆盖 | 已学习基础 | `step1/week6` |
| Week 3 | pthread、同步与有界队列 | 已学习 | `step1/week4` |
| Week 4 | 非阻塞 I/O、`select`/`poll`/`epoll` | 已学习原理 | 待加入观察程序 |
| Week 5 | socket 与 `epoll` echo server 设计 | 已学习设计 | 下一项实现任务 |
| Week 6 | mini-shell、信号与作业控制 | 待学习 | — |
| Week 7 | 目录、`mmap`、文件锁与 IPC | 待学习 | — |
| Week 8 | 事件驱动网络程序整合 | 待学习 | — |

前几项实验最初作为基础阶段的过渡内容建立，因此本目录使用链接复用它们，不复制源码。这样能保留已有提交历史，并避免两份代码逐渐不一致。

## 学习方法

每个主题都按同一顺序推进：

```text
阅读 API 契约
  → 运行最小程序
  → 用 strace / GDB / /proc 验证
  → 解释失败路径与资源所有权
  → 再组合成项目
```

建议观察工具：

```bash
strace -f ./program
lsof -p PID
ls -l /proc/PID/fd
gdb ./program
perf stat ./program
```

阶段完成标准：能够实现 mini-shell、线程池、文件工具和事件驱动服务器，并能把 `open`、`fork`、`read`、`epoll_wait` 等用户态行为对应到内核对象和系统调用路径。

