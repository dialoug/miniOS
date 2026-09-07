# Week 4：非阻塞 I/O 与就绪通知

本周把管道与 socket 的阻塞行为推广到 `select()`、`poll()` 和 Linux `epoll`。

## 阻塞与非阻塞

fd 设置 `O_NONBLOCK` 后，暂时无法完成的操作不会让线程睡眠，而是返回：

```text
-1，errno == EAGAIN 或 EWOULDBLOCK
```

这不等于 EOF。EOF 表示未来不会再有数据；`EAGAIN` 表示当前没有数据、以后可能有。

非阻塞 fd 应与就绪通知配合。读到 `EAGAIN` 后回到 `poll()`/`epoll_wait()` 睡眠；若立即反复重试，就会形成消耗 CPU 的 busy loop。

## 三种接口

| 接口 | 关注集合 | 返回后的处理 | 适用范围 |
|---|---|---|---|
| `select` | 每轮提交 fd 位图 | 扫描到最大 fd | POSIX、小型或遗留程序 |
| `poll` | 每轮提交 `pollfd` 数组 | 扫描整个数组 | POSIX、少量 fd |
| `epoll` | 内核持久登记 | 返回本次就绪项 | Linux、大量连接 |

`epoll_ctl()` 用于维护关注集合：

```text
EPOLL_CTL_ADD  首次登记 fd
EPOLL_CTL_MOD  修改 EPOLLIN/EPOLLOUT 等事件
EPOLL_CTL_DEL  移除 fd
```

## 水平触发与边缘触发

- 水平触发：只要 fd 仍就绪，后续 `epoll_wait()` 继续报告，较容易写对。
- 边缘触发（`EPOLLET`）：主要在状态从未就绪变为就绪时通知；必须使用非阻塞 fd，并循环到 `EAGAIN`。

实践顺序：先实现“非阻塞 + 水平触发”，确认性能瓶颈后再评估 `EPOLLET`。

完成标准：能解释为什么非阻塞不等于忙等，能够根据 fd 数量与活跃比例选择 `poll` 或 `epoll`，并写出读到 `EAGAIN` 的处理循环。
