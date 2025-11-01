#ifndef SKIPLIST_STORAGE_ENGINE_H
#define SKIPLIST_STORAGE_ENGINE_H

#include <mutex>
#include "IStorageEngine.h"
#include "skipList.h"

/**
 * @brief SkipList存储引擎适配器
 * 
 * 将现有的SkipList包装为IStorageEngine接口，
 * 使其可以与新架构无缝集成。
 */
class SkipListStorageEngine : public IStorageEngine {
private:
    SkipList<std::string, std::string> m_skipList;
    mutable std::mutex m_mtx;
    
public:
    explicit SkipListStorageEngine(int maxLevel = 18) : m_skipList(maxLevel) {}
    
    bool Get(const std::string& key, std::string* value) override {
        std::lock_guard<std::mutex> lock(m_mtx);
        return m_skipList.search_element(const_cast<std::string&>(key), *value);
    }
    
    void Put(const std::string& key, const std::string& value) override {
        std::lock_guard<std::mutex> lock(m_mtx);
        std::string k = key;
        std::string v = value;
        m_skipList.insert_set_element(k, v);
    }
    
    void Append(const std::string& key, const std::string& value) override {
        std::lock_guard<std::mutex> lock(m_mtx);
        std::string existingValue;
        std::string k = key;
        
        if (m_skipList.search_element(k, existingValue)) {
            // 键存在，追加值
            std::string newValue = existingValue + value;
            m_skipList.delete_element(k);
            m_skipList.insert_element(k, newValue);
        } else {
            // 键不存在，直接插入
            std::string v = value;
            m_skipList.insert_element(k, v);
        }
    }
    
    void Delete(const std::string& key) override {
        std::lock_guard<std::mutex> lock(m_mtx);
        std::string k = key;
        m_skipList.delete_element(k);
    }
    
    std::string Serialize() override {
        std::lock_guard<std::mutex> lock(m_mtx);
        return m_skipList.dump_file();
    }
    
    void Deserialize(const std::string& data) override {
        std::lock_guard<std::mutex> lock(m_mtx);
        m_skipList.load_file(data);
    }
    
    size_t Size() const override {
        // 注意：SkipList::size()不是const方法，这里需要const_cast
        // 或者考虑不加锁直接返回（size()本身是线程安全的）
        return const_cast<SkipList<std::string, std::string>&>(m_skipList).size();
    }
    
    void Clear() override {
        std::lock_guard<std::mutex> lock(m_mtx);
        // SkipList没有clear方法，需要重新构造
        // 这里暂时不实现，因为原项目中没有用到
    }
};

#endif  // SKIPLIST_STORAGE_ENGINE_H

