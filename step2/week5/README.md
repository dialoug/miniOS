# Week 5：TCP socket 与 epoll 服务器设计

本周已经完成事件驱动 echo server 的设计推导，并用 `01_epoll_echo.c` 实现第一版服务器。

## 构建与运行

终端 1：

```bash
cd /mnt/d/claude/miniOS/step2/week5
make
./01_epoll_echo 9090
```

终端 2（安装了 `netcat-openbsd` 时）：

```bash
printf 'hello epoll\n' | nc -N 127.0.0.1 9090
```

验证背压路径：

```bash
./02_slow_client 9090
```

慢客户端把接收缓冲区调小，使用非阻塞 `send()` 持续写入；瞬时 `EAGAIN` 后用 `poll()` 等待，直到连续 100 ms 不可写才确认背压。随后暂停读取一秒，再接收全部回显。最终 `queued` 与 `received` 字节数必须一致，服务器应打印 `paused reads` 与 `resumed reads`。

服务端只绑定 `127.0.0.1`，不会对局域网或公网开放。按 `Ctrl+C` 后停止接收新连接，并给现有连接 3 秒退出宽限期。

## 连接生命周期

```text
socket(SOCK_NONBLOCK | SOCK_CLOEXEC)
  → setsockopt(SO_REUSEADDR)
  → bind()
  → listen()
  → epoll_ctl(ADD, listen_fd)
  → accept4(SOCK_NONBLOCK | SOCK_CLOEXEC)
  → epoll_ctl(ADD, client_fd)
```

`listen_fd` 的 `EPOLLIN` 表示 accept 队列中有连接；每次 `accept4()` 返回的 `client_fd` 才承载具体客户端的业务数据。

## 每个客户端的状态

```text
client_fd
input/output buffer
output offset
是否暂停读取
对端是否关闭发送方向
```

- TCP 是字节流，一次 `read()` 不等于一条完整消息。
- 短写或 `EAGAIN` 后必须保存未发送数据，并临时关注 `EPOLLOUT`。
- 输出缓冲区清空后取消 `EPOLLOUT`，避免 socket 长期可写造成无效唤醒。
- `read() == 0` 表示对端发送方向 EOF；若仍有待发送数据，应排空后再关闭。
- 确认 EOF 后取消 `EPOLLIN`/`EPOLLRDHUP`；若仍有积压，只等待 `EPOLLOUT`，避免关闭状态反复唤醒事件循环。
- `send(..., MSG_NOSIGNAL)` 可避免单个断开的客户端用 `SIGPIPE` 终止整个服务器。

## 背压

若客户端发送很快但读取回显很慢，输出缓冲区会增长。达到高水位后应暂停 `EPOLLIN`；降到低水位再恢复，或者超过硬上限时关闭连接。

## 信号与退出

异步信号处理函数只适合修改 `volatile sig_atomic_t` 标志。Linux 服务器也可将阻塞的 `SIGINT`/`SIGTERM` 转成 `signalfd`，注册到 epoll 后在普通事件循环上下文中执行关闭流程。

## 实现范围

第一版使用 epoll 水平触发，实现：

- 非阻塞监听与接受连接；
- 每客户端输出缓冲区；
- 动态启停 `EPOLLOUT`；
- 半关闭与资源回收；
- 背压与优雅退出；
- `strace`、并发客户端与大数据回显测试。

建议先按以下函数顺序阅读：

```text
main
  → create_listener / create_signal_fd
  → run_server
  → accept_clients / add_client
  → handle_client_event
  → read_client / flush_client
  → update_client_interest / destroy_client
```

观察系统调用：

```bash
strace -f -e trace=network,epoll_wait,epoll_ctl,read,write,close \
  ./01_epoll_echo 9090
```

完成标准：实现上述服务器，并能解释一次连接从 accept 队列、client fd、epoll 就绪到最终 close 的完整生命周期。
