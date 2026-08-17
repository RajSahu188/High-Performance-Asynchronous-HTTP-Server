cmake_minimum_required(VERSION 3.10)
project(EpollServer CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(server main.cpp)
target_link_libraries(server PRIVATE pthread)