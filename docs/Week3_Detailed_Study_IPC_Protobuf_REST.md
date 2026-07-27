# 第 3 周：跨进程通信 + Protobuf & RESTful API — 详细学习指南

> **总目标**：补充 JD 要求的 Protobuf 和 RESTful API 技能，深化跨进程通信知识体系，串联实践经验。
>
> **本周定位**：你的 IPC 实践经验非常丰富（Socket/共享内存/跨语言通信），但需要系统化为面试可表达的知识框架；Protobuf 和 RESTful API 是 JD 明确要求的技能点，需从零快速掌握核心。
>
> **学习方式**：每天上午 IPC/网络理论（2h）+ 下午实践编码（1.5h），周末综合串联。

---

## Day 1：跨进程通信机制全景

> **核心目标**：系统化整理所有 IPC 机制的原理与选型，为后续深挖共享内存和 Socket 打好理论基础。

### IPC 体系对比

- [ ] **管道（匿名管道 / 命名管道）**
  - **匿名管道**（`pipe()`）：半双工，父子进程间通信，通过 `fork()` 继承 fd。本质是内核缓冲区（4KB~64KB）
  - **命名管道**（`mkfifo` / `FIFO`）：不相关的进程通过文件系统路径通信，半双工
  - **限制**：字节流无边界，数据量小（内核缓冲有限），无随机访问
  - **面试话术**："管道是最古老的 IPC 形式，本质是内核中的一个循环缓冲区。在单生产者-单消费者场景下，管道的延迟约 3-5μs（Pipe 操作本身开销小），但容量受限，不适合大数据量传输。"

- [ ] **消息队列（POSIX / System V）**
  - **System V 消息队列**（`msgget` / `msgsnd` / `msgrcv`）：消息按类型读取，有 POSIX 标准兼容性问题
  - **POSIX 消息队列**（`mq_open` / `mq_send` / `mq_receive`）：更现代的 API，支持通知机制（`mq_notify` + 信号/线程）
  - **对比**：消息队列的优势是消息有边界、可带类型、支持优先级；劣势是消息大小有上限（`/proc/sys/fs/mqueue/msgsize_max`），性能低于共享内存
  - **面试话术**："消息队列适合'发一条收一条'的解耦场景，不适合批量数据流——你发的每条消息都需要一次系统调用。在我设计的项目中，控制消息（如配置更新、状态查询）走消息队列，而高频业务数据走共享内存。"

- [ ] **共享内存（`shm_open` / `mmap`）** ⭐ 你的强项
  - **System V 共享内存**（`shmget` / `shmat` / `shmdt`）
  - **POSIX 共享内存**（`shm_open` + `mmap`）：更现代，基于 fd，可配合 `ftruncate` 调整大小
  - **匿名 `mmap`**（`MAP_ANONYMOUS | MAP_SHARED`）：不需要文件，`fork()` 后父子进程共享
  - **文件 `mmap`**：映射文件到内存，多个进程映射同一个文件可实现共享
  - **同步问题**：共享内存本身不提供同步机制——需要配合信号量（`sem_t` / POSIX 信号量）或原子操作 + 内存屏障
  - **NUMA 感知**：共享内存可能分配在某个 NUMA 节点上，远端访问延迟会增加——通过 `mbind()` 或 `numactl` 控制

- [ ] **Socket（Unix Domain / TCP/UDP）** ⭐ 你的强项
  - **Unix Domain Socket**（`AF_UNIX` / `AF_LOCAL`）：同一主机通信，不经过网络协议栈，延迟远低于 TCP
    - 流式（`SOCK_STREAM`）：类似 TCP，可靠字节流
    - 数据报（`SOCK_DGRAM`）：有边界，可靠，不会丢包（内核内部传递，不经过网络层）
    - **性能**：Unix Domain Socket 延迟约 10-20μs（对比 TCP loopback 约 30-50μs），吞吐接近内存带宽
  - **TCP Socket**：跨主机通信的标准方案，有内核协议栈开销（TCP 校验和、拥塞控制等）
  - **UDP Socket**：不可靠但低延迟——适合实时音视频、游戏状态同步等容忍丢包的场景

- [ ] **信号（Signal）—— 异步通知机制**
  - 不可靠信号（1-31） vs 可靠实时信号（34-64，支持排队）
  - `SIGKILL` / `SIGSTOP` 不可被捕获或忽略
  - 信号处理的限制：信号处理函数必须是可重入的，不能调用非异步信号安全的函数（如 `printf` / `malloc`）
  - **面试话术**："信号适合做简单的异步通知（如进程退出、定时器超时），不适合复杂通信——信号处理函数的上下文极其受限，且不同操作系统信号语义有微妙差异。在生产项目中，我宁愿用事件循环 + 消息队列来替代信号。"

- [ ] **D-Bus（Linux 桌面总线）**
  - 基于 Socket 的消息总线系统，支持进程间的"方法调用"模式
  - 系统总线 vs 会话总线：systemd 与 systemd-journald 的通信使用 D-Bus
  - **了解即可**：面试不太可能深究，但知道 D-Bus 是什么 + 它的定位（高级 IPC 框架，不是高性能方案）

### IPC 性能对比速览

| 方式 | P50 延迟 | 单次拷贝次数 | 适用数据量 | 跨语言 |
|------|---------|------------|-----------|--------|
| 共享内存（无锁队列）| ~100ns | 1（用户态） | 任意 | 需序列化 |
| Unix Socket（DGRAM）| ~10μs | 2（内核参与） | < MTU 最佳 | 天然 |
| TCP Loopback | ~30μs | 2（完整协议栈） | 大块数据 | 天然 |
| 匿名管道 | ~5μs | 2 | < 64KB | 天然 |
| POSIX 消息队列 | ~5μs | 2 | < 8KB/条 | 需序列化 |

### 跨平台 IPC 方案选型

- [ ] **Windows 下的 IPC**
  - Named Pipe：类似 Unix Domain Socket，支持流式 + 消息模式，支持异步 I/O（`Overlapped I/O`）
  - `WM_COPYDATA`：通过窗口消息传递数据（简单但有 UI 依赖，仅适用于同用户会话）
  - COM（Component Object Model）：跨进程调用（进程外 COM Server），复杂度高
  - 共享内存：`CreateFileMapping` + `MapViewOfFile`，与 POSIX `mmap` 思想一致
  - **MailSlot**：单向广播通信（类似 UDP），已基本被 Named Pipe 替代

- [ ] **Linux/Unix 下的 IPC 最佳实践** ⭐
  - **控制面**：Unix Domain Socket（可靠字节流，适合请求-响应模式）
  - **数据面**：共享内存 + 无锁环形缓冲区（适合大吞吐数据流）
  - **同步**：POSIX 信号量（`sem_t`）或 futex 实现快速等待/唤醒
  - **通知**：`eventfd`（内核事件通知 fd，可通过 epoll 监听）

- [ ] **跨平台抽象层的设计方法** ⭐
  - 用适配器模式封装：`#ifdef _WIN32` vs `#ifdef __linux__` 在底层隔离
  - 跨平台 Socket API：winsock 需要 `WSAStartup` / `WSACleanup`，Linux 不需要
  - **你的实践经验**：你在跨语言通信中间件中已经做过类似的跨平台封装

### 经验串联 💡

**你在恒生的通信中间件实践：**
- Socket 通信层：连接管理 / 心跳保活 / 断线重连 / 流量控制（你已实现）
- 共享内存层：环形缓冲区 / 信号量同步 / 数据版本控制（你已实现）
- **面试话术**："在多进程/跨语言场景中，我常用的 IPC 组合是——控制面走 Unix Domain Socket（可靠、有序、便于 epoll 统一管理事件），数据面走共享内存 + 无锁环形缓冲区（零拷贝、纳秒级延迟）。两者通过 eventfd 联动——Socket 收到控制消息后通过 eventfd 通知数据处理线程开始消费共享内存中的数据。"

### 📖 推荐资料
- 《Linux/UNIX 系统编程手册》第 44-54 章 —— 全套 IPC 机制详解
- 《Windows 核心编程》第 3 部分 —— Windows IPC（可快速浏览）
- **man page**：`man 7 shm_overview` / `man 7 sem_overview` / `man 7 unix` / `man 7 fifo`

### 🛠 推荐练习
- 写一对多进程通信 demo：一个进程通过共享内存广播状态，多个子进程通过信号量等待并读取
- 对比测试：Unix Domain Socket vs TCP Loopback vs 共享内存的延迟/吞吐（用 `chrono` 高精度计时）
- 用 `ipcs -a` / `ipcmk` / `ipcrm` 管理系统中现有的 IPC 资源

---

## Day 2：Protobuf 深入学习（上）— 编码原理

> **核心目标**：从 `.proto` 语法到 Wire Format 编码，彻底掌握 Protobuf。**本周最高优先级内容。**

### Protobuf 基础

- [ ] **`.proto` 文件语法速览**
  ```protobuf
  syntax = "proto3";
  
  message Order {
    int64  id         = 1;
    string symbol     = 2;
    double price      = 3;
    int32  quantity   = 4;
    Side   side       = 5;   // 枚举
    map<string, string> ext = 6;  // proto3 可选
    oneof extra {
      LimitOrder  limit  = 7;
      MarketOrder market = 8;
    }
  }
  
  enum Side {
    SIDE_UNSPECIFIED = 0;
    SIDE_BUY  = 1;
    SIDE_SELL = 2;
  }
  
  service OrderService {
    rpc PlaceOrder(Order) returns (OrderResponse);
  }
  ```
  - **字段编号 1-15**：占用 1 byte tag（field_number + wire_type），**高频字段使用小编号**可提升编码效率
  - **字段编号 16-2047**：占用 2 bytes tag
  - `reserved` 关键字：防止字段编号/名称被重用（前向/后向兼容）
  - package 命名空间防止冲突
  - import 引入其他 `.proto` 文件中的定义

- [ ] **proto2 vs proto3 核心差异**
  | 特性 | proto2 | proto3 |
  |------|--------|--------|
  | 字段修饰符 | `required` / `optional` / `repeated` | 默认 `optional`（无 `required`）|
  | 默认值 | 可自定义 | 完全由类型决定（0 / false / ""）|
  | 枚举 | 第一个值可以为非 0 | **第一个值必须为 0**（语义为 UNSPECIFIED）|
  | 未知字段 | 保留在序列化结果中 | Wire 格式时丢弃（但 `protoc` 有 `--experimental_allow_proto3_optional`）|
  | 预设的 JSON 映射 | 无 | 有官方 JSON 映射规则 |

- [ ] **Varint 编码原理** ⭐ 高频考点
  - **核心思想**：小整数用更少字节表示。每字节最高位（MSB）是 continuation flag（1=还有后续字节，0=结束），低 7 位存数据
  - **编码举例**：
    - `uint32 300` 编码过程：二进制 `0000 0001 0010 1100`
    - 按 7 位分组（小端）：`010 1100` | `000 0010`（实际低位在前）
    - 加 continuation bit：`1010 1100` (0xAC) | `0000 0010` (0x02)
    - 所以 300 编码为 `0xAC 0x02`
  - **Varint 的局限**：负数（int32/sint32）——负数在大多数语言中用二进制补码表示，int32(-1) 在 Varint 中会编码为 10 字节（补码全 1）！
  - **ZigZag 编码（`sint32` / `sint64`）**：
    - `sint32`：`(n << 1) ^ (n >> 31)` — 绝对值小的负数编码后变正数小整数
    - `sint64`：`(n << 1) ^ (n >> 63)`
    - -1 → 1，1 → 2，-2 → 3，2 → 4 ... 完美解决负数编码膨胀问题
    - **面试话术**："如果消息中可能有负数（如价格差额、时间差），一定要用 `sint32/sint64` 而非 `int32/int64`——int32 负数会用 10 字节编码，sint32 只需 1-2 字节。"

- [ ] **Wire Format 详解**
  | Wire Type | 含义 | 编码方式 | 支持类型 |
  |-----------|------|---------|---------|
  | 0 | Varint | Varint | int32/int64/uint32/uint64/sint32/sint64/bool/enum |
  | 1 | 64-bit | 固定 8 字节 | fixed64/sfixed64/double |
  | 2 | Length-delimited | Varint(length) + data | string/bytes/embedded message/packed repeated |
  | 3 | Start group | 已废弃（proto2 中也很少用）| — |
  | 4 | End group | 已废弃 | — |
  | 5 | 32-bit | 固定 4 字节 | fixed32/sfixed32/float |

- [ ] **`repeated` 字段的 packed 编码优化**
  - proto3 中 `repeated` 默认使用 packed 编码（Wire Type 2，将所有元素打包为一个 length-delimited 块）
  - 相比 proto2 非 packed（每个元素一个独立的 tag-value 对），packed 对大量小元素场景节省约 50% 空间
  - `repeated int32` 非 packed：`tag + value + tag + value + ...` → packed：`tag + length + value + value + ...`
  - **性能注意**：解析 packed repeated 时需要先分配内存存放所有元素，对大数组场景（如 10 万条 float）是好事（一次性分配），但对流式处理可能增加延迟

- [ ] **前向兼容与后向兼容（字段编号规则）**
  - **不可变规则**：已分配字段编号**永远不能修改**、新旧字段不可共用同一编号
  - 添加新字段：旧代码解析时**忽略未知字段**（proto3 行为），新代码读到默认值
  - 删除字段：用 `reserved` 标记已删除的字段编号和名称，防止未来误用
  - 修改字段类型：仅当 Wire Type 兼容时可行（如 int32/uint32/sint32/bool 之间、fixed32/sfixed32 之间）
  - **类型兼容对照表**：
    | 原类型 | 兼容类型 |
    |--------|---------|
    | int32 | int64, uint32, uint64, sint32, sint64, bool |
    | fixed32 | sfixed32, float |
    | fixed64 | sfixed64, double |
    | string | bytes |

### 动手实践

- [ ] **实践 1**：定义包含嵌套 message、enum、map、oneof、repeated 的 `.proto` 文件
- [ ] **实践 2**：用 `protoc` 编译生成 C++ 代码（`--cpp_out=.`），阅读生成的 `.pb.h` / `.pb.cc`
- [ ] **实践 3**：写序列化/反序列化 demo：
  ```cpp
  Order order;
  order.set_id(12345);
  order.set_symbol("AAPL");
  order.set_price(150.50);
  order.set_quantity(100);
  order.set_side(Side::SIDE_BUY);
  
  std::string data;
  order.SerializeToString(&data);   // 序列化
  std::cout << "Serialized size: " << data.size() << " bytes\n";
  
  Order parsed;
  parsed.ParseFromString(data);      // 反序列化
  std::cout << "Parsed symbol: " << parsed.symbol() << "\n";
  ```
- [ ] **实践 4**：用十六进制查看序列化结果，手动解析 Wire Format（验证你对 Varint / Tag 的理解）

### 📖 推荐资料
- **Protobuf 官方文档**：`https://protobuf.dev/programming-guides/proto3/` — Language Guide
- **Protobuf 编码**：`https://protobuf.dev/programming-guides/encoding/` — **必读**
- protobuf C++ 生成的代码阅读指南

---

## Day 3：gRPC 与序列化方案对比

> **核心目标**：掌握 gRPC 四种通信模式，能动手搭建 C++ demo，理解序列化方案选型。

### gRPC 核心概念

- [ ] **gRPC 基本架构**
  - 基于 HTTP/2（多路复用、流控、头部压缩、Server Push）
  - 默认序列化：Protobuf（也可替换为 FlatBuffers 等）
  - 通信流程：客户端 Stub（代理）→ 序列化 → HTTP/2 Stream → 服务端 → 反序列化 → 业务处理

- [ ] **gRPC 四种通信模式** ⭐ 面试必考
  - **Unary RPC**：客户端发一个请求，服务端回一个响应。最像传统 RPC
    - 场景：查询账户余额、提交订单
    - 接口定义：`rpc GetBalance(GetBalanceRequest) returns (GetBalanceResponse);`
  - **Server Streaming RPC**：客户端发一个请求，服务端返回一个流（多次消息）
    - 场景：订阅行情快照、批量下载数据
    - 接口定义：`rpc SubscribeQuotes(SubscribeRequest) returns (stream Quote);`
  - **Client Streaming RPC**：客户端发送流式请求，服务端返回单个响应
    - 场景：批量上传、日志聚合后返回汇总
    - 接口定义：`rpc UploadLogs(stream LogEntry) returns (UploadSummary);`
  - **Bidirectional Streaming RPC**：双方独立发送流，全双工
    - 场景：聊天、实时交易指令双向推送
    - 接口定义：`rpc Chat(stream ChatMessage) returns (stream ChatMessage);`
    - **面试话术**："在交易系统中，双向流非常适合做实时指令下发 + 执行确认——客户端主动订阅后，服务端可以随时推送指令，客户端也能即时反馈执行结果，避免了 Unary 模式的轮询开销。"

- [ ] **gRPC 的 C++ 编程模型**
  - **同步 vs 异步**：
    - 同步 Stub：`stub->PlaceOrder(context, request, &response)` —— 阻塞直到响应或超时
    - 异步 Stub（`CompletionQueue`）：基于回调的异步模型，适合高吞吐场景
    - C++ 异步 API 较底层（需要手动管理 `CompletionQueue` 和 `void* tag`）
  - **Server 端**：`AddService` → `BuildAndStart` → 循环处理 `CompletionQueue` 事件
  - **Client 端**：`CreateChannel("localhost:50051", grpc::InsecureChannelCredentials())` → 创建 Stub

- [ ] **gRPC 与 Protobuf 的关系**
  - gRPC service 定义在 `.proto` 文件中
  - `protoc` + `grpc_cpp_plugin` 同时生成 pb 消息代码 + gRPC stub 代码
  - Protobuf 负责序列化，gRPC 负责传输层

### 动手搭建 gRPC C++ Demo ⚠️ 必做

- [ ] **Step 1**：安装 gRPC（建议用 v1.60+，支持 C++14+）
  - 从源码编译：`git clone --recurse-submodules https://github.com/grpc/grpc`
  - 或者用 vcpkg：`vcpkg install grpc`
  - **注意**：Windows 上编译 gRPC 需要较长时间（> 30 分钟），建议提前开始

- [ ] **Step 2**：定义 `.proto` 文件（service + messages）

- [ ] **Step 3**：编写 Server（处理请求 + 响应）
  ```cpp
  class OrderServiceImpl final : public OrderService::Service {
    grpc::Status PlaceOrder(grpc::ServerContext* context,
                            const Order* request,
                            OrderResponse* reply) override {
      // 业务逻辑
      reply->set_status("FILLED");
      reply->set_fill_price(request->price());
      return grpc::Status::OK;
    }
  };
  ```

- [ ] **Step 4**：编写 Client（发送请求 + 接收响应）

- [ ] **Step 5**：交叉编译 + 运行测试

### 其他序列化方案对比

- [ ] **FlatBuffers / Cap'n Proto —— 零拷贝序列化** ⭐ 结合你的零拷贝经验
  - **FlatBuffers**（Google 出品）：
    - 序列化后的数据就是内存中的结构体布局——**无需解析即可直接访问**
    - 关键 API：`flatbuffers::FlatBufferBuilder` 构建 → 直接写入 Socket/文件 → 接收端直接内存映射后访问字段
    - 适用场景：游戏数据 / 高频交易（避免反序列化的 CPU 开销）/ 移动端大数据包
    - 与 Protobuf 对比：FlatBuffers 省掉了 parse 步骤，但 Builder 构建过程比 Protobuf 的 `set_*` 略慢
  - **Cap'n Proto**（Kenton Varda 设计，Protobuf 2 作者）：
    - 设计思想与 FlatBuffers 类似，但更激进——连 Builder 阶段都接近零拷贝
    - 通过 **capability** 系统实现 RPC（基于 Promise 的异步调用）
  - **面试话术**："在我构建的零拷贝数据通路中，序列化是最后一个瓶颈。Protobuf 的反序列化需要为每个字段做内存分配和复制——对于 GB/s 级的数据流，这个开销不可忽略。FlatBuffers 的'访问即解析'模式让我们在金融行情数据的处理中将反序列化时间从微秒级降到纳秒级（代价是序列化后数据体积略大约 10-20%）。"

- [ ] **MessagePack / JSON / XML 适用场景**
  | 方案 | 编码尺寸 | 编码/解码速度 | 可读性 | Schema | 跨语言 |
  |------|---------|-------------|-------|--------|--------|
  | Protobuf | 最小 | 极快 | 不可读 | 需 .proto | ✅ |
  | FlatBuffers | 较小 | 最快（零解析）| 不可读 | 需 .fbs | ✅ |
  | MessagePack | 中等 | 快 | 二进制 | 无 | ✅ |
  | JSON | 最大 | 慢 | 可读 | 无（可配合 JSON Schema）| 所有语言 |
  | XML | 最大 | 最慢 | 可读 | XSD | 所有语言 |

- [ ] **选型考量因素** ⭐ 面试决策题
  - **性能优先**（高频交易/实时系统）：FlatBuffers 或 Cap'n Proto
  - **通用性 + 生态**（微服务/Web）：Protobuf + gRPC
  - **调试友好**（开发阶段/低并发）：JSON（可读性强，方便 curl 测试）
  - **数据体积**：Protobuf ≈ MessagePack < JSON < XML（通常）
  - **Schema 演进**：Protobuf / FlatBuffers 支持最好的版本兼容
  - **语言支持**：Protobuf 覆盖语言最多，FlatBuffers 主要语言支持良好

### 📖 推荐资料
- gRPC C++ Quick Start：`https://grpc.io/docs/languages/cpp/quickstart/`
- gRPC 官方文档：`https://grpc.io/docs/`
- FlatBuffers 白皮书：`https://google.github.io/flatbuffers/`
- Cap'n Proto 介绍：`https://capnproto.org/`

### 🛠 推荐练习
- 完成一套完整的 gRPC C++ demo（Unary + Server Streaming）
- 对比测试：Protobuf 序列化 vs FlatBuffers 序列化的序列化/反序列化速度 + 数据大小
- 思考：如何将你的交易系统中的自定义二进制协议迁移到 Protobuf？利弊如何？

---

## Day 4：RESTful API 与服务化

> **核心目标**：快速掌握 RESTful API 设计规范 + C++ HTTP 服务编程。

### RESTful API 设计基础

- [ ] **HTTP 方法与语义**
  | 方法 | 语义 | 幂等 | 安全（不修改资源）| 典型用途 |
  |------|------|------|-----------------|---------|
  | GET | 读取资源 | ✅ | ✅ | 查询数据 |
  | POST | 创建资源 / 触发动作 | ❌ | ❌ | 提交订单 |
  | PUT | 全量更新 / 替换资源 | ✅ | ❌ | 更新全部字段 |
  | PATCH | 部分更新资源 | 不一定 | ❌ | 更新某个字段 |
  | DELETE | 删除资源 | ✅ | ❌ | 删除订单 |

  - **幂等（Idempotent）**：多次执行与一次执行结果相同。POST 非幂等——"重复提交"需要额外去重
  - **安全方法（Safe）**：不改变服务端状态。POST/PUT/DELETE/PATCH 都不安全

- [ ] **状态码规范使用** ⭐ 面试必知
  - **`200 OK`**：GET/PUT 成功
  - **`201 Created`**：POST 成功创建资源
  - **`204 No Content`**：DELETE 成功（无返回体）
  - **`400 Bad Request`**：客户端请求错误（参数错误 / 格式错误）
  - **`401 Unauthorized`**：未认证
  - **`403 Forbidden`**：已认证但无权限
  - **`404 Not Found`**：资源不存在
  - **`409 Conflict`**：资源冲突（如重复创建 / 版本冲突）
  - **`422 Unprocessable Entity`**：请求体语义错误（如字段验证失败）
  - **`429 Too Many Requests`**：限流
  - **`500 Internal Server Error`**：服务端内部错误
  - **`503 Service Unavailable`**：服务不可用（过载 / 维护）
  - **面试话术**："我要求团队遵循'精确的状态码'——400 vs 422 要区分清楚（400 是语法错误，422 是语义错误），成功的 POST 必须返回 201 而非 200。这种精确性让调用方（尤其是跨团队调用）能正确处理每种响应。"

- [ ] **API 版本控制策略**
  - **URL Path 版本化**：`/api/v1/orders` vs `/api/v2/orders` —— 最直观，最常用
  - **Header 版本化**：`Accept: application/vnd.company.v1+json` —— 更 RESTful，但调试不方便
  - **Query 参数**：`/api/orders?version=1` —— 简单但违反 REST 原则
  - **最佳实践**：URL Path 版本化 + 定期废弃旧版本 + 不同版本可共存

- [ ] **认证与授权：JWT / OAuth2 基础**
  - **JWT（JSON Web Token）**：`header.payload.signature` 三段式 Token，无状态认证
    - Header：`alg: HS256` / `typ: JWT`
    - Payload：用户信息 + 过期时间（`exp`）+ 自定义 claim
    - Signature：防止篡改
    - C++ 中解析：`jwt-cpp` 或 `cpp-jwt` 库
  - **OAuth2**：授权框架（授权码模式 / 客户端模式 / 密码模式 / 隐式模式）
    - "授权码模式"是后端场景的标准——用户授权 → 服务端用授权码换 Token → 访问资源
    - C++ 中基本只需要作为客户端使用（`libcurl` 请求 Token 端点）

### C++ 中的 HTTP 实现

- [ ] **常用 C++ HTTP 库对比**
  | 库 | 类型 | HTTP 版本 | 特点 |
  |----|------|----------|------|
  | `libcurl` | 客户端 | HTTP/1.1, HTTP/2 | 最成熟，功能最全，但 C API 较底层 |
  | `cpp-httplib` | 客户端+服务端 | HTTP/1.1 | **header-only**，极其轻量，适合 demo 和中小服务 |
  | `Boost.Beast` | 客户端+服务端 | HTTP/1.1, HTTP/2, WebSocket | Boost 官方，性能高，但学习曲线陡 |
  | `Drogon` | 服务端框架 | HTTP/1.1, HTTP/2, WebSocket | 全功能 Web 框架（路由/ORM/模板），Github 5k+ star |
  | `Pistache` | 服务端框架 | HTTP/1.1 | 轻量 RPC 风格，适合内部微服务 |
  | `Oat++` | 服务端框架 | HTTP/1.1, WebSocket | 高性能，支持 Swagger 自动生成 |

- [ ] **动手实践：用 cpp-httplib 搭建 RESTful 服务**
  ```cpp
  #include <httplib.h>
  
  int main() {
      httplib::Server svr;
      
      // GET /api/v1/orders
      svr.Get("/api/v1/orders", [](const httplib::Request& req, httplib::Response& res) {
          res.set_header("Content-Type", "application/json");
          res.status = 200;
          res.set_content(R"({"orders":[]})", "application/json");
      });
      
      // POST /api/v1/orders
      svr.Post("/api/v1/orders", [](const httplib::Request& req, httplib::Response& res) {
          auto order = req.body;  // 解析 JSON/Protobuf
          // 处理订单...
          res.status = 201;
          res.set_content(R"({"id":"abc123","status":"created"})", "application/json");
      });
      
      svr.listen("0.0.0.0", 8080);
  }
  ```
  - 配合 Protobuf：请求体接收 Protobuf 二进制数据后用 `ParseFromString` 解析
  - 配合 JSON：请求体接收 JSON 后用 `nlohmann/json` 解析

### 📖 推荐资料
- RESTful API 设计规范（Microsoft 版）：`https://docs.microsoft.com/en-us/azure/architecture/best-practices/api-design`
- JSON Web Token 官方文档：`https://jwt.io/introduction/`
- **cpp-httplib**：`https://github.com/yhirose/cpp-httplib`（header-only，加一个 .h 即可用）
- **nlohmann/json**：`https://github.com/nlohmann/json`（现代 C++ JSON 库）

### 🛠 推荐练习
- 用 cpp-httplib 写一个 RESTful 订单服务 CRUD（GET / POST / PUT / DELETE）
- 用 curl 测试：`curl -X GET http://localhost:8080/api/v1/orders`
- 将 Protobuf 作为请求体格式，用 cpp-httplib + Protobuf 完成序列化传输

---

## Day 5：跨进程通信方案深度 + 实战串联 ⭐ 核心亮点日

> **核心目标**：将本周所有 IPC / Protobuf / gRPC / RESTful 知识串联起来，设计完整方案，准备面试话术。

### 共享内存通信深度

- [ ] **环形缓冲区设计（结合你的实践经验）** ⭐ 深挖
  - **基础模型**：固定大小数组, write_idx / read_idx 模运算环绕
  - **SPSC 优化**：
    - 两个指针各占一个独立的 Cache Line（`alignas(64)` 防止 False Sharing）
    - 写者只更新 `write_idx`，读者只更新 `read_idx` —— 无需原子操作，`store(relaxed)` + `load(acquire)` 即可
    - 判空：`read_idx == write_idx`；判满：`(write_idx + 1) % size == read_idx`（空一格检测）
  - **MPSC / MPMC 扩展**：
    - 多生产者：用 CAS 原子操作竞争 Slot 所有权（`sequence` 数组标记每个 slot 的状态）
    - 多消费者：CAS 竞争消费权，或按序消费（每个消费者负责连续的段）
  - **内存映射**：
    - 共享内存 + `mmap` 将环形缓冲区映射到多个进程的地址空间
    - 需要跨进程信号量 `sem_t` 做生产者消费者同步（或基于 futex 的快速同步）

- [ ] **无锁队列在共享内存中的应用** ⭐ 结合你的项目
  - 关键挑战：共享内存中的指针是绝对地址，跨进程映射后地址不同——**指针必须用偏移量替代**
  - 解决方案：基址 + offset 替代裸指针，每个进程计算自己的实际地址
  - **面试话术**："将无锁队列放在共享内存中时，最大的坑是指针失效——共享内存在不同进程中映射的基地址不同，不能直接用指针引用队列元素。我们的做法是用 `uint32` 偏移量替代指针，每个进程读取时用 `base_addr + offset` 计算实际位置。另外 CAS 操作也要注意：`std::atomic` 不能直接用于共享内存（它的锁机制是 per-process 的），需要用 C++20 的 `std::atomic_ref` 或直接用 `__atomic` builtins。"

- [ ] **跨语言共享内存的序列化策略**
  - **方案一**：统一预定义布局——C++ struct 和 Java ByteBuffer 映射到相同内存布局
  - **方案二**：环形缓冲区中传递 FlatBuffers 数据（零解析直接访问）
  - **方案三**：Protobuf 序列化写入共享内存（需要一次反序列化，但兼容性最好）
  - **你的经验回顾**：你之前的跨语言通信中间件用了哪种策略？为什么？坑在哪里？

### 实战串联：设计完整通信方案 ⚠️ 面试展示材料

- [ ] **场景设计题目**：设计一个 C++ 核心交易服务 + 外部 C#/Java 客户端的通信方案
  - **需求**：
    - C++ 核心引擎：低延迟（P99 < 100μs），高吞吐（5 万笔/秒）
    - C#/Java 客户端：接收行情、发送订单、接收成交回报
    - 跨语言、跨机器（C++ 在交易服务器，客户端在应用服务器）
  - **你的分层设计方案**：
    ```
    ┌─────────────────────────────────────────┐
    │  C#/Java 客户端                           │
    │  ┌───────────────────────────────────┐   │
    │  │  gRPC Client Stub (Unary + Stream) │   │
    │  └──────────┬────────────────────────┘   │
    │             │ gRPC (HTTP/2, Protobuf)      │
    └─────────────┼────────────────────────────┘
                  │  跨网络
    ┌─────────────┼────────────────────────────┐
    │  C++ 交易引擎 (Server)                     │
    │  ┌──────────▼────────────────────────┐   │
    │  │  gRPC Server (外部线程池)            │   │
    │  │  ↕ Protobuf ↔ 内部数据结构          │   │
    │  ├───────────────────────────────────┤   │
    │  │  共享内存 + 无锁环缓冲区 (内部 IPC)  │   │
    │  │  ↑ eventfd 通知 ∣ ↓ 零拷贝传递     │   │
    │  ├───────────────────────────────────┤   │
    │  │  交易处理核心 (DAG 算子调度)         │   │
    │  └───────────────────────────────────┘   │
    └─────────────────────────────────────────┘
    ```
  - **数据流时序**（客户端的订单如何到达核心引擎）：
    1. C# 客户端：`stub->PlaceOrder(order_proto)` → gRPC over TCP
    2. C++ gRPC Server 收到 → 解析 Protobuf → 转化为内部数据结构
    3. 写入共享内存无锁环形缓冲区（零拷贝）
    4. 通过 `eventfd` 通知交易处理线程
    5. 交易处理：查账 → 风控检查 → 撮合 → 写回结果
    6. 结果通过 gRPC Stream 推送给客户端
  - **关键技术决策（Trade-off）** ⭐ 必考
    - **为什么外部用 gRPC（而非共享内存）？**——跨网络需求不可忽略；客户端需要统一的跨语言方案；Protobuf 兼容性远好于自定义协议
    - **为什么内部用共享内存而非 gRPC？**——延迟敏感（gRPC 基于 HTTP/2 延迟 > 1ms，共享内存 ~100ns）；内部不需要跨语言（C++ ⇄ C++）
    - **为什么内部不用 Protobuf 传共享内存数据？**——频繁 Proto 解析的 CPU 开销不可忽略；内部数据结构无需兼容外部协议
    - **gRPC 的 Protobuf 和内部数据结构如何转换？**——直接匹配字段，减少拷贝次数

- [ ] **架构图绘制准备**
  - 在白板上画出分层架构图 + 数据流时序图
  - 准备 3 分钟的"方案介绍"话术（从场景到决策到细节）

### 经验串联 💡

**结合你在恒生的跨语言通信中间件实践：**
- 你设计的 C++ ↔ Java 通信方案与上述架构有哪些相似和不同？
- 你当时的"自定义二进制协议"——现在用 Protobuf 会更好吗？（坦诚对比：Protobuf 生态好但编码解码额外开销）
- **面试话术**："在恒生电子，我设计的跨语言通信中间件采用 Socket + 自定义二进制协议（C++ ↔ Java）。如果今天重新设计，我会优先考虑 Protobuf + gRPC——因为 Protobuf 在前向兼容性、多语言生态、调试工具链上远超自定义协议，且 gRPC Stream 模式天然支持我们的推送场景。不过，对于极高频的数据通道（如行情分发，每秒百万级更新），我仍会保留共享内存 + FlatBuffers 的低延迟路径。"

### 📖 推荐资料
- 重温你项目的通信中间件架构（设计文档 / 代码结构）
- 阅读 Facebook 的 Wangle 或 Folly 中的共享内存组件设计

### 🛠 推荐练习
- 用一张表完整对比五种 IPC 机制（共享内存/Unix Socket/TCP/消息队列/管道）的延迟/吞吐/复杂度/跨语言能力
- 画两套图：① 架构分层图 ② 数据流时序图（白板练习，控制时间）

---

## Day 6：本周复习 + 模拟面试 + Protobuf 编码原理巩固

> **核心目标**：总结本周知识体系，核心考点口述过关，完成面试模拟。

### 知识点自检清单

- [ ] **IPC 机制**
  - [ ] 能对比 5 种 IPC 机制的性能、适用场景和复杂度
  - [ ] 能解释共享内存为什么是延迟最低的方案 + 需要什么同步手段
  - [ ] 能对比 Unix Domain Socket 和 TCP Loopback 的差异
  - [ ] 能讲述跨平台 IPC 封装的要点（Windows vs Linux）
  - [ ] 能画出你项目中 IPC 方案的分层架构

- [ ] **Protobuf**
  - [ ] 能写一个完整的 `.proto` 文件（含 message/enum/oneof/map/repeated）
  - [ ] 能解释 Varint 编码原理并手算示例
  - [ ] 能解释 ZigZag 编码的公式为什么能解决负数膨胀
  - [ ] 能说出 proto2 vs proto3 的三个关键差异
  - [ ] 能解释字段兼容性规则（哪些改变是破坏性的？）
  - [ ] 能说出 packed repeated 的编码优化原理

- [ ] **gRPC**
  - [ ] 能解释四种通信模式及其适用场景
  - [ ] 能写出 gRPC Server + Client 的关键代码骨架
  - [ ] 能说出 gRPC 底层为什么选择 HTTP/2

- [ ] **RESTful API**
  - [ ] 能列出 HTTP 方法语义和对应的状态码
  - [ ] 能说出 API 版本化的常见策略及其优劣
  - [ ] 能用 cpp-httplib 写出一个简单的 RESTful 服务

- [ ] **序列化方案选型**
  - [ ] 能对比 Protobuf / FlatBuffers / JSON / XML 的核心差异
  - [ ] 能说出 FlatBuffers "零拷贝访问"的工作原理
  - [ ] 能根据不同场景推荐合适的序列化方案

### 核心知识点记忆卡片

**Varint 编码速记**：
```
数字 → 按 7 位分组（低位在前）→ 每组加上 continuation bit（最高位）
0-127（0x00-0x7F）：1 字节
128-16383（0x80-0x3FFF）：2 字节
...

ZigZag(sint32) = (n << 1) ^ (n >> 31)
ZigZag(sint64) = (n << 1) ^ (n >> 63)
```

**Protobuf Wire Format 速记**：
```
[Tag, Value]+
Tag = (field_number << 3) | wire_type
Wire Type: 0=Varint, 1=64bit, 2=Length-delimited, 5=32bit
```

**gRPC 四种模式速记**：
```
Unary:        req ───→ res   （传统 RPC）
Server Stream: req ───→ |res|res|...| （订阅推送）
Client Stream: |req|...| ───→ res   （批量上传）
Bidirectional: |req|...| ⇄ |res|...| （实时对话）
```

### 模拟问答练习

1. **"请说说常见的进程间通信方式有哪些，你的项目中用了哪些？为什么？"**
   - 答要点：列举 5-6 种 IPC → 你的项目中用了共享内存（高频数据）+ Unix Domain Socket（控制消息）+ TCP Socket（跨机器）→ 按性能/场景/可维护性说明选型理由 → 说出每种方案的延迟量级

2. **"Protobuf 的 Varint 编码是怎么工作的？负数怎么处理？"**
   - 答要点：Varint 每字节 7 位数据 + 1 位 continuation flag → 小整数编码高效 → int32 负数会编码为 10 字节（补码全 1）→ ZigZag 解决 → 手算几个例子

3. **"gRPC 的四种通信模式分别适用于什么场景？Bidirectional Streaming 你在交易系统中怎么用？"**
   - 答要点：四种模式定义 → 双向流可以做"订阅-推送"双向消息通道 → 交易系统中：客户端订阅后，服务端推行情/成交回报，客户端同时也发指令

4. **"在你的项目中如何设计跨语言通信中间件？如果重来，你会怎么改进？"**
   - 答要点：当前方案（Socket + 自定义二进制协议）→ 挑战（字节序/字符串编码/跨平台）→ 改进方向（Protobuf 替代自定义协议 + gRPC Stream 替代自建 Socket + 服务注册发现）→ 明确指出"高频通道仍保留共享内存 + FlatBuffers"

5. **"Protobuf v3 和 FlatBuffers 有什么区别？什么时候选哪个？"**
   - 答要点：Protobuf 需要 parse → 访问字段（CPU 开销 + 内存分配）→ 适合通用微服务、跨语言、动态类型；FlatBuffers 访问即解析（零拷贝）→ 适合高频、大数据量、移动端 → 举例：交易系统行情通道用 FlatBuffers，外部 gRPC 接口用 Protobuf

6. **"RESTful API 中 POST 和 PUT 有什么区别？幂等性为什么重要？"**
   - 答要点：POST 创建（非幂等，每次创建新资源）→ PUT 全量替换/更新（幂等，多次执行结果相同）→ 幂等性在网络重试场景中至关重要（如支付下单：POST 需要去重保证创建一次，PUT 可以安全重试）

7. **"共享内存做 IPC 时，怎么保证数据一致性？"**
   - 答要点：共享内存本身无同步 → 三种方案：① 信号量（`sem_t`）做互斥/同步 ② 无锁环形缓冲区 + `std::atomic` 内存序（SPSC 场景） ③ eventfd 做事件通知 → 注意跨进程 `std::atomic` 的限制（需用 `atomic_ref` 或 GCC builtins）→ NUMA 感知（共享内存分配节点）

### 综合实战串联（面试白板题练习）

**[场景题] 设计一个跨进程/跨语言的订单高速处理系统**

```
需求：
- C++ 交易引擎（Linux，低延迟）
- 外部 Python/Java 客户端发送订单、接收成交
- 内部需要与风控模块、撮合模块通信
- 跨网络（客户端与引擎在不同机器上）

请给出：
1. 整体架构图
2. 关键通信链路的选择（外部/内部各用什么IPC）
3. 数据流时序
4. 序列化方案选型
5. 3 个关键 Trade-off 决策
```

**建议**：用纸笔画出架构图 + 口头讲述（5-8 分钟）。

### 本周薄弱点记录

| 知识点 | 掌握度 (1-5) | 备注 |
|--------|-------------|------|
| IPC 机制全景 | | |
| 共享内存进阶（偏移量指针等） | | |
| Protobuf Wire Format / Varint | | |
| Protobuf 兼容性规则 | | |
| gRPC 四种模式 | | |
| RESTful API 设计 | | |
| 序列化方案选型对比 | | |

---

## 📚 本周推荐资源汇总

### 书籍
- 《Linux/UNIX 系统编程手册》第 44-54 章 —— IPC 大全
- 《嵌入式 Linux 基础教程》（如果涉及跨平台通信层设计）

### 在线文档
- **Protobuf 编码原理**（必读）：`https://protobuf.dev/programming-guides/encoding/`
- **gRPC C++ Quick Start**：`https://grpc.io/docs/languages/cpp/quickstart/`
- **FlatBuffers 介绍**：`https://google.github.io/flatbuffers/`
- **RESTful API 设计规范**：Microsoft / Google API Design Guide

### 库/工具
| 用途 | 推荐库 | 学习重点 |
|------|--------|---------|
| gRPC | gRPC C++ | 四种模式编程 |
| HTTP Server | cpp-httplib | RESTful 服务 |
| JSON | nlohmann/json | JSON 解析 |
| JWT | jwt-cpp | 认证 Token 解析 |
| IPC 同步 | POSIX semaphore / eventfd | 跨进程同步 |

### 代码练习清单
- [ ] `.proto` 文件定义 + protoc 编译
- [ ] Protobuf 序列化/反序列化 + 16 进制查看 Wire Format
- [ ] gRPC Server + Client（Unary + Server Streaming）
- [ ] cpp-httplib RESTful 服务（CRUD 四个方法）
- [ ] 共享内存 SPSC 环形缓冲区（跨进程版）
- [ ] Protobuf + FlatBuffers 序列化速度对比测试

---

> 📅 **更新日志**：2026-07-27 基于主计划第 3 周内容扩展生成
>
> 🔄 **前置依赖**：建议完成第 2 周（OS + 算法）后再进入本周
>
> 📌 **重点提醒**：本周 Protobuf 和 gRPC 部分是你简历短板，投入至少 40% 的时间；IPC 和共享内存是你的强项，重点在于系统化梳理和面试话术准备。
