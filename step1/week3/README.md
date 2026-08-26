# 阶段 1 · 第 3 周：编译、链接、ELF 与调试

本周目标：把一条 `make` 命令拆成可观察的阶段，理解源文件为何能分别编译、链接器如何解析符号，以及 ELF 中保存了什么。

## 本周项目

`main.c` 调用定义在 `math_ops.c` 中的 `add()`。它们通过 `math_ops.h` 共享函数声明。

| 文件 | 作用 |
|---|---|
| `main.c` | 程序入口与 `add()` 的调用点 |
| `math_ops.c` | `add()` 的函数定义 |
| `math_ops.h` | 两个翻译单元共同遵守的接口 |
| `Makefile` | 每个构建阶段的可重复命令 |

## 从源码到可执行文件

```text
.c + .h
  └─ 预处理（展开 #include/#define） → .i
       └─ 编译（语法/优化/生成汇编） → .s
            └─ 汇编（机器码与重定位） → .o
                 └─ 链接（解析跨文件符号、加入运行时依赖） → elf_demo
```

`.c` 文件分别编译后的结果叫**翻译单元**。`main.o` 知道自己需要 `add`，但不知道其地址；`math_ops.o` 提供 `add`。链接器把两者配对。

## 构建与运行

```bash
cd /mnt/d/claude/miniOS/step1/week3
make
./build/elf_demo
```

预期输出：

```text
40 + 2 = 42
```

## 逐阶段观察

```bash
make clean
make preprocess  # 查看 build/main.i 与 build/math_ops.i
make assembly    # 查看 build/main.s 与 build/math_ops.s
make objects     # 生成两个 .o 文件
make             # 链接成 build/elf_demo
```

建议命令：

```bash
less build/main.i
grep -nE "add|printf" build/main.i
grep -n "add" build/main.s
nm -C build/main.o
nm -C build/math_ops.o
readelf -h build/elf_demo
readelf -S build/elf_demo
readelf -s build/elf_demo | grep -E "add|main"
readelf -r build/main.o
objdump -d -S build/elf_demo | less
ldd build/elf_demo
```

重点观察：`main.o` 中 `add` 是 `U`（undefined），而 `math_ops.o` 中是 `T`（text section 中的定义）。链接完成后，最终 ELF 的符号表中可以找到 `add`。

## 故意制造链接错误

```bash
make broken-link
```

这个目标只链接 `main.o`，预期报出 `undefined reference to 'add'`。这是正常实验结果；恢复时执行 `make` 即可。

## GDB 跟踪调用

```bash
gdb ./build/elf_demo
```

在 GDB 中：

```gdb
break main
run
next
step
info locals
backtrace
disassemble /m add
continue
```

`next` 运行当前行但不进入函数；`step` 会进入 `add()`。对比调用栈和反汇编，建立“源码调用 → 函数符号 → 指令跳转”的连接。

## 思考题

1. 为什么 `main.c` 有声明就能独立生成 `main.o`，却不能单独链接成程序？
2. `.o` 文件为何还不能直接运行？
3. 为什么头文件通常只放声明，而把函数定义放入 `.c`？
4. 链接器如何区分“某符号需要别人提供”与“某符号由当前文件提供”？
5. 修改 `math_ops.h` 后，为什么 `main.c` 和 `math_ops.c` 都应重新编译？

完成标准：能够不看资料解释 `.i`、`.s`、`.o`、ELF 的职责，读懂一次 `undefined reference`，并在 GDB 中从 `main` 单步进入 `add`。
