cmake_minimum_required(VERSION 3.16)
project(BinanceWebSocketAPI VERSION 1.0.0 LANGUAGES C CXX)

# 设置C++标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 设置编译选项
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -O2")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g -DDEBUG")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3 -DNDEBUG")

# 设置输出目录
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# 包含目录
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

# 使用主项目的第三方库路径
set(MAIN_3RD_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../)
include_directories(${MAIN_3RD_DIR}/yyjson/src)
include_directories(${MAIN_3RD_DIR}/IXWebSocket)
include_directories(${MAIN_3RD_DIR}/RxCpp/Rx/v2/src)

# 查找系统库
find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)

# 设置第三方库路径 - 使用主项目的库
set(YYJSON_DIR ${MAIN_3RD_DIR}/yyjson)
set(IXWEBSOCKET_DIR ${MAIN_3RD_DIR}/IXWebSocket)
set(RXCPP_DIR ${MAIN_3RD_DIR}/RxCpp)

# 构建yyjson库 - 使用主项目已构建的库，不重复构建
# yyjson库已在主项目中构建，这里只需要设置包含目录
# add_library(yyjson STATIC
#     ${YYJSON_DIR}/src/yyjson.c
# )
# target_include_directories(yyjson PUBLIC ${YYJSON_DIR}/src)
# set_target_properties(yyjson PROPERTIES LINKER_LANGUAGE C)

# 构建IXWebSocket库 - 使用主项目已构建的库
# IXWebSocket库已在主项目中构建，这里不需要重复构建
# if(NOT EXISTS ${IXWEBSOCKET_DIR}/libixwebsocket.a)
#     message(STATUS "Building IXWebSocket from source...")
#     
#     # 收集IXWebSocket源文件
#     file(GLOB_RECURSE IXWEBSOCKET_SOURCES 
#         ${IXWEBSOCKET_DIR}/ixwebsocket/*.cpp
#         ${IXWEBSOCKET_DIR}/ixwebsocket/*.c
#     )
#     
#     # 排除不需要的文件
#     list(FILTER IXWEBSOCKET_SOURCES EXCLUDE REGEX ".*test.*")
#     list(FILTER IXWEBSOCKET_SOURCES EXCLUDE REGEX ".*example.*")
#     
#     add_library(ixwebsocket STATIC ${IXWEBSOCKET_SOURCES})
#     target_include_directories(ixwebsocket PUBLIC ${IXWEBSOCKET_DIR})
#     target_link_libraries(ixwebsocket OpenSSL::SSL OpenSSL::Crypto Threads::Threads)
#     
#     # 设置编译定义
#     target_compile_definitions(ixwebsocket PRIVATE 
#         IXWEBSOCKET_USE_TLS=1
#         IXWEBSOCKET_USE_ZLIB=1
#     )
# else()
#     # 使用预编译的库
#     add_library(ixwebsocket_gateway STATIC IMPORTED)
#     set_target_properties(ixwebsocket_gateway PROPERTIES
#         IMPORTED_LOCATION ${IXWEBSOCKET_DIR}/libixwebsocket.a
#         INTERFACE_INCLUDE_DIRECTORIES ${IXWEBSOCKET_DIR}
#     )
# endif()

# 主项目源文件
set(SOURCES
    src/config_manager.cpp
    src/binance_websocket.cpp
    src/main.cpp
)

# 添加调试程序 - 注释掉因为没有debug_config.cpp文件
# add_executable(debug_config debug_config.cpp src/config_manager.cpp)
# target_link_libraries(debug_config yyjson)
# target_include_directories(debug_config PRIVATE include 3rd/yyjson/src)

# 创建gateway库
add_library(gateway STATIC
    src/config_manager.cpp
    src/binance_websocket.cpp
)

# 设置gateway库的包含目录
target_include_directories(gateway PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${YYJSON_DIR}/src
    ${IXWEBSOCKET_DIR}
    ${RXCPP_DIR}/Rx/v2/src
)

# 链接库到静态库
target_link_libraries(gateway
    yyjson
    ixwebsocket
    OpenSSL::SSL
    OpenSSL::Crypto
    Threads::Threads
    gcrypt
    gpg-error
)

# 设置编译定义
target_compile_definitions(gateway PRIVATE
    IXWEBSOCKET_USE_TLS=1
)

# 创建可执行文件
add_executable(${PROJECT_NAME} ${SOURCES})

# 链接库
target_link_libraries(${PROJECT_NAME}
    yyjson
    ixwebsocket
    OpenSSL::SSL
    OpenSSL::Crypto
    Threads::Threads
    gcrypt
    gpg-error
    dl
    z
)

# 设置包含目录
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${YYJSON_DIR}/src
    ${IXWEBSOCKET_DIR}
    ${RXCPP_DIR}/Rx/v2/src
)

# 编译定义
target_compile_definitions(${PROJECT_NAME} PRIVATE
    IXWEBSOCKET_USE_TLS=1
)

# 安装规则
install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION bin
)

install(DIRECTORY config/
    DESTINATION config
    FILES_MATCHING PATTERN "*.json"
)

# 创建测试目标 (可选)
option(BUILD_TESTS "Build tests" OFF)
if(BUILD_TESTS)
    enable_testing()
    
    # 查找GoogleTest
    find_package(GTest QUIET)
    if(NOT GTest_FOUND)
        # 使用本地的googletest
        add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/3rd/googletest)
        set(GTEST_LIBRARIES gtest gtest_main)
        set(GTEST_INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/3rd/googletest/googletest/include)
    else()
        set(GTEST_LIBRARIES GTest::gtest GTest::gtest_main)
    endif()
    
    # 测试源文件
    set(TEST_SOURCES
        tests/test_config_manager.cpp
        tests/test_binance_websocket.cpp
    )
    
    # 创建测试可执行文件
    add_executable(${PROJECT_NAME}_tests 
        ${TEST_SOURCES}
        src/config_manager.cpp
        src/binance_websocket.cpp
    )
    
    target_link_libraries(${PROJECT_NAME}_tests
        ${GTEST_LIBRARIES}
        yyjson
        ixwebsocket_gateway
        OpenSSL::SSL
        OpenSSL::Crypto
        Threads::Threads
        dl
        z
    )
    
    target_include_directories(${PROJECT_NAME}_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${GTEST_INCLUDE_DIRS}
        ${YYJSON_DIR}/src
        ${IXWEBSOCKET_DIR}
    )
    
    # 添加测试
    add_test(NAME ConfigManagerTest COMMAND ${PROJECT_NAME}_tests --gtest_filter=ConfigManager*)
    add_test(NAME BinanceWebSocketTest COMMAND ${PROJECT_NAME}_tests --gtest_filter=BinanceWebSocket*)
endif()

# 打印配置信息
message(STATUS "=== Build Configuration ===")
message(STATUS "Project: ${PROJECT_NAME}")
message(STATUS "Version: ${PROJECT_VERSION}")
message(STATUS "Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "Install Prefix: ${CMAKE_INSTALL_PREFIX}")
message(STATUS "Source Directory: ${CMAKE_CURRENT_SOURCE_DIR}")
message(STATUS "Binary Directory: ${CMAKE_CURRENT_BINARY_DIR}")
message(STATUS "============================")

# 脚本文件已经存在，不需要配置