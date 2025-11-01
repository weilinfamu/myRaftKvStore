#ifndef ISTORAGE_ENGINE_H
#define ISTORAGE_ENGINE_H

#include <string>

/**
 * @brief 存储引擎接口 - 抽象KV存储操作
 * 
 * 通过这个接口，可以轻松替换底层存储实现：
 * - SkipList
 * - LsmTree
 * - BTree
 * - RocksDB等
 */
class IStorageEngine {
public:
    virtual ~IStorageEngine() = default;
    
    /**
     * @brief 获取键对应的值
     * @param key 键
     * @param value 输出参数，存储查到的值
     * @return true表示找到，false表示不存在
     */
    virtual bool Get(const std::string& key, std::string* value) = 0;
    
    /**
     * @brief 设置键值对（如果存在则覆盖）
     * @param key 键
     * @param value 值
     */
    virtual void Put(const std::string& key, const std::string& value) = 0;
    
    /**
     * @brief 追加值到现有键（如果不存在则创建）
     * @param key 键
     * @param value 要追加的值
     */
    virtual void Append(const std::string& key, const std::string& value) = 0;
    
    /**
     * @brief 删除键值对
     * @param key 键
     */
    virtual void Delete(const std::string& key) = 0;
    
    /**
     * @brief 序列化存储引擎的所有数据
     * @return 序列化后的字符串
     */
    virtual std::string Serialize() = 0;
    
    /**
     * @brief 反序列化并恢复数据
     * @param data 序列化的数据
     */
    virtual void Deserialize(const std::string& data) = 0;
    
    /**
     * @brief 获取存储的键值对数量
     * @return 元素数量
     */
    virtual size_t Size() const = 0;
    
    /**
     * @brief 清空所有数据
     */
    virtual void Clear() = 0;
};

#endif  // ISTORAGE_ENGINE_H

