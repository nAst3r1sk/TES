# Install script for directory: /home/tr2f/workspace/BM/TES_final/3rd/IXWebSocket

# Set the install prefix
if(NOT DEFINED CMAKE_INSTALL_PREFIX)
  set(CMAKE_INSTALL_PREFIX "/usr/local")
endif()
string(REGEX REPLACE "/$" "" CMAKE_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")

# Set the install configuration name.
if(NOT DEFINED CMAKE_INSTALL_CONFIG_NAME)
  if(BUILD_TYPE)
    string(REGEX REPLACE "^[^A-Za-z0-9_]+" ""
           CMAKE_INSTALL_CONFIG_NAME "${BUILD_TYPE}")
  else()
    set(CMAKE_INSTALL_CONFIG_NAME "")
  endif()
  message(STATUS "Install configuration: \"${CMAKE_INSTALL_CONFIG_NAME}\"")
endif()

# Set the component getting installed.
if(NOT CMAKE_INSTALL_COMPONENT)
  if(COMPONENT)
    message(STATUS "Install component: \"${COMPONENT}\"")
    set(CMAKE_INSTALL_COMPONENT "${COMPONENT}")
  else()
    set(CMAKE_INSTALL_COMPONENT)
  endif()
endif()

# Install shared libraries without execute permission?
if(NOT DEFINED CMAKE_INSTALL_SO_NO_EXE)
  set(CMAKE_INSTALL_SO_NO_EXE "1")
endif()

# Is this installation the result of a crosscompile?
if(NOT DEFINED CMAKE_CROSSCOMPILING)
  set(CMAKE_CROSSCOMPILING "FALSE")
endif()

# Set default install directory permissions.
if(NOT DEFINED CMAKE_OBJDUMP)
  set(CMAKE_OBJDUMP "/usr/bin/objdump")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/home/tr2f/workspace/BM/TES_final/lib/libixwebsocket.a")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/ixwebsocket" TYPE FILE FILES
    "/home/tr2f/workspace/BM/TES_final/3rd/IXWebSocket/ixwebsocket/IXBase64.h"
    "/home/tr2f/workspace/BM/TES_final/3rd/IXWebSocket/ixwebsocket/IXBench.h"
    "/home/tr2f/workspace/BM/TES_final/3rd/IXWebSocket/ixwebsocket/IXCancellationRequest.h"
    "/home/tr2f/workspace/BM/TES_final/3rd/IXWebSocket/ixwebsocket/IXConnectionState.h"
    "/home/tr2f/workspace/BM/TES_final/3rd/IXWebSocket/ixwebsocket/IXDNSLookup.h"
    "/home/tr2f/workspace/BM/TES_final/3rd/IXWebSocket/ixwebsocket/IXExponentialBackoff.h"
    "/home/tr2f/workspace/BM/TES_final/3rd/IXWebSocket/ixwebsocket/IXGetFreePort.h"
    "/home/tr2f/workspace/BM/TES_final/3rd/IXWebSocket/ixwebsocket/IXGzipCodec.h"
    "/home/tr2f/workspace/BM/TES_final/3rd/IXWebSocket/ixwebsocket/IXHttp.h"
    "/home/tr2f/workspace/BM/TES_final/3rd/IXWebSocket/ixwebsocket/IXHttpClient.h"
    "/home/tr2f/workspace/BM/TES_final/3rd/IXWebSocket/ixwebsocket/IXHttpServer.h"
    "/home/tr2f/workspace/BM/TES_final/3rd/IXWebSocket/ixwebsocket/IXNetSystem.h"
    "/home/tr2f/workspace/BM/TES_final/3rd/IXWebSocket/ixwebsocket/IXProgressCallback.h"
    "/home/tr2f/workspace/BM/TES_final/3rd/IXWebSocket/ixwebsocket/IXSelectInterrupt.h"
    "/home/tr2f/workspace/BM/TES_final/3rd/IXWebSocket/ixwebsocket/IXSelectInterruptFactory.h"
    "/home/tr2f/workspace/BM/TES_final/3rd/IXWebSocket/ixwebsocket/IXSelectInterruptPipe.h"
    "/home/tr2f/workspace/BM/TES_final/3rd/IXWebSocket/ixwebsocket/IXSelectInterruptEvent.h"
    "/home/tr2f/workspace/BM/TES_final/3rd/IXWebSocket/ixwebsocket/IXSetThreadName.h"
    "/home/tr2f/workspace/BM/TES_final/3rd/IXWebSocket/ixwebsocket/IXSocket.h"
    "/home/tr2f/workspace/BM/TES_final/3rd/IXWebSocket/ixwebsocket/IXSocketConnect.h"
    "/home/tr2f/workspace/BM/TES_final/3rd/IXWebSocket/ixwebsocket/IXSocketFactory.h"
    "/home/tr2f/workspace/BM/TES_final/3rd/IXWebSocket/ixwebsocket/IXSocketServer.h"
    "/home/tr2f/workspace/BM/TES_final/3rd/IXWebSocket/ixwebsocket/IXSocketTLSOptions.h"
    "/home/tr2f/workspace/BM/TES_final/3rd/IXWebSocket/ixwebsocket/IXStrCaseCompare.h"
    "/home/tr2f/workspace/BM/TES_final/3rd/IXWebSocket/ixwebsocket/IXUdpSocket.h"
    "/home/tr2f/workspace/BM/TES_final/3rd/IXWebSocket/ixwebsocket/IXUniquePtr.h"
    "/home/tr2f/workspace/BM/TES_final/3rd/IXWebSocket/ixwebsocket/IXUrlParser.h"
    "/home/tr2f/workspace/BM/TES_final/3rd/IXWebSocket/ixwebsocket/IXUuid.h"
    "/home/tr2f/workspace/BM/TES_final/3rd/IXWebSocket/ixwebsocket/IXUtf8Validator.h"
    "/home/tr2f/workspace/BM/TES_final/3rd/IXWebSocket/ixwebsocket/IXUserAgent.h"
    "/home/tr2f/workspace/BM/TES_final/3rd/IXWebSocket/ixwebsocket/IXWebSocket.h"
    "/home/tr2f/workspace/BM/TES_final/3rd/IXWebSocket/ixwebsocket/IXWebSocketCloseConstants.h"
    "/home/tr2f/workspace/BM/TES_final/3rd/IXWebSocket/ixwebsocket/IXWebSocketCloseInfo.h"
    "/home/tr2f/workspace/BM/TES_final/3rd/IXWebSocket/ixwebsocket/IXWebSocketErrorInfo.h"
    "/home/tr2f/workspace/BM/TES_final/3rd/IXWebSocket/ixwebsocket/IXWebSocketHandshake.h"
    "/home/tr2f/workspace/BM/TES_final/3rd/IXWebSocket/ixwebsocket/IXWebSocketHandshakeKeyGen.h"
    "/home/tr2f/workspace/BM/TES_final/3rd/IXWebSocket/ixwebsocket/IXWebSocketHttpHeaders.h"
    "/home/tr2f/workspace/BM/TES_final/3rd/IXWebSocket/ixwebsocket/IXWebSocketInitResult.h"
    "/home/tr2f/workspace/BM/TES_final/3rd/IXWebSocket/ixwebsocket/IXWebSocketMessage.h"
    "/home/tr2f/workspace/BM/TES_final/3rd/IXWebSocket/ixwebsocket/IXWebSocketMessageType.h"
    "/home/tr2f/workspace/BM/TES_final/3rd/IXWebSocket/ixwebsocket/IXWebSocketOpenInfo.h"
    "/home/tr2f/workspace/BM/TES_final/3rd/IXWebSocket/ixwebsocket/IXWebSocketPerMessageDeflate.h"
    "/home/tr2f/workspace/BM/TES_final/3rd/IXWebSocket/ixwebsocket/IXWebSocketPerMessageDeflateCodec.h"
    "/home/tr2f/workspace/BM/TES_final/3rd/IXWebSocket/ixwebsocket/IXWebSocketPerMessageDeflateOptions.h"
    "/home/tr2f/workspace/BM/TES_final/3rd/IXWebSocket/ixwebsocket/IXWebSocketProxyServer.h"
    "/home/tr2f/workspace/BM/TES_final/3rd/IXWebSocket/ixwebsocket/IXWebSocketSendData.h"
    "/home/tr2f/workspace/BM/TES_final/3rd/IXWebSocket/ixwebsocket/IXWebSocketSendInfo.h"
    "/home/tr2f/workspace/BM/TES_final/3rd/IXWebSocket/ixwebsocket/IXWebSocketServer.h"
    "/home/tr2f/workspace/BM/TES_final/3rd/IXWebSocket/ixwebsocket/IXWebSocketTransport.h"
    "/home/tr2f/workspace/BM/TES_final/3rd/IXWebSocket/ixwebsocket/IXWebSocketVersion.h"
    "/home/tr2f/workspace/BM/TES_final/3rd/IXWebSocket/ixwebsocket/IXSocketOpenSSL.h"
    )
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/cmake/ixwebsocket" TYPE FILE FILES "/home/tr2f/workspace/BM/TES_final/build/3rd/IXWebSocket/ixwebsocket-config.cmake")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/pkgconfig" TYPE FILE FILES "/home/tr2f/workspace/BM/TES_final/build/3rd/IXWebSocket/ixwebsocket.pc")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/ixwebsocket/ixwebsocket-targets.cmake")
    file(DIFFERENT _cmake_export_file_changed FILES
         "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/ixwebsocket/ixwebsocket-targets.cmake"
         "/home/tr2f/workspace/BM/TES_final/build/3rd/IXWebSocket/CMakeFiles/Export/dbc99e06a99e696141dafd40631f8060/ixwebsocket-targets.cmake")
    if(_cmake_export_file_changed)
      file(GLOB _cmake_old_config_files "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/ixwebsocket/ixwebsocket-targets-*.cmake")
      if(_cmake_old_config_files)
        string(REPLACE ";" ", " _cmake_old_config_files_text "${_cmake_old_config_files}")
        message(STATUS "Old export file \"$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/ixwebsocket/ixwebsocket-targets.cmake\" will be replaced.  Removing files [${_cmake_old_config_files_text}].")
        unset(_cmake_old_config_files_text)
        file(REMOVE ${_cmake_old_config_files})
      endif()
      unset(_cmake_old_config_files)
    endif()
    unset(_cmake_export_file_changed)
  endif()
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/cmake/ixwebsocket" TYPE FILE FILES "/home/tr2f/workspace/BM/TES_final/build/3rd/IXWebSocket/CMakeFiles/Export/dbc99e06a99e696141dafd40631f8060/ixwebsocket-targets.cmake")
  if(CMAKE_INSTALL_CONFIG_NAME MATCHES "^()$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/cmake/ixwebsocket" TYPE FILE FILES "/home/tr2f/workspace/BM/TES_final/build/3rd/IXWebSocket/CMakeFiles/Export/dbc99e06a99e696141dafd40631f8060/ixwebsocket-targets-noconfig.cmake")
  endif()
endif()

