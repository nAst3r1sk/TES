# 执行模块
set(EXECUTION_SOURCES
    execution_controller.cpp
    order_manager.cpp
    order_state_machine.cpp
    twap_algorithm.cpp
    trading_rule_checker.cpp
    position_manager.cpp
    performance_monitor.cpp
    thread_pool.cpp
    async_callback_manager.cpp
    config_manager.cpp
    binance_account_websocket.cpp
    signal_transmission_manager.cpp
    json_feedback_writer.cpp
    position_subscription_manager.cpp
    gateway_adapter.cpp
)

# 包含头文件目录
include_directories(${CMAKE_SOURCE_DIR}/include/execution)
include_directories(${CMAKE_SOURCE_DIR}/include/common)
include_directories(${CMAKE_SOURCE_DIR}/include/shared_memory)
include_directories(${CMAKE_SOURCE_DIR}/3rd/gateway/include)
include_directories(${CMAKE_SOURCE_DIR}/3rd/IXWebSocket)
#include_directories(${CMAKE_SOURCE_DIR}/include/risk_control)
#include_directories(${CMAKE_SOURCE_DIR}/include/strategy)

# 创建静态库
add_library(tes_execution STATIC ${EXECUTION_SOURCES})

# 设置目标属性
set_target_properties(tes_execution PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)

# 链接依赖库
target_link_libraries(tes_execution 
    tes_shared_memory
    tes_utils
    ixwebsocket
    OpenSSL::SSL
    OpenSSL::Crypto
    Threads::Threads
)