# 配置文件说明

## 📁 配置文件列表

### 本地测试配置

| 配置文件 | 节点数 | 端口范围 | 用途 |
|---------|-------|---------|------|
| `test_3nodes.conf` | 3 | 17830-17832 | 基础测试、快速验证 |
| `test_5nodes.conf` | 5 | 17830-17834 | 容错性测试、Leader选举 |
| `test_7nodes.conf` | 7 | 17830-17836 | 高可用测试、分区容错 |

### 分布式部署配置

| 配置文件 | 节点数 | 说明 |
|---------|-------|------|
| `test_distributed.conf` | 5 | 多机部署示例（需修改IP） |

---

## 🚀 快速开始

### 1. 启动 3 节点集群（最常用）

```bash
# 终端 1 - 启动节点 0
./bin/raftCoreRun callee config/test_3nodes.conf 0

# 终端 2 - 启动节点 1
./bin/raftCoreRun callee config/test_3nodes.conf 1

# 终端 3 - 启动节点 2
./bin/raftCoreRun callee config/test_3nodes.conf 2

# 终端 4 - 启动客户端
./bin/callerMain caller config/test_3nodes.conf
```

---

### 2. 启动 5 节点集群

```bash
# 使用脚本批量启动（推荐）
for i in {0..4}; do
    ./bin/raftCoreRun callee config/test_5nodes.conf $i &
done

# 等待集群启动
sleep 3

# 启动客户端
./bin/callerMain caller config/test_5nodes.conf
```

---

### 3. 启动 7 节点集群

```bash
# 使用脚本批量启动
for i in {0..6}; do
    ./bin/raftCoreRun callee config/test_7nodes.conf $i &
done

# 等待集群启动
sleep 5

# 启动客户端
./bin/callerMain caller config/test_7nodes.conf
```

---

## 📝 配置文件格式

```bash
# 节点 0
node0ip=<IP地址>
node0port=<端口号>

# 节点 1
node1ip=<IP地址>
node1port=<端口号>

# 节点 2
node2ip=<IP地址>
node2port=<端口号>

# ... 以此类推
```

### 字段说明

- `nodeXip`: 节点 X 的 IP 地址
  - 本地测试：`127.0.0.1`
  - 分布式部署：实际服务器 IP
- `nodeXport`: 节点 X 的端口号
  - 推荐范围：`17830-17899`
  - 避免冲突：确保每个节点端口唯一

---

## 🔧 自定义配置

### 创建自定义配置

```bash
# 复制模板
cp config/test_3nodes.conf config/my_config.conf

# 编辑配置
vim config/my_config.conf

# 使用自定义配置
./bin/raftCoreRun callee config/my_config.conf 0
```

---

## ⚠️ 注意事项

### 端口选择

1. **避免冲突**：
   - 确保端口未被其他程序占用
   - 使用 `netstat -tuln | grep <port>` 检查

2. **防火墙设置**：
   - 本地测试：无需配置
   - 分布式部署：确保端口在防火墙中开放
   ```bash
   # CentOS/RHEL
   sudo firewall-cmd --permanent --add-port=17830-17899/tcp
   sudo firewall-cmd --reload
   
   # Ubuntu
   sudo ufw allow 17830:17899/tcp
   ```

3. **端口范围建议**：
   - 开发环境：`17830-17899`
   - 测试环境：`18830-18899`
   - 生产环境：`19830-19899`

---

### 节点数量选择

| 节点数 | 容错能力 | 推荐场景 |
|-------|---------|---------|
| 3 | 1 节点 | 开发、基础测试 |
| 5 | 2 节点 | 生产环境、高可用 |
| 7 | 3 节点 | 金融级可靠性 |

**Raft 共识规则**：
- 集群需要 **超过半数** 节点存活才能工作
- 3 节点：最多容忍 1 个故障
- 5 节点：最多容忍 2 个故障
- 7 节点：最多容忍 3 个故障

---

## 🧪 测试场景

### 场景 1：基础功能测试（3节点）

```bash
# 使用 test_3nodes.conf
config/test_3nodes.conf
```

**测试内容**：
- Put/Get 基本操作
- Leader 选举
- 日志复制

---

### 场景 2：容错性测试（5节点）

```bash
# 使用 test_5nodes.conf
config/test_5nodes.conf
```

**测试内容**：
1. 启动 5 个节点
2. 杀死 Leader 节点
3. 观察是否能重新选举
4. 测试数据是否一致

```bash
# 杀死节点 0
kill $(ps aux | grep "raftCoreRun.*config/test_5nodes.conf 0" | grep -v grep | awk '{print $2}')

# 观察日志
tail -f logs/raft_node_1.log logs/raft_node_2.log
```

---

### 场景 3：网络分区测试（7节点）

```bash
# 使用 test_7nodes.conf
config/test_7nodes.conf
```

**测试内容**：
1. 启动 7 个节点
2. 使用 `iptables` 模拟网络分区
3. 观察分区恢复后的行为

```bash
# 模拟分区：隔离节点 0-2 和节点 3-6
sudo iptables -A INPUT -p tcp --sport 17830:17832 -j DROP
sudo iptables -A OUTPUT -p tcp --dport 17830:17832 -j DROP

# 恢复分区
sudo iptables -F
```

---

## 📚 相关文档

- `../docs/快速开始指南.md` - 项目入门
- `../docs/测试指南.md` - 详细测试说明
- `../docs/部署指南.md` - 生产环境部署

---

## ❓ 常见问题

### Q1: 配置文件应该放在哪里？

**A:** 统一放在 `config/` 目录下，项目根目录也可以，但推荐使用 `config/`。

---

### Q2: 如何快速切换配置？

**A:** 使用软链接：
```bash
# 创建默认配置链接
ln -s config/test_3nodes.conf test.conf

# 切换到 5 节点
ln -sf config/test_5nodes.conf test.conf
```

---

### Q3: 端口被占用怎么办？

**A:** 修改配置文件中的端口号，或查找占用进程：
```bash
# 查找占用端口的进程
lsof -i :17830

# 杀死进程
kill <PID>
```

---

**最后更新**: 2025-11-01

