cmake_minimum_required(VERSION 3.16)
project(BinanceAPITest LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 查找依赖库
find_package(PkgConfig REQUIRED)
find_package(OpenSSL REQUIRED)

# 添加第三方库目录
add_subdirectory(../3rd/yyjson yyjson_build)
add_subdirectory(../3rd/IXWebSocket ixwebsocket_build)

# 设置yyjson库的链接语言
set_target_properties(yyjson PROPERTIES LINKER_LANGUAGE C)

# 创建测试可执行文件
add_executable(test_api_key test_api_key.cpp)

# 链接库
target_link_libraries(test_api_key
    yyjson
    ixwebsocket
    OpenSSL::SSL
    OpenSSL::Crypto
    pthread
)

# 设置包含目录
target_include_directories(test_api_key PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../3rd/yyjson/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../3rd/IXWebSocket
)