# Week 3：pthread、同步与共享状态

本周实验位于 [`step1/week4`](../../step1/week4/README.md)，包括线程生命周期、计数器数据竞争和有界队列。

## 已掌握内容

- `pthread_create()` 启动线程，`pthread_join()` 等待结束并建立结果交接点。
- C 数据竞争属于未定义行为；一次输出正确不能证明程序没有竞争。
- `counter++` 是读—修改—写，不是天然原子操作；`volatile` 不能提供互斥或内存顺序。
- mutex 保护共享状态和跨多个字段的不变量。
- 条件变量不保存通知，必须在持锁状态下用 `while` 重查谓词。
- `pthread_cond_wait()` 原子地释放 mutex 并睡眠，唤醒后重新持锁再返回。
- 信号量保存资源计数；`sem_post()` 的许可不会因暂时没有等待者而丢失。
- graceful close 需要共享的 `closed` 状态和 `broadcast`，使所有等待者都能醒来退出。

## 工具验证

```bash
cd /mnt/d/claude/miniOS/step1/week4
make clean
make SANITIZE=thread

./02_counter_race unsafe
./02_counter_race safe
./03_bounded_queue
```

完成标准：能够指出共享不变量由哪把锁保护，解释 `while` 等待谓词和伪唤醒，并区分 mutex、原子变量、条件变量与信号量的职责。
