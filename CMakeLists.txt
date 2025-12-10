cmake_minimum_required(VERSION 3.10)
project(tinybuf)


if(NOT DEFINED CMAKE_TOOLCHAIN_FILE AND DEFINED ENV{VCPKG_ROOT})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
endif()
# find_package(Catch2 CONFIG REQUIRED)

if(DEFINED ENV{VCPKG_ROOT})
    if(DEFINED ENV{VCPKG_DEFAULT_TRIPLET})
        list(APPEND CMAKE_PREFIX_PATH "$ENV{VCPKG_ROOT}/installed/$ENV{VCPKG_DEFAULT_TRIPLET}")
    else()
        list(APPEND CMAKE_PREFIX_PATH "$ENV{VCPKG_ROOT}/installed/x64-windows")
    endif()
endif()
find_package(Catch2 REQUIRED)


#使能c++11
#add_compile_options(-std=c++11)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)


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
add_executable(all_tests tests/system_extend_tests.cpp tests/unit/system_extend_plugin_tests.cpp benchmark.cpp)

add_library(upper_plugin SHARED ${Source_Root}/../src/plugins/upper_plugin.c)
add_library(system_extend SHARED ${Source_Root}/../src/plugins/system_extend.c)

set(LINK_LIST)
find_package(JEMALLOC QUIET)
if(JEMALLOC_FOUND)
    set(LINK_LIST ${JEMALLOC_LIBRARY})
endif()




if(MSVC)
    target_link_libraries(benchmark jsoncpp tinybuf Catch2::Catch2WithMain ${LINK_LIST} ws2_32)
    target_link_libraries(sys_tests jsoncpp tinybuf Catch2::Catch2WithMain ${LINK_LIST} ws2_32)
    target_link_libraries(unit_tests jsoncpp tinybuf Catch2::Catch2WithMain ws2_32)
    target_link_libraries(all_tests jsoncpp tinybuf Catch2::Catch2WithMain ${LINK_LIST} ws2_32)
    target_link_libraries(upper_plugin tinybuf ws2_32)
    target_link_libraries(system_extend tinybuf ws2_32)
else()
    target_link_libraries(benchmark jsoncpp tinybuf Catch2::Catch2WithMain ${LINK_LIST} pthread)
    target_link_libraries(sys_tests jsoncpp tinybuf Catch2::Catch2WithMain ${LINK_LIST} pthread)
target_link_libraries(unit_tests jsoncpp tinybuf Catch2::Catch2WithMain pthread)
    target_link_libraries(all_tests jsoncpp tinybuf Catch2::Catch2WithMain ${LINK_LIST} pthread)
    target_link_libraries(upper_plugin tinybuf pthread)
    target_link_libraries(system_extend tinybuf pthread)
endif()

add_custom_command(TARGET system_extend POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:benchmark>/../tinybuf_plugins"
    COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:system_extend>" "$<TARGET_FILE_DIR:benchmark>/../tinybuf_plugins/"
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:all_tests>/../tinybuf_plugins"
    COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:system_extend>" "$<TARGET_FILE_DIR:all_tests>/../tinybuf_plugins/"
)

enable_testing()
add_test(NAME unit_tests COMMAND $<TARGET_FILE:unit_tests>)
set_tests_properties(unit_tests PROPERTIES TIMEOUT 8 LABELS "unit")
add_test(NAME sys_tests COMMAND $<TARGET_FILE:sys_tests>)
set_tests_properties(sys_tests PROPERTIES TIMEOUT 8 LABELS "system")

# 注册 benchmark 测试为 CTest，但默认禁用，仅编译不运行
add_test(NAME benchmark COMMAND $<TARGET_FILE:benchmark>)
set_tests_properties(benchmark PROPERTIES LABELS "benchmark;performance" DISABLED TRUE TIMEOUT 120)

# 汇总所有测试一次性运行
add_test(NAME all_tests COMMAND $<TARGET_FILE:all_tests>)
set_tests_properties(all_tests PROPERTIES TIMEOUT 120 LABELS "all")






