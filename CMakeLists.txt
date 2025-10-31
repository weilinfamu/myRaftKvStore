# 设置cmake的最低版本和项目名称
cmake_minimum_required(VERSION 3.22)
project(KVRaftCpp)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# 生成debug版本，可以进行gdb调试
set(CMAKE_BUILD_TYPE "Debug")

# ==================== 压缩库检测 ====================
# 尝试查找 LZ4 和 Zstd 压缩库
# 如果找不到，会使用占位实现（性能较差但能编译通过）

# 查找 LZ4
find_path(LZ4_INCLUDE_DIR lz4.h)
find_library(LZ4_LIBRARY lz4)

if(LZ4_INCLUDE_DIR AND LZ4_LIBRARY)
    message(STATUS "✅ Found LZ4: ${LZ4_LIBRARY}")
    add_definitions(-DHAVE_LZ4)
    set(COMPRESSION_LIBRARIES ${COMPRESSION_LIBRARIES} ${LZ4_LIBRARY})
    include_directories(${LZ4_INCLUDE_DIR})
else()
    message(WARNING "⚠️ LZ4 not found, using dummy implementation. Install: sudo apt install liblz4-dev")
endif()

# 查找 Zstd
find_path(ZSTD_INCLUDE_DIR zstd.h)
find_library(ZSTD_LIBRARY zstd)

if(ZSTD_INCLUDE_DIR AND ZSTD_LIBRARY)
    message(STATUS "✅ Found Zstd: ${ZSTD_LIBRARY}")
    add_definitions(-DHAVE_ZSTD)
    set(COMPRESSION_LIBRARIES ${COMPRESSION_LIBRARIES} ${ZSTD_LIBRARY})
    include_directories(${ZSTD_INCLUDE_DIR})
else()
    message(WARNING "⚠️ Zstd not found, using dummy implementation. Install: sudo apt install libzstd-dev")
endif()

message(STATUS "==================== Compression Libraries ====================")
message(STATUS "LZ4: ${LZ4_LIBRARY}")
message(STATUS "Zstd: ${ZSTD_LIBRARY}")
message(STATUS "Total compression libs: ${COMPRESSION_LIBRARIES}")
message(STATUS "================================================================")
# ==================== 压缩库检测结束 ====================

# 设置项目可执行文件输出的路径
set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin)
# 设置项目库文件输出的路径
set(LIBRARY_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/lib)

# 设置项目编译头文件搜索路径 -I
# 目前可能存在路径污染的问题，需要进一步解决
include_directories(${PROJECT_SOURCE_DIR}/src/common/include)
include_directories(${PROJECT_SOURCE_DIR}/src/fiber/include)
include_directories(${PROJECT_SOURCE_DIR}/src/rpc/include)
include_directories(${PROJECT_SOURCE_DIR}/example)
include_directories(${PROJECT_SOURCE_DIR}/src/raftCore/include)
include_directories(${PROJECT_SOURCE_DIR}/src/raftRpcPro/include)
include_directories(${PROJECT_SOURCE_DIR}/src/raftClerk/include)
include_directories(${PROJECT_SOURCE_DIR}/src/skipList/include)


# 设置项目库文件搜索路径 -L
link_directories(${PROJECT_SOURCE_DIR}/lib)

# src包含了所有的相关代码
add_subdirectory(src)
# example包含了使用的示例代码
add_subdirectory(example)

add_library(skip_list_on_raft STATIC  ${src_rpc} ${src_fiber} ${rpc_example} ${raftsource} ${src_raftCore} ${src_raftRpcPro})
target_link_libraries(skip_list_on_raft muduo_net muduo_base pthread dl ${COMPRESSION_LIBRARIES})
# 添加格式化目标 start
# from : https://blog.csdn.net/guotianqing/article/details/121661067

add_custom_target(format
        COMMAND bash ${PROJECT_SOURCE_DIR}/format.sh
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
        COMMAND echo "format done!"
)


# 添加格式化目标 end

# 添加性能测试程序（如果文件存在）
if(EXISTS ${PROJECT_SOURCE_DIR}/kv_raft_performance_test.cpp)
  add_executable(kv_raft_performance_test kv_raft_performance_test.cpp src/raftClerk/clerk.cpp src/raftClerk/raftServerRpcUtil.cpp src/common/util.cpp src/common/compressor.cpp)
  target_link_libraries(kv_raft_performance_test skip_list_on_raft muduo_net muduo_base pthread dl boost_serialization protobuf ${COMPRESSION_LIBRARIES})
endif()

# 协程压测客户端（如果文件存在）
if(EXISTS ${PROJECT_SOURCE_DIR}/fiber_stress_test.cpp)
  add_executable(fiber_stress_test fiber_stress_test.cpp src/raftClerk/clerk.cpp src/raftClerk/raftServerRpcUtil.cpp src/common/util.cpp src/common/compressor.cpp)
  target_link_libraries(fiber_stress_test skip_list_on_raft muduo_net muduo_base pthread dl boost_serialization protobuf ${COMPRESSION_LIBRARIES})
endif()
