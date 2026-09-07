# 阶段 2 · 第 6 周：文件 I/O 与系统调用

本周目标：用 `open()`、`read()`、`write()`、`close()` 实现一个可靠的最小文件复制工具，
并理解 fd 如何进入 VFS 与具体文件系统。

## 文件与学习顺序

| 文件 | 主题 | 运行方式 |
|---|---|---|
| `01_mini_cp.c` | 文件描述符、短读/短写、`EINTR`、错误路径 | `./01_mini_cp SOURCE DEST` |

## 构建、运行与观察

```bash
cd /mnt/d/claude/miniOS/step1/week6
make

printf 'hello\n' > source.txt
./01_mini_cp source.txt copy.txt
cmp source.txt copy.txt
```

观察核心系统调用：

```bash
strace -e trace=openat,read,write,close,newfstatat,ftruncate \
  ./01_mini_cp source.txt copy.txt
```

## `01_mini_cp` 的边界

- 只复制普通文件；目录、设备和符号链接不在本练习范围内。
- 若源文件和目标文件是同一个 inode，程序在截断目标前拒绝操作，避免清空源文件。
- 已存在的目标文件会被截断；新建目标文件请求权限为源文件权限的低 9 位，仍受 `umask` 影响。
- 复制过程中出错时，目标可能已经被部分写入。本练习不提供临时文件加 `rename()` 的原子替换。

## 核心规则

### `read()` 的三种结果

```text
> 0  实际读取的字节数；不保证等于请求长度
0    EOF
-1   出错，查看 errno；EINTR 通常应重试
```

### `write()` 也可能短写

一次 `write(fd, buffer, count)` 只承诺写入不超过 `count` 的字节数。
因此必须循环，直到缓冲区全部写出；不能把“返回值非负”误认为“整块数据已经写完”。

### fd、`struct file` 与 inode

```text
fd（进程内编号） → struct file（本次打开的 offset/flags） → inode（文件对象）
```

两次 `open()` 同一路径通常生成两份独立的 `struct file`；`dup()` 或 `fork()` 继承 fd
则会共享同一份 `struct file`。

## 阅读问题

1. 为什么 `read()` 返回 0 不是错误，而是 EOF？
2. 为什么 `write_all()` 必须维护已经写出的偏移量？
3. 为什么不能一开始就以 `O_TRUNC` 打开目标文件？
4. `fstat()` 比较 `st_dev` 与 `st_ino` 为什么能发现源、目标是同一个文件？
5. 若要保证复制失败时旧目标文件完全不变，应如何使用临时文件和 `rename()`？

完成标准：能解释短读、短写、`EINTR`、EOF 与 `errno` 的区别；能用 `strace` 将
`open`/`read`/`write`/`close` 对应到程序中的代码路径。
