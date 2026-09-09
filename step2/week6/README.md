# Week 6：mini-shell、信号与作业控制

本周目标：从命令行参数开始，逐步实现一个支持外部命令、内建命令、重定向、管道和前台作业控制的最小 shell。

## 实现顺序

| 文件 | 主题 | 状态 |
|---|---|---|
| `01_argv_inspect.c` | 观察现有 shell 生成的 `argc`/`argv` | 已实现 |
| `02_parse_command.c` | 原地切分输入，建立以 `NULL` 结尾的 `argv` | 已实现 |
| `03_mini_shell.c` | `getline`、内建命令、`fork`/`execvp`/`waitpid` | 已实现 |
| 后续扩展 | 重定向、管道、信号与前台进程组 | 待实现 |

## 第一项：观察 `argv`

```bash
cd /mnt/d/claude/miniOS/step2/week6
make

./01_argv_inspect alpha beta
./01_argv_inspect alpha "two words" '*.c'
./01_argv_inspect alpha two words *.c
```

需要观察：

- `argv[0]` 是调用者提供的程序名，不保证是规范化绝对路径。
- 双引号负责让 `two words` 成为一个参数，引号本身不会传给程序。
- 单引号中的 `*.c` 不展开；未加引号的 `*.c` 会先由当前 shell 展开成多个文件名。
- `argv[argc]` 必须是空指针，`execvp()` 用它识别参数数组末尾。

## shell 与程序的边界

程序收到的是已经处理好的字符串数组：

```text
用户输入文本
  → shell 解析引号、转义、变量和通配符
  → 构造 argv[]
  → execvp(argv[0], argv)
  → 新程序从 main(argc, argv) 接收参数
```

第一版自己的解析器只支持空白分隔，不支持引号、转义、变量替换和 glob；这些能力会在基本进程生命周期正确后再逐步加入。

## 第二项：原地构造 `argv`

```bash
./02_parse_command
parse> ls -l /tmp
parse> echo hello world
```

输入 EOF（交互终端中按 `Ctrl+D`）结束程序。解析器把每个分隔空白替换为 `\0`，让 `argv` 的各个元素指向同一块 `getline()` 缓冲区：

```text
输入：ls -l /tmp\n
内存：ls\0-l\0/tmp\0
argv：[&ls, &-l, &/tmp, NULL]
```

当前最多接受 8 个参数；超限时报告错误并丢弃当前行。这个限制让第一版的数组边界与失败路径保持清楚。

完成标准：能区分“用户输入的命令行文本”和“程序收到的 `argv`”，并能解释为什么 `argv[argc]` 必须为 `NULL`。

## 第三项：执行外部命令与内建命令

```bash
./03_mini_shell
mini$ pwd
mini$ cd /tmp
mini$ pwd
mini$ printf hello
mini$ command-that-does-not-exist
mini$ exit 0
```

外部命令的生命周期是：

```text
shell 读取并解析命令
  → fork() 创建子进程
  → 子进程用 execvp() 替换自身程序
  → 父进程用 waitpid() 等待并提取退出状态
```

`execvp()` 中的 `p` 表示根据 `PATH` 搜索程序。命令不存在时子进程返回 127；找到了但不能执行时返回 126。

`cd` 和 `exit` 是内建命令，由父进程直接处理。若在子进程中执行 `chdir()`，改变的只是子进程自己的当前目录，子进程退出后 shell 的目录不会改变。

当前解析器仍然只支持空白分隔；引号、转义、重定向、管道和作业控制属于后续阶段。完成标准：能够说明为什么外部命令在子进程中执行，而 `cd` 必须在 shell 进程中执行。
