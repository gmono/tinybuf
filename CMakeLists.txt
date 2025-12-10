cmake_minimum_required(VERSION 3.5)
project(tinybuf)

#使能c++11
#add_compile_options(-std=c++11)
set(CMAKE_CXX_STANDARD 23)
if(NOT MSVC)
    add_definitions(-Wno-int-to-void-pointer-cast -Wno-int-to-pointer-cast -Wno-pointer-to-int-cast)
endif()


#加载自定义模块
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
#设置库文件路径
set(LIBRARY_OUTPUT_PATH ${PROJECT_BINARY_DIR}/lib)
#设置可执行程序路径
set(EXECUTABLE_OUTPUT_PATH ${PROJECT_BINARY_DIR}/bin)

#设置工程源码根目录
set(Source_Root ${CMAKE_CURRENT_SOURCE_DIR}/src)

#设置头文件目录
INCLUDE_DIRECTORIES(${Source_Root}/include)
INCLUDE_DIRECTORIES(${CMAKE_CURRENT_SOURCE_DIR}/3rdpart/)


#收集源代码
file(GLOB tinybuf_src_Root ${Source_Root}/source/*.c)
file(GLOB json_src_Root ${CMAKE_CURRENT_SOURCE_DIR}/3rdpart/jsoncpp/*.cpp)

add_library(tinybuf STATIC ${tinybuf_src_Root})
add_library(jsoncpp STATIC ${json_src_Root})

add_executable(benchmark benchmark.cpp)
add_executable(sys_tests tests/system_extend_tests.cpp)
add_executable(unit_tests tests/unit/system_extend_plugin_tests.cpp)

add_library(upper_plugin SHARED ${Source_Root}/../src/plugins/upper_plugin.c)
add_library(system_extend SHARED ${Source_Root}/../src/plugins/system_extend.c)

set(LINK_LIST)
find_package(JEMALLOC QUIET)
if(JEMALLOC_FOUND)
    set(LINK_LIST ${JEMALLOC_LIBRARY})
endif()


if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "c:/vcpkg/scripts/buildsystems/vcpkg.cmake")
endif()
find_package(Catch2 CONFIG QUIET)


if(MSVC)
    target_link_libraries(benchmark jsoncpp tinybuf ${LINK_LIST} ws2_32)
    target_link_libraries(sys_tests jsoncpp tinybuf ${LINK_LIST} ws2_32)
    target_link_libraries(unit_tests jsoncpp tinybuf Catch2::Catch2WithMain ws2_32)
    target_link_libraries(upper_plugin tinybuf ws2_32)
    target_link_libraries(system_extend tinybuf ws2_32)
else()
    target_link_libraries(benchmark jsoncpp tinybuf ${LINK_LIST} pthread)
    target_link_libraries(sys_tests jsoncpp tinybuf ${LINK_LIST} pthread)
    target_link_libraries(unit_tests jsoncpp tinybuf Catch2::Catch2WithMain pthread)
    target_link_libraries(upper_plugin tinybuf pthread)
    target_link_libraries(system_extend tinybuf pthread)
endif()

add_custom_command(TARGET system_extend POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:benchmark>/../tinybuf_plugins"
    COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:system_extend>" "$<TARGET_FILE_DIR:benchmark>/../tinybuf_plugins/"
)






