# 阶段 1 · 第 4 周：pthread、原子操作与同步

本周目标：理解线程共享地址空间带来的数据竞争，掌握 `pthread_create`/`pthread_join`、互斥锁和条件变量，并读懂一个有界生产者—消费者队列。

## 文件与学习顺序

| 文件 | 主题 | 运行方式 |
|---|---|---|
| `01_thread_lifecycle.c` | 创建线程、传参、`join` 与结果所有权 | `./01_thread_lifecycle` |
| `02_counter_race.c` | 数据竞争与 `pthread_mutex_t` | `./02_counter_race safe` / `unsafe` |
| `03_bounded_queue.c` | 有界队列、条件变量、生产者—消费者 | `./03_bounded_queue` |

## 构建与运行

```bash
cd /mnt/d/claude/miniOS/step1/week4
make

./01_thread_lifecycle
./02_counter_race safe
./02_counter_race unsafe
./03_bounded_queue
```

`safe` 模式必须得到预期计数；`unsafe` 存在 C 数据竞争，输出可能偶然正确，也可能偏小。一次“看起来正确”的运行不能证明代码正确。

若本机 ThreadSanitizer 可用，可额外尝试：

```bash
make clean
make SANITIZE=thread
./02_counter_race unsafe
```

ThreadSanitizer 的运行环境兼容性因发行版而异；即使不可用，也可通过代码推理判断 `unsafe` 模式存在数据竞争。

## 核心规则

### 线程生命周期

```text
pthread_create → 新线程开始执行
pthread_join   → 等待该线程结束，并接收其返回值
```

传给线程的参数必须在线程完成前保持有效；线程返回的堆对象由 `pthread_join()` 的调用者取得并释放。

### 数据竞争

若两个线程并发访问同一对象，至少一个是写入，且没有同步，便是数据竞争。在 C 中数据竞争是未定义行为；`volatile` 不能修复它。

### mutex

互斥锁保护共享状态及其不变量：读取—修改—写入必须作为同一临界区完成。

```c
pthread_mutex_lock(&mutex);
/* 读取、检查、修改共享状态 */
pthread_mutex_unlock(&mutex);
```

### condition variable

条件变量不保存条件本身；队列的 `count` 才是条件。必须在持锁状态下用 `while` 检查：

```c
while (queue->count == QUEUE_CAPACITY) {
    pthread_cond_wait(&queue->not_full, &queue->mutex);
}
```

`pthread_cond_wait()` 会原子地释放 mutex、睡眠；被唤醒后重新持有 mutex 才返回。使用 `while` 是为了应对伪唤醒，以及被其他线程先一步改变条件的情况。

## 阅读问题

1. 为什么 `counter.value++` 不是原子操作？
2. 为什么 `pthread_join()` 不只是“等线程结束”，还与结果生命周期有关？
3. 为什么队列的 `count`、`head`、`tail` 必须由同一把锁保护？
4. 为什么等待 `not_empty`/`not_full` 时必须写 `while`，不能只写 `if`？
5. 生产者在入队后应通知哪个条件变量，消费者在出队后又应通知哪个？

完成标准：能解释数据竞争、mutex 临界区、条件变量的释放/重新获取锁语义，并可画出一次生产者等待、消费者消费、生产者被唤醒的顺序。
