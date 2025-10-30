# 设置输出目录到项目根目录的bin文件夹
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)

# 使用项目中的Boost库
set(BOOST_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../3rd/boost")
set(Boost_INCLUDE_DIR "${BOOST_ROOT}/include")
set(Boost_LIBRARY_DIR "${BOOST_ROOT}/lib")

# 创建可执行文件
add_executable(crypto_test main.cpp)
target_link_libraries(crypto_test gcrypt gpg-error)

# 添加原始加解密测试程序
#add_executable(test_original_crypto test_original_crypto.cpp)
#target_link_libraries(test_original_crypto gcrypt gpg-error fmt)
#target_include_directories(test_original_crypto PRIVATE ${Boost_INCLUDE_DIR})

# 添加test_binance_uppercase可执行文件
#add_executable(test_binance_uppercase test_binance_uppercase.cpp)
#target_link_libraries(test_binance_uppercase gcrypt gpg-error fmt)
#target_include_directories(test_binance_uppercase PRIVATE ${Boost_INCLUDE_DIR})

# 添加测试程序
#add_executable(test_simple_comparison test_simple_comparison.cpp)
#target_link_libraries(test_simple_comparison gcrypt gpg-error fmt)

# 添加原始BlowFish测试程序
#add_executable(test_original_blowfish test_original_blowfish.cpp)
#target_link_libraries(test_original_blowfish gcrypt gpg-error fmt)

# 添加修改后的crypto测试程序
#add_executable(test_modified_crypto test_modified_crypto.cpp)
#target_link_libraries(test_modified_crypto gcrypt gpg-error fmt)

# 添加最终对比测试程序
#add_executable(test_final_comparison test_final_comparison.cpp)
#target_link_libraries(test_final_comparison gcrypt gpg-error fmt)