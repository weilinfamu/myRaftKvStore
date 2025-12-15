# RPC数据序列化面试问题

## 核心问题：RPC把信息转化成什么数据类型？

### 简短回答
RPC将信息转化为**字节流（byte stream）**，通过TCP socket传输。具体使用**Protobuf序列化**将结构化数据转为二进制格式。

---

## 详细过程

### 1. 数据转换流程

```
用户请求对象 → Protobuf序列化 → 二进制字节流 → TCP传输 → 反序列化 → 响应对象
```

### 2. 具体实现步骤（发送端）

#### 步骤1：序列化请求参数
```cpp
// mprpcchannel.cpp:327
std::string args_str;
request->SerializeToString(&args_str);  // Protobuf对象 → 二进制字符串
uint32_t args_size = args_str.size();
```

#### 步骤2：构造RPC头部
```cpp
// mprpcchannel.cpp:334-337
RPC::RpcHeader rpcHeader;
rpcHeader.set_service_name(service_name);  // 服务名
rpcHeader.set_method_name(method_name);    // 方法名
rpcHeader.set_args_size(args_size);        // 参数大小
```

#### 步骤3：序列化头部
```cpp
// mprpcchannel.cpp:340
std::string rpc_header_str;
rpcHeader.SerializeToString(&rpc_header_str);  // 头部 → 二进制
```

#### 步骤4：组装最终数据包
```cpp
// mprpcchannel.cpp:348-357
// 数据包格式：[头部长度(变长)] + [头部内容] + [请求参数]
google::protobuf::io::CodedOutputStream coded_output(&string_output);
coded_output.WriteVarint32(rpc_header_str.size());  // 变长编码的头部长度
coded_output.WriteString(rpc_header_str);           // 头部内容
send_rpc_str += args_str;                           // 附加请求参数
```

#### 步骤5：TCP发送
```cpp
// mprpcchannel.cpp:365
send(m_clientFd, data_ptr, total_to_send, 0);  // 发送字节流
```

### 3. 数据包结构

```
+------------------+------------------+------------------+
| 头部长度(varint) | RPC头部(binary)  | 请求参数(binary) |
+------------------+------------------+------------------+
    1-5字节            N字节              M字节
```

**头部内容（Protobuf定义）：**
```protobuf
// rpcheader.proto:5-16
message RpcHeader {
    bytes service_name = 1;      // 服务名
    bytes method_name = 2;       // 方法名
    uint32 args_size = 3;        // 参数大小（变长编码）
    bool compressed = 4;         // 是否压缩
    uint32 original_size = 5;    // 原始大小
    uint32 compression_type = 6; // 压缩类型
}
```

### 4. 接收端反序列化

#### 步骤1：读取头部长度
```cpp
// mprpcchannel.cpp:395-421
uint8_t varint_buf[10];
// 逐字节读取变长编码的头部长度
coded_input.ReadVarint32(&header_size);
```

#### 步骤2：读取头部内容
```cpp
// mprpcchannel.cpp:429
recv(m_clientFd, header_buf.data(), header_size, 0);
```

#### 步骤3：反序列化头部
```cpp
// mprpcchannel.cpp:445
RPC::RpcHeader resp_header;
resp_header.ParseFromArray(header_buf.data(), header_size);
```

#### 步骤4：读取并反序列化业务数据
```cpp
// mprpcchannel.cpp:457, 472
recv(m_clientFd, response_buf.data(), response_args_size, 0);
response->ParseFromArray(response_buf.data(), response_args_size);
```

---

## 可能被问到的问题

### Q1: 为什么使用Protobuf而不是JSON？
**答：**
- **性能**：Protobuf二进制格式比JSON文本格式小30-50%
- **速度**：序列化/反序列化速度快5-10倍
- **类型安全**：强类型定义，编译时检查
- **跨语言**：支持多种语言自动生成代码

### Q2: 什么是变长编码（Varint）？
**答：**
- 用1-5个字节表示uint32，小数字用更少字节
- 每字节最高位标识是否继续：`1`表示后续还有字节，`0`表示结束
- 例如：`300 = 0xAC 0x02`（2字节），而不是固定4字节

**代码位置：** [mprpcchannel.cpp:352](src/rpc/mprpcchannel.cpp#L352)

### Q3: 如何保证数据完整性？
**答：**
1. **长度前缀**：先发送数据长度，接收端按长度读取
2. **循环接收**：使用while循环确保读取完整数据
   ```cpp
   // mprpcchannel.cpp:428-440
   while (received < header_size) {
       ret = recv(m_clientFd, header_buf.data() + received,
                  header_size - received, 0);
       received += ret;
   }
   ```
3. **Protobuf校验**：反序列化失败会返回错误

### Q4: 如何处理大数据传输？
**答：**
- **分块发送**：使用循环发送，避免一次性发送失败
  ```cpp
  // mprpcchannel.cpp:364-383
  while (sent < total_to_send) {
      ret = send(m_clientFd, data_ptr + sent, total_to_send - sent, 0);
      sent += ret;
  }
  ```
- **数据压缩**：支持LZ4/Zstd压缩（见RpcHeader定义）
- **超时控制**：设置5秒超时避免阻塞

**代码位置：** [mprpcchannel.cpp:92-115](src/rpc/mprpcchannel.cpp#L92-L115)

### Q5: 如何支持不同的RPC方法？
**答：**
通过**服务名+方法名**动态路由：
```cpp
// mprpcchannel.cpp:320-322
const ServiceDescriptor* sd = method->service();
std::string service_name = sd->name();    // 如 "KvServerRpc"
std::string method_name = method->name(); // 如 "PutAppend"
```
服务端根据这两个字段反射调用对应方法。

### Q6: 网络异常如何处理？
**答：**
1. **发送失败**：关闭连接，标记失败，触发重连
   ```cpp
   // mprpcchannel.cpp:376-380
   close(m_clientFd);
   m_clientFd = -1;
   HandleFailure();
   ```
2. **接收超时**：通过`SO_RCVTIMEO`设置5秒超时
3. **连接状态管理**：HEALTHY → PROBING → DISCONNECTED三态管理

**代码位置：** [mprpcchannel.cpp:154-196](src/rpc/mprpcchannel.cpp#L154-L196)

### Q7: 为什么要分离头部和数据？
**答：**
- **灵活性**：头部包含元数据（服务名、方法名、压缩标志），数据是业务参数
- **可扩展**：可以在头部添加新字段（如压缩、加密）而不影响数据部分
- **高效解析**：先读小头部确定数据大小，再精确读取数据，避免缓冲区浪费

### Q8: 如何实现零拷贝？
**答：**
使用Protobuf的`StringOutputStream`和`CodedOutputStream`：
```cpp
// mprpcchannel.cpp:348-349
google::protobuf::io::StringOutputStream string_output(&send_rpc_str);
google::protobuf::io::CodedOutputStream coded_output(&string_output);
```
直接写入目标字符串，避免中间缓冲区拷贝。

---

## 关键代码位置速查

| 功能 | 文件位置 |
|------|---------|
| 序列化请求 | [mprpcchannel.cpp:327](src/rpc/mprpcchannel.cpp#L327) |
| 构造头部 | [mprpcchannel.cpp:334-343](src/rpc/mprpcchannel.cpp#L334-L343) |
| 组装数据包 | [mprpcchannel.cpp:348-357](src/rpc/mprpcchannel.cpp#L348-L357) |
| 发送数据 | [mprpcchannel.cpp:364-383](src/rpc/mprpcchannel.cpp#L364-L383) |
| 读取头部长度 | [mprpcchannel.cpp:395-421](src/rpc/mprpcchannel.cpp#L395-L421) |
| 反序列化头部 | [mprpcchannel.cpp:445](src/rpc/mprpcchannel.cpp#L445) |
| 反序列化数据 | [mprpcchannel.cpp:472](src/rpc/mprpcchannel.cpp#L472) |
| Protobuf定义 | [rpcheader.proto:5-16](src/rpc/rpcheader.proto#L5-L16) |

---

## 总结

**核心答案：** RPC将信息转化为**Protobuf序列化的二进制字节流**，通过TCP socket传输。

**数据格式：** `[变长头部长度] + [Protobuf头部] + [Protobuf请求参数]`

**关键技术：**
- Protobuf序列化（高效、跨语言）
- 变长编码（节省空间）
- 长度前缀（保证完整性）
- 循环收发（处理大数据）
- 状态管理（处理异常）
