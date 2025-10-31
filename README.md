# MyRaftKV - 基于Raft的分布式KV存储系统


> **当前维护者**  
> 本项目由 [weilinfamu](https://github.com/weilinfamu) 维护，包含性能优化、协程化改造、RPC增强等改进。

> **注意**：本项目的目的是学习Raft的原理，并实现一个简单的k-v存储数据库。因此并不适用于生产环境。


> notice：本项目的目的是学习Raft的原理，并实现一个简单的k-v存储数据库。因此并不适用于生产环境。

## 分支说明
- main：最新内容，已经实现一个简单的clerk
- rpc：基于muduo和rpc框架相关内容
- raft_DB：基于Raft的k-v存储数据库，主要用于观察选举过程

## 使用方法

### 1.库准备
- muduo
- boost
- protoc
- clang-format（可选）

