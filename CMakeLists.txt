cmake_minimum_required(VERSION 3.16)
project(TradingExecutionSystem VERSION 1.0.0 LANGUAGES C CXX)

# 设置C++标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 编译选项
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -O3 -march=native")
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -DDEBUG")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")

# 设置库输出目录到项目根目录的lib文件夹
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/bin)

# 包含目录
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

# 添加第三方库目录
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/3rd)

# 查找必要的包
find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
find_package(OpenSSL REQUIRED)

# 使用本地GTest
find_package(GTest QUIET)
if(NOT GTest_FOUND)
    message(STATUS "GTest not found in system, using local version from /3rd/")
    # 添加本地GTest
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/3rd/googletest)
    set(GTEST_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/3rd/googletest/googletest/include)
    set(GTEST_LIBRARY gtest)
    set(GTEST_MAIN_LIBRARY gtest_main)
    include_directories(${GTEST_INCLUDE_DIR})
endif()

# RxCpp (header-only library)
set(RXCPP_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/3rd/RxCpp/Rx/v2/src)
if(NOT EXISTS ${RXCPP_INCLUDE_DIR})
    message(FATAL_ERROR "RxCpp not found at ${RXCPP_INCLUDE_DIR}. Please ensure it's placed in /3rd/RxCpp/")
endif()
include_directories(${RXCPP_INCLUDE_DIR})

# nlohmann/json (header-only library)
set(NLOHMANN_JSON_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/3rd/nlohmann/include)
if(NOT EXISTS ${NLOHMANN_JSON_INCLUDE_DIR}/nlohmann/json.hpp)
    # 尝试另一种可能的目录结构
    set(NLOHMANN_JSON_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/3rd/nlohmann)
    if(NOT EXISTS ${NLOHMANN_JSON_INCLUDE_DIR}/json.hpp)
        message(FATAL_ERROR "nlohmann/json not found. Please ensure it's placed in /3rd/nlohmann/")
    endif()
endif()
include_directories(${NLOHMANN_JSON_INCLUDE_DIR})

# libgpg-error library
set(LIBGPG_ERROR_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/3rd/libgpg-error/include)
set(LIBGPG_ERROR_LIB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/lib)
if(NOT EXISTS ${LIBGPG_ERROR_INCLUDE_DIR})
    message(FATAL_ERROR "libgpg-error not found at ${LIBGPG_ERROR_INCLUDE_DIR}")
endif()
include_directories(${LIBGPG_ERROR_INCLUDE_DIR})
link_directories(${LIBGPG_ERROR_LIB_DIR})

# libgcrypt library
set(LIBGCRYPT_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/3rd/libgcrypt/include)
set(LIBGCRYPT_LIB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/lib)
if(NOT EXISTS ${LIBGCRYPT_INCLUDE_DIR})
    message(FATAL_ERROR "libgcrypt not found at ${LIBGCRYPT_INCLUDE_DIR}")
endif()
include_directories(${LIBGCRYPT_INCLUDE_DIR})
link_directories(${LIBGCRYPT_LIB_DIR})

# Boost library removed - not used by main_gateway.cpp or crypto_modified.hpp

# IXWebSocket library
set(USE_TLS ON)
set(USE_OPEN_SSL ON)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/3rd/IXWebSocket)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/3rd/IXWebSocket)

# yyjson library
set(YYJSON_DIR ${CMAKE_CURRENT_SOURCE_DIR}/3rd/yyjson)
add_library(yyjson STATIC
    ${YYJSON_DIR}/src/yyjson.c
)
target_include_directories(yyjson PUBLIC ${YYJSON_DIR}/src)
set_target_properties(yyjson PROPERTIES LINKER_LANGUAGE C)

# libgpg-error library
set(LIBGPG_ERROR_DIR ${CMAKE_CURRENT_SOURCE_DIR}/3rd/libgpg-error)
add_library(gpg-error SHARED IMPORTED)
set_target_properties(gpg-error PROPERTIES
    IMPORTED_LOCATION ${LIBGPG_ERROR_DIR}/src/.libs/libgpg-error.so
)
target_include_directories(gpg-error INTERFACE ${LIBGPG_ERROR_DIR}/src)

# libgcrypt library  
set(LIBGCRYPT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/3rd/libgcrypt)
add_library(gcrypt SHARED IMPORTED)
set_target_properties(gcrypt PROPERTIES
    IMPORTED_LOCATION ${LIBGCRYPT_DIR}/src/.libs/libgcrypt.so
)
target_include_directories(gcrypt INTERFACE ${LIBGCRYPT_DIR}/src)

# 子目录
add_subdirectory(src/shared_memory)
add_subdirectory(src/utils)
add_subdirectory(src/execution)
add_subdirectory(3rd/gateway)
add_subdirectory(crypto)

# 主可执行文件
add_executable(trading_system_gateway main_gateway.cpp)

target_link_libraries(trading_system_gateway
    tes_execution
    tes_shared_memory
    tes_utils
    gateway
    ixwebsocket
    yyjson
    gcrypt
    gpg-error
    pthread
)

# 设置RPATH，使可执行文件在运行时从项目根目录的lib目录加载动态库
set_target_properties(trading_system_gateway PROPERTIES
    INSTALL_RPATH "${CMAKE_CURRENT_SOURCE_DIR}/lib"
    BUILD_WITH_INSTALL_RPATH TRUE
)

# 测试和工具程序已移除编译配置
# add_executable(test_decrypt test_decrypt.cpp)
# target_link_libraries(test_decrypt gcrypt gpg-error)
# add_executable(encrypt_api_keys encrypt_api_keys.cpp)
# target_link_libraries(encrypt_api_keys gcrypt gpg-error)
