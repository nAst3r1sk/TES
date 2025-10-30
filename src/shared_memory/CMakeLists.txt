# 共享内存模块
set(SHARED_MEMORY_SOURCES
    control_info.cpp
    heartbeat_monitor.cpp
    memory_manager.cpp
    order_feedback_buffer.cpp
    order_report_buffer.cpp
    sequence_manager.cpp
    signal_buffer.cpp
    state_sync.cpp
)

# 包含头文件目录
include_directories(${CMAKE_SOURCE_DIR}/include/shared_memory)
include_directories(${CMAKE_SOURCE_DIR}/include/shared_memory/core)
include_directories(${CMAKE_SOURCE_DIR}/include/shared_memory/interfaces)
include_directories(${CMAKE_SOURCE_DIR}/include/shared_memory/adapters)
include_directories(${CMAKE_SOURCE_DIR}/include/common)

# 创建静态库
add_library(tes_shared_memory STATIC ${SHARED_MEMORY_SOURCES})

# 设置目标属性
set_target_properties(tes_shared_memory PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)