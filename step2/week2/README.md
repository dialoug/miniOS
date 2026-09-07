# Week 2：文件 I/O 与可靠更新

基础程序位于 [`step1/week6`](../../step1/week6/README.md)：使用 `open()`、`read()`、`write()`、`close()` 实现 `mini_cp`。

## 已掌握内容

### `read()` 与 `write()`

```text
read > 0   实际读取字节数
read == 0  EOF
read == -1 失败，此时检查 errno
```

- 成功调用不保证清零 `errno`；只有返回值表示失败时，`errno` 才描述本次错误。
- `EINTR` 表示调用被信号打断，许多读写循环可以重试。
- 短读、短写不是错误；程序必须维护已经完成的偏移量。
- `EBADF` 表示 fd 无效或访问模式不允许，`EIO` 表示底层 I/O 错误。

### fd、打开文件描述与 inode

```text
fd → struct file（offset、flags）→ inode（文件对象）
```

- 两次独立 `open()` 通常拥有独立偏移量。
- `dup()` 和 `fork()` 继承的 fd 共享底层打开状态。
- `unlink()` 删除名称到 inode 的映射；已有 fd 仍能访问该 inode。
- `st_dev + st_ino` 可判断不同路径是否指向同一文件，包括硬链接或符号链接最终解析出的目标。

### 安全覆盖

不能在确认源、目标不同之前以 `O_TRUNC` 打开目标，否则同一 inode 会被提前清空。

更可靠的原子更新模式是：

```text
同目录创建临时文件
  → 完整写入
  → fsync(临时文件)
  → rename(临时文件, 目标)
  → fsync(目标目录)
```

`rename()` 保证名称切换的原子可见性；两个 `fsync()` 分别处理文件内容和目录项的持久化。跨文件系统不能依赖 `rename()` 原子替换。

## 复习实验

```bash
cd /mnt/d/claude/miniOS/step1/week6
make
printf 'hello\n' > source.txt
./01_mini_cp source.txt copy.txt
cmp source.txt copy.txt

strace -e trace=openat,read,write,close,ftruncate \
  ./01_mini_cp source.txt copy.txt
```

完成标准：能写出正确的短写循环，区分 EOF、`EAGAIN`、`EINTR` 与真正错误，并解释为什么源、目标同 inode 时不能先截断。
