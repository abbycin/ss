cmake_minimum_required(VERSION 3.10)
project(signalslot)

enable_testing()

set(TARGET ss)
set(CMAKE_CXX_STANDARD 17)
set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-pmf-conversions")
add_executable(${TARGET} signal_slot.h main.cpp)
target_link_libraries(${TARGET} pthread)

add_test(NAME ${TARGET} COMMAND ${TARGET})