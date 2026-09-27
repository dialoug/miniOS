# Week 8：事件驱动 HTTP 服务

`01_http_server`把 Week 5 的非阻塞 TCP 连接管理与 Week 7 的目录 fd、文件 I/O 和请求状态机结合起来。它只监听 `127.0.0.1`，每个连接处理一条 HTTP/1.1 请求，发送响应后关闭。

## 构建与运行

```bash
cd /mnt/d/claude/miniOS/step2/week8
make
./01_http_server 8080 .
```

另一个终端：

```bash
curl --noproxy 127.0.0.1 -i http://127.0.0.1:8080/hello
curl --noproxy 127.0.0.1 -i http://127.0.0.1:8080/missing
curl --noproxy 127.0.0.1 -i -X POST http://127.0.0.1:8080/hello
curl --noproxy 127.0.0.1 http://127.0.0.1:8080/files/README.md \
  -o /tmp/minios-week8-readme-copy
cmp README.md /tmp/minios-week8-readme-copy
```

预期状态分别是 200、404、405。`GET /hello`的正文是`hello\n`，`Content-Length`为 6 字节。无效请求返回 400；请求头超过 8192 字节返回 431。

## 连接状态

```text
READING_HEADERS
  → 逐次读取到 EAGAIN，跨事件保留已收到的字节
  → 找到 CRLF CRLF，检查请求行与 Host
  → 生成完整响应
WRITING_HEADERS
  → 发送响应首部和固定正文，或只发送文件响应首部
WRITING_FILE_BODY
  → 从已打开的文件分块读取，发送到 EAGAIN 时保留进度
  → 文件内容全部发送后关闭文件 fd 与连接
```

HTTP/1.1 请求必须有且仅有一个非空 `Host`首部。本实验只接受不带请求体的消息，拒绝 `Content-Length`和`Transfer-Encoding`；不实现持久连接或流水线请求。响应包含`Content-Length`和`Connection: close`。服务端最多保留 256 个客户端连接，超出时关闭新连接。

第二个命令行参数是文件目录；省略时仍可访问 `/hello`，但 `/files/` 路由返回 404。`GET /files/NAME`只允许单层 ASCII 文件名：首字符为字母、数字或下划线，其余字符还可包含 `-`和 `.`。服务端通过目录 fd 调用`openat(..., O_NOFOLLOW | O_NONBLOCK)`，再用`fstat()`确认打开的是普通文件，因此不会跟随末尾的符号链接、进入子目录，或因打开 FIFO 而卡住事件循环。不存在或不符合规则的文件不会被发送。

文件响应的 `Content-Length`取打开时的文件大小；正文用 16 KiB 缓冲区逐块读取，单次事件最多发送约 64 KiB，避免大文件长期占用事件循环。`O_NONBLOCK`只防止打开 FIFO 时等待，并不使普通文件的`pread()`异步；慢磁盘仍可能短暂阻塞事件循环。若文件在传输过程中被截短，服务端会关闭该连接，客户端可观察到正文比声明长度短；本实验不提供一致性快照。

服务端通过 `signalfd`接收 `SIGINT`与`SIGTERM`，收到信号后退出并关闭现有连接。另有每秒触发一次的 `timerfd`：连接建立后须在 10 秒内完成请求头，即使期间陆续发送字节也不延长这个期限；开始响应后，若连续 30 秒没有成功写入字节，则关闭连接。期限基于 `CLOCK_MONOTONIC`，按周期检查，因此实际关闭会略晚于期限；事件循环被同步文件 I/O 阻塞时还会进一步延迟。超时时直接关闭连接，不尝试发送新的 HTTP 错误响应。清理过期客户端放在当前批次的 `epoll` 事件全部处理完之后，避免事件数组持有已释放的客户端指针。

已验证 200、404、405、缺少 `Host`时的 400、超长首部的 431、分段到达的请求，以及慢连接存在时其他客户端仍能完成请求。文件路由还验证了小文件和 1.5 MiB 文件逐字节一致、缺失文件返回 404、路径穿越返回 400、符号链接与 FIFO 返回 404。超时测试验证了不完整请求头约 10 秒后关闭、停止读取大文件响应约 30 秒后关闭，且超时后普通请求仍可完成。使用 `curl` 时显式绕过本机代理，确保请求直达实验服务端。
