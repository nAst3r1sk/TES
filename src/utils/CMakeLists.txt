# Utils库
add_library(tes_utils STATIC
    websocket_client.cpp
)

# 链接IXWebSocket库
target_link_libraries(tes_utils
    ixwebsocket
    OpenSSL::SSL
    OpenSSL::Crypto
    Threads::Threads
)

# 设置包含目录
target_include_directories(tes_utils PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../3rd/IXWebSocket
)