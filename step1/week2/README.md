# 阶段 1 · 第 2 周：内存布局与未定义行为

本周目标：把“变量存在内存中”变成能观察、验证的事实，并学会让工具定位 C 的典型内存错误。

## 文件与顺序

| 文件 | 主题 | 运行方式 |
|---|---|---|
| `01_memory_layout.c` | 对齐、padding、成员偏移和存储期 | `./01_memory_layout` |
| `02_undefined_behavior.c` | 堆/栈越界、use-after-free、带符号整数溢出 | 见下文 |

## 编译与运行

在 WSL 中：

```bash
cd /mnt/d/claude/miniOS/step1/week2
make
./01_memory_layout
```

再以 sanitizer 构建故障实验。每次只运行一个模式；它们预期会报错并非测试失败。

```bash
make clean
make SANITIZE=1
./02_undefined_behavior overflow
./02_undefined_behavior heap-oob
./02_undefined_behavior stack-oob
./02_undefined_behavior use-after-free
```

AddressSanitizer 应定位 `heap-oob`、`stack-oob` 和 `use-after-free`，UndefinedBehaviorSanitizer 应报告 `overflow`。

## 观察要点

1. `struct compact` 与 `struct scattered` 有相同成员但不同字段顺序；比较大小和偏移，解释 padding 来自哪里。
2. 地址会因 ASLR 而每次不同；不要用绝对地址下结论，只比较同一次运行中的相对关系。
3. sanitizer 报告中的“写入位置”与“分配位置”通常共同指向根因；先读第一处用户代码栈帧。
4. 代码没有崩溃不代表正确。C 的未定义行为可能看似正常、产生错误结果，或只在优化/不同机器上暴露。

## 练习

1. 给 `struct compact` 增加一个 `short`，预测并验证新布局。
2. 新增一个 `stack-oob` 模式；运行后比较 ASan 给出的诊断和 `heap-oob` 的差异。
3. 用 GDB 查看 `show_layout()` 内的 `compact`：`p compact`、`p &compact`、`x/16xb &compact`。
4. 解释为什么释放后指针值仍可能非空，但解引用仍然错误。

完成标准：能解释对齐、padding、生命周期和三类故障的原因，并能根据 sanitizer 的首个栈帧定位到源代码。
