# Week 7：目录、内存映射、文件锁与 IPC

本周从“打开一个已知文件”推进到目录遍历、内存映射和进程间文件锁。`01_mini_find` 不跟随遍历过程中遇到的符号链接，并且用目录 fd 作为每次子项操作的锚点；`02_mmap_reader` 用只读私有映射输出普通文件内容；`03_locked_counter` 用 `fcntl()` 写锁保护跨进程的“读取—修改—写回”事务。

## 构建与运行

```bash
cd /home/i/项目/work/miniOS/step2/week7
make

./01_mini_find .
./01_mini_find /tmp

./02_mmap_reader README.md
./02_mmap_reader README.md > copied-readme.md
cmp README.md copied-readme.md
```

每条输出包含对象类型、权限位、inode、大小和显示路径。例如：

```text
file             -rw-r--r-- inode=123 size=42 ./README.md
directory        drwxr-xr-x inode=124 size=4096 ./subdir
symlink          lrwxrwxrwx inode=125 size=8 ./link
```

## 实现路径

```text
open(root, O_DIRECTORY)
  → fdopendir(root_fd)
  → readdir()
  → fstatat(dirfd(directory), name, ..., AT_SYMLINK_NOFOLLOW)
  → openat(parent_fd, name, O_DIRECTORY | O_NOFOLLOW)
  → 递归处理子目录
  → closedir()
```

## 需要观察的细节

- `readdir()` 返回的是名字；不能依赖 `d_type` 判断真实对象类型。
- `fstatat(..., AT_SYMLINK_NOFOLLOW)`检查的是链接本身，不会把链接目标误判为普通文件或目录。
- `fdopendir()`接管传入 fd 的所有权，随后必须由 `closedir()`关闭，而不是再单独 `close()`。
- 对目录使用 `openat()`，可避免通过拼接路径字符串再次解析父路径；`O_NOFOLLOW`避免在打开子目录时跟随符号链接。
- `fstatat()`到`openat()`之间，目录项可能被并发替换。程序重新 `fstat()`已打开的目录并对比 inode，发现变化时停止进入该目录。

## 边界

- 最大递归深度为 64，防止异常目录结构或资源消耗失控。
- 显示路径仍需拼接，仅用于输出；真正的子项访问基于目录 fd。
- 未实现跨文件系统策略、忽略规则、按名称过滤或并行遍历。

## `mmap` 文件读取

`02_mmap_reader`先用`fstat()`取得普通文件的当前大小，再执行：

```text
mmap(NULL, size, PROT_READ, MAP_PRIVATE, file_fd, 0)
  → write_all(STDOUT_FILENO, mapped_address, size)
  → munmap(mapped_address, size)
```

`mmap()`建立的是虚拟地址区间和文件页之间的映射；它不要求在调用时就把整个文件复制进用户缓冲区。程序第一次访问某一页时，内核可通过缺页处理从页缓存提供该页。

- 空文件直接成功：长度为 0 时不能调用 `mmap()`。
- 映射建立后可关闭原始 fd；映射本身仍有效。本练习为了让资源生命周期更直观，在 `munmap()`后才关闭 fd。
- `MAP_PRIVATE`配合`PROT_READ`使程序不会修改文件；这不是共享可写映射。
- 文件若在映射后被其他进程截短，后续访问已失效页面可能触发 `SIGBUS`。本最小实验不尝试处理这种并发修改。

可观察系统调用：

```bash
strace -e trace=openat,newfstatat,mmap,munmap,write,close \
  ./02_mmap_reader README.md > /dev/null
```

## `fcntl()` 文件锁

先观察故意不加锁的丢失更新：

```bash
printf '0\n' > counter.txt
./03_locked_counter --no-lock counter.txt 100 &
./03_locked_counter --no-lock counter.txt 100 &
wait
cat counter.txt
```

两个进程总共尝试递增 200 次，但结果通常接近 100。`--no-lock` 模式在读取和写回之间故意等待 1 ms，使竞争更容易复现。

再观察阻塞写锁：

```bash
printf '0\n' > counter.txt
./03_locked_counter counter.txt 100 &
./03_locked_counter counter.txt 100 &
wait
cat counter.txt
```

结果应稳定为 200。每次递增的临界区是：

```text
fcntl(F_SETLKW, F_WRLCK)
  → lseek + read
  → value + 1
  → ftruncate + lseek + write
  → fcntl(F_SETLK, F_UNLCK)
```

锁是建议性的：不遵守同一加锁协议的其他程序仍能直接修改文件。锁能协调并发访问，但不能保证进程崩溃时的持久化原子性；`ftruncate()`后、`write()`前崩溃仍可能留下空文件。

## FIFO 消息传递

终端 1 启动服务端：

```bash
./04_fifo_server counter.fifo
```

终端 2 发送单向命令：

```bash
./04_fifo_client counter.fifo increment
./04_fifo_client counter.fifo increment
./04_fifo_client counter.fifo get
./04_fifo_client counter.fifo quit
```

服务端独占内存中的计数值；客户端只发送命令，不直接接触共享状态。服务端输出应依次包含计数值 1、2、2，并在 `quit` 后退出。

当前协议以换行符分隔消息。客户端将整条短消息通过一次逻辑写操作发送；服务端仍使用增量解析器，因为字节流的一次 `read()`不保证对应一次 `write()`。

服务端以只读方式打开 FIFO。每批写端全部关闭后，`read()`返回 0；服务端关闭读端并重新打开，等待未来客户端。若 FIFO 是本次服务端创建的，它会在正常退出或收到 `SIGINT`/`SIGTERM` 后删除路径；若启动时路径已经是 FIFO，则保留它。

## Unix Domain Socket 请求—响应

终端 1：

```bash
./05_unix_server counter.sock
```

终端 2：

```bash
./05_unix_client counter.sock increment
./05_unix_client counter.sock increment
./05_unix_client counter.sock get
./05_unix_client counter.sock quit
```

每个客户端都通过独立连接收到响应：

```text
counter=1
counter=2
counter=2
counter=2; shutting-down
```

服务端的连接生命周期是：

```text
socket(AF_UNIX, SOCK_STREAM)
  → bind(socket path)
  → listen()
  → accept4() 得到独立 client fd
  → 读取一条换行命令
  → 在同一连接发送响应
  → close(client fd)
```

客户端发送请求后调用 `shutdown(SHUT_WR)`，表示不再发送但仍继续读取响应。服务端不覆盖任何已存在的 socket 路径；成功绑定后只在退出时删除自己创建的路径。此版本一次处理一个客户端；下节的 `06_epoll_unix_server` 提供非阻塞多客户端版本。

## 非阻塞 Unix Socket 与 `epoll`

`06_epoll_unix_server`复用现有的`05_unix_client`：

```bash
# 终端 1
./06_epoll_unix_server epoll-counter.sock

# 终端 2，可并发启动多个客户端
./05_unix_client epoll-counter.sock increment &
./05_unix_client epoll-counter.sock increment &
./05_unix_client epoll-counter.sock get &
wait

./05_unix_client epoll-counter.sock quit
```

事件循环同时管理：

```text
listen fd   → EPOLLIN 时 accept4() 到 EAGAIN
signal fd   → 将 SIGINT/SIGTERM 作为普通可读事件处理
client fd   → EPOLLIN 增量读取，EPOLLOUT 继续短写后的响应
```

每个客户端都有独立的输入长度、输出偏移和读取阶段。解析出一条请求后取消 `EPOLLIN`；响应全部发送后关闭连接。`EPOLLOUT`只在确有待发送数据时启用，避免 socket 长期可写造成事件循环空转。

收到 `quit` 或退出信号后，服务端停止接受新连接；已有客户端处理完成后退出并删除 socket 路径。当前没有退出超时，故一个已连接但始终不发送完整请求的客户端可能延迟退出。
