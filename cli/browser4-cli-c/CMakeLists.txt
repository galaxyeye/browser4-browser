cmake_minimum_required(VERSION 3.15)
project(browser4-cli C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Windows-specific: link against WinHTTP and Winsock
if(WIN32)
    set(EXTRA_LIBS ws2_32.lib winhttp.lib)
endif()

add_subdirectory(src)
