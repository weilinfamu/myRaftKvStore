#include "compressor.h"
#include <iostream>
#include <cstring>

// 尝试包含压缩库头文件
// 如果系统没有安装，会使用占位实现
#ifdef HAVE_LZ4
#include <lz4.h>
#endif

#ifdef HAVE_ZSTD
#include <zstd.h>
#endif

// ==================== LZ4 压缩实现 ====================

std::string Compressor::compressLZ4(const std::string& input) {
    if (input.empty()) {
        return "";
    }
    
#ifdef HAVE_LZ4
    // 实际 LZ4 压缩实现
    int max_dst_size = LZ4_compressBound(input.size());
    std::string output(max_dst_size, '\0');
    
    int compressed_size = LZ4_compress_default(
        input.data(),
        &output[0],
        input.size(),
        max_dst_size
    );
    
    if (compressed_size <= 0) {
        throw std::runtime_error("LZ4 compression failed");
    }
    
    output.resize(compressed_size);
    
    // 统计信息（调试用）
    double ratio = (double)input.size() / compressed_size;
    std::cout << "[Compressor] LZ4 compressed: " << input.size() 
              << " -> " << compressed_size << " bytes (" 
              << ratio << "x)" << std::endl;
    
    return output;
#else
    // 占位实现：简单的RLE压缩（仅用于演示）
    std::cout << "[Compressor] WARNING: LZ4 not available, using dummy compression" << std::endl;
    
    std::string output;
    output.reserve(input.size() / 2);  // 预估
    
    size_t i = 0;
    while (i < input.size()) {
        char ch = input[i];
        size_t count = 1;
        
        // 统计重复字符
        while (i + count < input.size() && input[i + count] == ch && count < 255) {
            count++;
        }
        
        if (count > 3) {
            // 使用RLE编码：0xFF + count + char
            output.push_back((char)0xFF);
            output.push_back((char)count);
            output.push_back(ch);
            i += count;
        } else {
            // 直接存储
            for (size_t j = 0; j < count; ++j) {
                output.push_back(ch);
            }
            i += count;
        }
    }
    
    double ratio = (double)input.size() / output.size();
    std::cout << "[Compressor] Dummy compressed: " << input.size() 
              << " -> " << output.size() << " bytes (" 
              << ratio << "x)" << std::endl;
    
    return output;
#endif
}

std::string Compressor::decompressLZ4(const std::string& compressed, size_t originalSize) {
    if (compressed.empty() || originalSize == 0) {
        return "";
    }
    
#ifdef HAVE_LZ4
    // 实际 LZ4 解压实现
    std::string output(originalSize, '\0');
    
    int decompressed_size = LZ4_decompress_safe(
        compressed.data(),
        &output[0],
        compressed.size(),
        originalSize
    );
    
    if (decompressed_size != static_cast<int>(originalSize)) {
        throw std::runtime_error("LZ4 decompression failed");
    }
    
    return output;
#else
    // 占位实现：RLE解压
    std::string output;
    output.reserve(originalSize);
    
    size_t i = 0;
    while (i < compressed.size() && output.size() < originalSize) {
        if ((unsigned char)compressed[i] == 0xFF && i + 2 < compressed.size()) {
            // RLE编码
            size_t count = (unsigned char)compressed[i + 1];
            char ch = compressed[i + 2];
            for (size_t j = 0; j < count && output.size() < originalSize; ++j) {
                output.push_back(ch);
            }
            i += 3;
        } else {
            // 直接存储
            output.push_back(compressed[i]);
            i++;
        }
    }
    
    if (output.size() != originalSize) {
        throw std::runtime_error("Dummy decompression size mismatch");
    }
    
    return output;
#endif
}

// ==================== Zstd 压缩实现 ====================

std::string Compressor::compressZstd(const std::string& input, int level) {
    if (input.empty()) {
        return "";
    }
    
    // 限制压缩级别范围
    if (level < 1) level = 1;
    if (level > 22) level = 22;
    
#ifdef HAVE_ZSTD
    // 实际 Zstd 压缩实现
    size_t max_dst_size = ZSTD_compressBound(input.size());
    std::string output(max_dst_size, '\0');
    
    size_t compressed_size = ZSTD_compress(
        &output[0],
        max_dst_size,
        input.data(),
        input.size(),
        level
    );
    
    if (ZSTD_isError(compressed_size)) {
        throw std::runtime_error(
            std::string("Zstd compression failed: ") + 
            ZSTD_getErrorName(compressed_size)
        );
    }
    
    output.resize(compressed_size);
    
    // 统计信息
    double ratio = (double)input.size() / compressed_size;
    std::cout << "[Compressor] Zstd-" << level << " compressed: " 
              << input.size() << " -> " << compressed_size 
              << " bytes (" << ratio << "x)" << std::endl;
    
    return output;
#else
    // 占位实现：使用dummy压缩
    std::cout << "[Compressor] WARNING: Zstd not available, using dummy compression" << std::endl;
    return compressLZ4(input);  // 使用LZ4的占位实现
#endif
}

std::string Compressor::decompressZstd(const std::string& compressed) {
    if (compressed.empty()) {
        return "";
    }
    
#ifdef HAVE_ZSTD
    // 实际 Zstd 解压实现
    unsigned long long original_size = ZSTD_getFrameContentSize(
        compressed.data(),
        compressed.size()
    );
    
    if (original_size == ZSTD_CONTENTSIZE_ERROR) {
        throw std::runtime_error("Zstd: not a valid frame");
    }
    
    if (original_size == ZSTD_CONTENTSIZE_UNKNOWN) {
        throw std::runtime_error("Zstd: original size unknown");
    }
    
    std::string output(original_size, '\0');
    
    size_t decompressed_size = ZSTD_decompress(
        &output[0],
        original_size,
        compressed.data(),
        compressed.size()
    );
    
    if (ZSTD_isError(decompressed_size)) {
        throw std::runtime_error(
            std::string("Zstd decompression failed: ") + 
            ZSTD_getErrorName(decompressed_size)
        );
    }
    
    return output;
#else
    // 占位实现：尝试用LZ4解压（假设原始大小在前4字节）
    if (compressed.size() < 4) {
        throw std::runtime_error("Invalid compressed data");
    }
    
    // 简单假设：未压缩返回原数据
    std::cout << "[Compressor] WARNING: Zstd not available, returning original data" << std::endl;
    return compressed;
#endif
}

// ==================== 自适应压缩 ====================

std::string Compressor::compressAdaptive(const std::string& input, Type type) {
    // 策略1：小数据不压缩
    if (input.size() < MIN_COMPRESS_SIZE) {
        std::string output;
        output.resize(sizeof(CompressionHeader) + input.size());
        
        CompressionHeader* header = reinterpret_cast<CompressionHeader*>(&output[0]);
        header->magic = LZ4_MAGIC;  // 使用LZ4魔数作为标识
        header->type = static_cast<uint8_t>(Type::NONE);
        header->level = 0;
        header->originalSize = input.size();
        
        std::memcpy(&output[sizeof(CompressionHeader)], input.data(), input.size());
        return output;
    }
    
    // 策略2：根据类型压缩
    std::string compressed;
    CompressionHeader header;
    
    try {
        if (type == Type::LZ4) {
            compressed = compressLZ4(input);
            header.magic = LZ4_MAGIC;
            header.type = static_cast<uint8_t>(Type::LZ4);
            header.level = 0;
            header.originalSize = input.size();
        } else if (type == Type::ZSTD) {
            compressed = compressZstd(input, 3);  // 使用level 3
            header.magic = ZSTD_MAGIC;
            header.type = static_cast<uint8_t>(Type::ZSTD);
            header.level = 3;
            header.originalSize = input.size();
        } else {
            // 不压缩
            header.magic = LZ4_MAGIC;
            header.type = static_cast<uint8_t>(Type::NONE);
            header.level = 0;
            header.originalSize = input.size();
            compressed = input;
        }
        
        // 策略3：检查压缩效果
        double ratio = (double)input.size() / compressed.size();
        if (ratio < 1.1) {
            // 压缩率不够好（< 1.1x），不压缩
            std::cout << "[Compressor] Compression ratio too low (" << ratio 
                      << "x), using original data" << std::endl;
            
            std::string output;
            output.resize(sizeof(CompressionHeader) + input.size());
            
            CompressionHeader* out_header = reinterpret_cast<CompressionHeader*>(&output[0]);
            out_header->magic = LZ4_MAGIC;
            out_header->type = static_cast<uint8_t>(Type::NONE);
            out_header->level = 0;
            out_header->originalSize = input.size();
            
            std::memcpy(&output[sizeof(CompressionHeader)], input.data(), input.size());
            return output;
        }
        
        // 组装最终数据：header + compressed
        std::string output;
        output.resize(sizeof(CompressionHeader) + compressed.size());
        
        std::memcpy(&output[0], &header, sizeof(CompressionHeader));
        std::memcpy(&output[sizeof(CompressionHeader)], compressed.data(), compressed.size());
        
        return output;
        
    } catch (const std::exception& e) {
        // 压缩失败，降级为不压缩
        std::cerr << "[Compressor] Compression failed: " << e.what() 
                  << ", fallback to no compression" << std::endl;
        
        std::string output;
        output.resize(sizeof(CompressionHeader) + input.size());
        
        CompressionHeader* out_header = reinterpret_cast<CompressionHeader*>(&output[0]);
        out_header->magic = LZ4_MAGIC;
        out_header->type = static_cast<uint8_t>(Type::NONE);
        out_header->level = 0;
        out_header->originalSize = input.size();
        
        std::memcpy(&output[sizeof(CompressionHeader)], input.data(), input.size());
        return output;
    }
}

std::string Compressor::decompressAdaptive(const std::string& compressed, Type* type) {
    if (compressed.size() < sizeof(CompressionHeader)) {
        throw std::runtime_error("Invalid compressed data: too small");
    }
    
    // 读取头部
    const CompressionHeader* header = reinterpret_cast<const CompressionHeader*>(compressed.data());
    
    // 验证魔数
    if (header->magic != LZ4_MAGIC && header->magic != ZSTD_MAGIC) {
        throw std::runtime_error("Invalid compressed data: bad magic number");
    }
    
    Type compression_type = static_cast<Type>(header->type);
    if (type) {
        *type = compression_type;
    }
    
    // 提取压缩数据部分
    std::string compressed_data = compressed.substr(sizeof(CompressionHeader));
    
    // 根据类型解压
    if (compression_type == Type::NONE) {
        return compressed_data;
    } else if (compression_type == Type::LZ4) {
        return decompressLZ4(compressed_data, header->originalSize);
    } else if (compression_type == Type::ZSTD) {
        return decompressZstd(compressed_data);
    } else {
        throw std::runtime_error("Unknown compression type");
    }
}

