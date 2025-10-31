#ifndef COMPRESSOR_H
#define COMPRESSOR_H

#include <string>
#include <stdexcept>
#include <cstring>

/**
 * @brief 数据压缩器 - 支持 LZ4 和 Zstd 两种算法
 * 
 * 算法选择理由：
 * 1. LZ4: 极速压缩（550 MB/s），适合日志和RPC传输
 *    - 压缩率：2.2x
 *    - CPU消耗：极低（~2%）
 *    - 延迟敏感场景的最佳选择
 * 
 * 2. Zstd: 平衡速度和压缩率（330 MB/s @ level 3）
 *    - 压缩率：3.3x
 *    - CPU消耗：中等（~5%）
 *    - 适合快照持久化
 * 
 * 为什么不用其他算法：
 * - Gzip: 太慢（25 MB/s），不适合实时系统
 * - Huffman: 单独使用压缩率低（~1.5x），现代算法已内置
 * - Snappy: 性能接近LZ4但生态不如LZ4成熟
 */
class Compressor {
public:
    // 压缩类型枚举
    enum class Type : uint8_t {
        NONE = 0,    // 不压缩
        LZ4 = 1,     // LZ4 压缩
        ZSTD = 2     // Zstd 压缩
    };
    
    /**
     * @brief LZ4 压缩 - 极速压缩，适合日志和RPC
     * 
     * 性能特点：
     * - 压缩速度：550 MB/s
     * - 解压速度：2200 MB/s（极快）
     * - 压缩率：2.0-2.5x
     * - CPU占用：~2%
     * 
     * @param input 原始数据
     * @return 压缩后的数据
     */
    static std::string compressLZ4(const std::string& input);
    
    /**
     * @brief LZ4 解压
     * 
     * @param compressed 压缩数据
     * @param originalSize 原始数据大小（必须提供）
     * @return 解压后的数据
     */
    static std::string decompressLZ4(const std::string& compressed, size_t originalSize);
    
    /**
     * @brief Zstd 压缩 - 平衡速度和压缩率，适合快照
     * 
     * 性能特点：
     * - 压缩速度：330 MB/s @ level 3
     * - 解压速度：950 MB/s
     * - 压缩率：3.0-3.5x @ level 3
     * - CPU占用：~5%
     * 
     * Level 选择建议：
     * - Level 1: 最快（470 MB/s），压缩率 2.8x
     * - Level 3: 推荐（330 MB/s），压缩率 3.3x ⭐
     * - Level 5: 高压缩（250 MB/s），压缩率 3.8x
     * - Level 9: 很高压缩（40 MB/s），压缩率 4.5x
     * 
     * @param input 原始数据
     * @param level 压缩级别（1-22，推荐3）
     * @return 压缩后的数据
     */
    static std::string compressZstd(const std::string& input, int level = 3);
    
    /**
     * @brief Zstd 解压
     * 
     * @param compressed 压缩数据（必须包含原始大小信息）
     * @return 解压后的数据
     */
    static std::string decompressZstd(const std::string& compressed);
    
    /**
     * @brief 自适应压缩 - 根据数据大小和类型自动选择算法
     * 
     * 策略：
     * - 数据 < 512B：不压缩（开销 > 收益）
     * - 数据 < 4KB：LZ4（快速）
     * - 数据 >= 4KB：根据type选择
     * 
     * @param input 原始数据
     * @param type 压缩类型
     * @return 压缩后的数据
     */
    static std::string compressAdaptive(const std::string& input, Type type);
    
    /**
     * @brief 自适应解压 - 根据压缩头部自动识别算法
     * 
     * @param compressed 压缩数据（包含类型头）
     * @param type 输出：识别的压缩类型
     * @return 解压后的数据
     */
    static std::string decompressAdaptive(const std::string& compressed, Type* type = nullptr);
    
private:
    // 压缩头部结构（8字节）
    struct CompressionHeader {
        uint32_t magic;          // 魔数：0x4C5A3442 ("LZ4B") or 0x5A535444 ("ZSTD")
        uint8_t type;            // 压缩类型
        uint8_t level;           // 压缩级别（Zstd用）
        uint16_t reserved;       // 保留
        uint32_t originalSize;   // 原始大小（LZ4需要）
        
        CompressionHeader() : magic(0), type(0), level(0), reserved(0), originalSize(0) {}
    } __attribute__((packed));
    
    static constexpr uint32_t LZ4_MAGIC = 0x4C5A3442;   // "LZ4B"
    static constexpr uint32_t ZSTD_MAGIC = 0x5A535444;  // "ZSTD"
    static constexpr size_t MIN_COMPRESS_SIZE = 512;     // 最小压缩大小
};

#endif  // COMPRESSOR_H

