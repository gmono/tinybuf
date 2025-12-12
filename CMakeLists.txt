cmake_minimum_required(VERSION 3.10)
project(tinybuf)

option(TINYBUF_BUILD_TESTS "Build tests and benchmarks" ON)


set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)


if(NOT DEFINED CMAKE_TOOLCHAIN_FILE AND DEFINED ENV{VCPKG_ROOT})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
endif()
# find_package(Catch2 CONFIG REQUIRED)

if(DEFINED ENV{VCPKG_ROOT})
    if(DEFINED ENV{VCPKG_DEFAULT_TRIPLET})
        list(APPEND CMAKE_PREFIX_PATH "$ENV{VCPKG_ROOT}/installed/$ENV{VCPKG_DEFAULT_TRIPLET}")
    endif()
endif()
include(FetchContent)
if(TINYBUF_BUILD_TESTS)
    find_package(Catch2 QUIET)
    if(NOT Catch2_FOUND)
        FetchContent_Declare(
            Catch2
            URL https://github.com/catchorg/Catch2/archive/refs/tags/v3.5.0.zip
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        )
        FetchContent_MakeAvailable(Catch2)
    endif()
endif()

if(WIN32 AND NOT MSVC)
    message(FATAL_ERROR "Windows仅支持MSVC编译")
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
INCLUDE_DIRECTORIES(${CMAKE_CURRENT_SOURCE_DIR}/3rdpart/dyncall-1.4)
INCLUDE_DIRECTORIES(${CMAKE_CURRENT_SOURCE_DIR}/3rdpart/dyncall-1.4/dyncall)
INCLUDE_DIRECTORIES(${CMAKE_CURRENT_SOURCE_DIR}/3rdpart/dyncall-1.4/dyncallback)
INCLUDE_DIRECTORIES(${CMAKE_CURRENT_SOURCE_DIR}/3rdpart/dyncall-1.4/dynload)
INCLUDE_DIRECTORIES(${CMAKE_CURRENT_SOURCE_DIR}/src/dyn_sys)
INCLUDE_DIRECTORIES(${CMAKE_CURRENT_SOURCE_DIR}/src/static_oop)
INCLUDE_DIRECTORIES(${CMAKE_CURRENT_SOURCE_DIR}/src/tb_lock)
INCLUDE_DIRECTORIES(${CMAKE_CURRENT_SOURCE_DIR}/3rdpart/)
INCLUDE_DIRECTORIES(${CMAKE_CURRENT_SOURCE_DIR}/3rdpart/klib)


#收集源代码
file(GLOB tinybuf_src_Root ${Source_Root}/source/*.c)
file(GLOB system_plugins_src ${Source_Root}/system_plugins/*.c)
file(GLOB json_src_Root ${CMAKE_CURRENT_SOURCE_DIR}/3rdpart/jsoncpp/*.cpp)

add_library(tinybuf STATIC ${tinybuf_src_Root} ${system_plugins_src})
add_library(dyn_sys STATIC src/dyn_sys/dyn_sys.c)
target_link_libraries(tinybuf PUBLIC dyn_sys)
add_library(jsoncpp STATIC ${json_src_Root})
set(DYNCALL_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/3rdpart/dyncall-1.4)
if(EXISTS ${DYNCALL_ROOT}/CMakeLists.txt)
    add_subdirectory(${DYNCALL_ROOT} ${CMAKE_CURRENT_BINARY_DIR}/3rdpart/dyncall)
    target_link_libraries(tinybuf PUBLIC dyncall_s dyncallback_s dynload_s)
endif()
set(DYNCALL_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/3rdpart/dyncall-1.4)
if(EXISTS ${DYNCALL_ROOT}/dyncall/dyncall.h)
    message(STATUS "Using local dyncall at ${DYNCALL_ROOT}")
else()
    message(WARNING "dyncall not found under 3rdpart/dyncall-1.4; headers configured, library not built")
endif()

if(TINYBUF_BUILD_TESTS)
    add_executable(benchmark benchmark.cpp tests/catch_test_logger.cpp)
    add_executable(sys_tests tests/system_extend_tests.cpp tests/catch_test_logger.cpp)
    add_executable(unit_tests tests/unit/system_extend_plugin_tests.cpp tests/catch_test_logger.cpp)
    add_executable(all_test tests/system_extend_tests.cpp tests/unit/system_extend_plugin_tests.cpp tests/unit/hole_string_tests.cpp tests/catch_test_logger.cpp)

    if(WIN32)
        add_library(upper_plugin SHARED ${Source_Root}/../src/plugins/upper_plugin.c)
    endif()
    add_library(system_extend SHARED ${Source_Root}/../src/plugins/system_extend.c)
endif()

set(LINK_LIST)
find_package(JEMALLOC QUIET)
if(JEMALLOC_FOUND)
    set(LINK_LIST ${JEMALLOC_LIBRARY})
endif()




if(MSVC)
    target_compile_options(tinybuf PRIVATE /W3 /permissive- /EHsc)
    target_compile_definitions(tinybuf PRIVATE _CRT_SECURE_NO_WARNINGS _SCL_SECURE_NO_WARNINGS)
    if(TINYBUF_BUILD_TESTS)
        target_link_libraries(benchmark jsoncpp tinybuf Catch2::Catch2WithMain ${LINK_LIST} ws2_32)
        target_link_libraries(sys_tests jsoncpp tinybuf Catch2::Catch2WithMain ${LINK_LIST} ws2_32)
        target_link_libraries(unit_tests jsoncpp tinybuf Catch2::Catch2WithMain ws2_32)
        target_link_libraries(all_test jsoncpp tinybuf Catch2::Catch2WithMain ${LINK_LIST} ws2_32)
        target_compile_definitions(sys_tests PRIVATE DISABLE_DATAFRAME_TESTS)
        target_compile_definitions(all_test PRIVATE DISABLE_DATAFRAME_TESTS DISABLE_INDEXED_TENSOR_TEST)
        if(WIN32)
            target_link_libraries(upper_plugin tinybuf dyn_sys ws2_32)
        endif()
        target_link_libraries(system_extend tinybuf dyn_sys ws2_32)
    endif()
else()
    target_compile_options(tinybuf PRIVATE -Wall -Wno-format-truncation -Wno-stringop-overflow -Wno-discarded-qualifiers -Wno-nonnull)
    set(EXTRA_LIBS)
    if(UNIX)
        list(APPEND EXTRA_LIBS pthread dl)
    elseif(MINGW)
        list(APPEND EXTRA_LIBS ws2_32)
    endif()
    if(TINYBUF_BUILD_TESTS)
        target_link_libraries(benchmark jsoncpp tinybuf Catch2::Catch2WithMain ${LINK_LIST} ${EXTRA_LIBS})
        target_link_libraries(sys_tests jsoncpp tinybuf Catch2::Catch2WithMain ${LINK_LIST} ${EXTRA_LIBS})
        target_link_libraries(unit_tests jsoncpp tinybuf Catch2::Catch2WithMain ${EXTRA_LIBS})
        target_link_libraries(all_test jsoncpp tinybuf Catch2::Catch2WithMain ${LINK_LIST} ${EXTRA_LIBS})
        target_compile_definitions(sys_tests PRIVATE DISABLE_DATAFRAME_TESTS)
        target_compile_definitions(all_test PRIVATE DISABLE_DATAFRAME_TESTS DISABLE_INDEXED_TENSOR_TEST)
        if(WIN32)
            target_link_libraries(upper_plugin tinybuf dyn_sys ${EXTRA_LIBS})
        endif()
        target_link_libraries(system_extend tinybuf dyn_sys ${EXTRA_LIBS})
    endif()
endif()

if(TINYBUF_BUILD_TESTS)
    add_custom_command(TARGET system_extend POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:benchmark>/../tinybuf_plugins"
        COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:system_extend>" "$<TARGET_FILE_DIR:benchmark>/../tinybuf_plugins/$<TARGET_FILE_NAME:system_extend>"
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:all_test>/../tinybuf_plugins"
        COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:system_extend>" "$<TARGET_FILE_DIR:all_test>/../tinybuf_plugins/$<TARGET_FILE_NAME:system_extend>"
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:sys_tests>/../tinybuf_plugins"
        COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:system_extend>" "$<TARGET_FILE_DIR:sys_tests>/../tinybuf_plugins/$<TARGET_FILE_NAME:system_extend>"
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:unit_tests>/../tinybuf_plugins"
        COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:system_extend>" "$<TARGET_FILE_DIR:unit_tests>/../tinybuf_plugins/$<TARGET_FILE_NAME:system_extend>"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_SOURCE_DIR}/tinybuf_plugins"
        COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:system_extend>" "${CMAKE_CURRENT_SOURCE_DIR}/tinybuf_plugins/$<TARGET_FILE_NAME:system_extend>"
    )
endif()

if(TINYBUF_BUILD_TESTS)
    enable_testing()
    add_test(NAME unit_tests COMMAND $<TARGET_FILE:unit_tests>)
    set_tests_properties(unit_tests PROPERTIES TIMEOUT 8 LABELS "unit")
    add_test(NAME sys_tests COMMAND $<TARGET_FILE:sys_tests>)
    set_tests_properties(sys_tests PROPERTIES TIMEOUT 8 LABELS "system")

    # 注册 benchmark 测试为 CTest，但默认禁用，仅编译不运行
    add_test(NAME benchmark COMMAND $<TARGET_FILE:benchmark>)
    set_tests_properties(benchmark PROPERTIES LABELS "benchmark;performance" DISABLED TRUE TIMEOUT 120)

    # 汇总所有测试一次性运行
    add_test(NAME all_test COMMAND $<TARGET_FILE:all_test> "~[benchmark]")
    set_tests_properties(all_test PROPERTIES TIMEOUT 180 LABELS "all")
endif()






