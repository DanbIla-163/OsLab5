cmake_minimum_required(VERSION 3.30)
project(OsLabs5)

set(CMAKE_C_COMPILER "gcc")
set(CMAKE_CXX_COMPILER "g++")

# Настройки компилятора
set(CMAKE_CXX_STANDARD 20)
add_compile_options(-Wall -Wextra -pedantic)

# Общие определения для Windows
if(WIN32)
    add_definitions(-D_WIN32_WINNT=0x0601 -DWIN32_LEAN_AND_MEAN)
endif()

# Сервер
add_executable(server server.cpp)
if(WIN32)
    target_link_libraries(server ws2_32)
endif()

# Клиент
add_executable(client client.cpp)
if(WIN32)
    target_link_libraries(client ws2_32)
endif()
