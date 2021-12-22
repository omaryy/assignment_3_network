cmake_minimum_required(VERSION 3.16)
project(client)

set(CMAKE_CXX_STANDARD 14)
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
add_executable(client main.cpp)
target_link_libraries(client Threads::Threads)
