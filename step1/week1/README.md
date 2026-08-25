# 阶段 1 · 第 1 周：C 与 C++ 的关键差异

本周目标：从熟练使用 C++ 过渡到能以 C 的思维阅读和编写代码，为后续阅读 Linux Kernel 做准备。

## 示例阅读顺序

| 文件 | 主题 | 与 C++ 的关键差异 |
|---|---|---|
| `01_arrays_and_pointers.c` | 数组、指针、字符串 | 数组传入函数后退化为指针；字符串可变性需显式区分。 |
| `02_structs_and_callbacks.c` | 结构体、函数指针、回调 | 常用“结构体 + 回调”组织行为，而不是成员函数/虚函数。 |
| `03_manual_resources.c` | 资源所有权、错误码、清理路径 | 没有 RAII 与异常；每个失败路径都须显式释放已获得的资源。 |
| `04_macros_and_container_of.c` | 宏、`offsetof`、`container_of` | 宏不做类型检查；`container_of` 是理解内核侵入式数据结构的基础。 |
| `05_dynamic_array.c` | 综合练习：动态数组 | 练习结构体、指针、堆所有权、`realloc` 与错误码。 |

## 编译与运行

在具备 GCC 或 Clang 的 Linux/WSL 环境中：

```bash
make
./01_arrays_and_pointers
./02_structs_and_callbacks
./03_manual_resources README.md
./04_macros_and_container_of
./05_dynamic_array
```

建议再以 sanitizer 编译一次，观察工具如何帮助发现内存问题：

```bash
make clean
make SANITIZE=1
```

当前 Windows 环境未检测到 GCC/Clang/Make，因此示例尚未在本机编译；可在 WSL 或安装 MinGW/LLVM 后执行以上命令。

## 本周练习

1. 完成 `05_dynamic_array.c` 中的三个 TODO；完成后输出应显示一次从 capacity 4 到 8 的扩容。
2. 修改示例 1：让 `increment_all()` 只修改数组的偶数下标元素。
3. 修改示例 2：新增一个回调，筛选并打印分数不低于 90 的学生。
4. 修改示例 3：改成读取整个文件；确保每一条失败路径都没有泄漏资源。
5. 修改示例 4：给 `struct list_node` 增加另一个业务字段，验证 `CONTAINER_OF` 仍能取得宿主结构体。

## 完成标准

- 不依赖 STL、RAII 或异常理解并运行所有示例。
- 能说明数组与指针的区别，以及常见字符串越界风险。
- 能读懂并编写使用函数指针回调的 C 代码。
- 能画出示例 3 中每一条资源获取与释放路径。
