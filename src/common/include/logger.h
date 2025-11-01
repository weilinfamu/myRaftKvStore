#ifndef LOGGER_H
#define LOGGER_H

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/async.h>
#include <memory>
#include <string>
#include <iostream>  // 添加iostream

/**
 * @brief 日志管理器 - 基于 spdlog 的封装
 * 
 * 特性：
 * 1. 异步日志 - 不阻塞业务线程
 * 2. 日志级别 - Trace/Debug/Info/Warn/Error/Critical
 * 3. 自动轮转 - 按大小切分日志文件（默认 10MB）
 * 4. 彩色输出 - 控制台彩色显示，便于调试
 * 5. 格式化 - 支持时间戳、线程ID、日志级别
 */
class Logger {
public:
    /**
     * @brief 初始化日志系统
     * @param log_name 日志名称（如 "raft_node_0"）
     * @param log_file 日志文件路径（如 "logs/raft_0.log"）
     * @param level 日志级别（默认 Info）
     */
    static void Init(const std::string& log_name = "raft",
                    const std::string& log_file = "logs/raft.log",
                    spdlog::level::level_enum level = spdlog::level::info) {
        try {
            // 创建异步日志器（队列大小 8192，后台线程数 1）
            spdlog::init_thread_pool(8192, 1);
            
            // 创建 sink：控制台 + 文件（自动轮转）
            std::vector<spdlog::sink_ptr> sinks;
            
            // 控制台 sink（彩色输出）
            auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
            console_sink->set_level(level);
            console_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [thread %t] %v");
            sinks.push_back(console_sink);
            
            // 文件 sink（自动轮转：10MB 一个文件，保留 3 个文件）
            auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                log_file, 1024 * 1024 * 10, 3);
            file_sink->set_level(level);
            file_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [thread %t] %v");
            sinks.push_back(file_sink);
            
            // 创建异步 logger
            auto logger = std::make_shared<spdlog::async_logger>(
                log_name,
                sinks.begin(),
                sinks.end(),
                spdlog::thread_pool(),
                spdlog::async_overflow_policy::block
            );
            
            logger->set_level(level);
            
            // 设置为默认 logger
            spdlog::set_default_logger(logger);
            
            // 立即刷盘级别（Error 及以上）
            spdlog::flush_on(spdlog::level::err);
            
            // 定期刷盘（每 3 秒）
            spdlog::flush_every(std::chrono::seconds(3));
            
            spdlog::info("Logger initialized: {}", log_name);
            
        } catch (const spdlog::spdlog_ex& ex) {
            std::cerr << "Log initialization failed: " << ex.what() << std::endl;
        }
    }
    
    /**
     * @brief 设置日志级别
     */
    static void SetLevel(spdlog::level::level_enum level) {
        spdlog::set_level(level);
    }
    
    /**
     * @brief 关闭日志系统（程序退出前调用）
     */
    static void Shutdown() {
        spdlog::shutdown();
    }
};

// ==================== 兼容旧的 DPrintf 接口 ====================

// 调试模式下启用日志
#ifdef Debug
    #define LOG_TRACE(...)    SPDLOG_TRACE(__VA_ARGS__)
    #define LOG_DEBUG(...)    SPDLOG_DEBUG(__VA_ARGS__)
    #define LOG_INFO(...)     SPDLOG_INFO(__VA_ARGS__)
    #define LOG_WARN(...)     SPDLOG_WARN(__VA_ARGS__)
    #define LOG_ERROR(...)    SPDLOG_ERROR(__VA_ARGS__)
    #define LOG_CRITICAL(...) SPDLOG_CRITICAL(__VA_ARGS__)
    
    // 兼容旧的 DPrintf（映射到 DEBUG 级别）
    #define DPrintf(fmt, ...) SPDLOG_DEBUG(fmt, ##__VA_ARGS__)
#else
    // Release 模式下禁用 DEBUG 和 TRACE
    #define LOG_TRACE(...)    ((void)0)
    #define LOG_DEBUG(...)    ((void)0)
    #define LOG_INFO(...)     SPDLOG_INFO(__VA_ARGS__)
    #define LOG_WARN(...)     SPDLOG_WARN(__VA_ARGS__)
    #define LOG_ERROR(...)    SPDLOG_ERROR(__VA_ARGS__)
    #define LOG_CRITICAL(...) SPDLOG_CRITICAL(__VA_ARGS__)
    
    // Release 模式下 DPrintf 不输出
    #define DPrintf(fmt, ...) ((void)0)
#endif

#endif  // LOGGER_H

