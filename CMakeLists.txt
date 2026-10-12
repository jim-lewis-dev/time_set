cmake_minimum_required(VERSION 3.20)
project(time_set LANGUAGES CXX)

option(TIME_SET_BUILD_TESTS "Build tests and examples" ON)
option(TIME_SET_BUILD_PYTHON "Build the native Python interface" OFF)
option(TIME_SET_SANITIZERS "Enable address and undefined-behavior sanitizers" OFF)
option(TIME_SET_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)

add_library(time_set_core INTERFACE)
add_library(time_set::core ALIAS time_set_core)
target_compile_features(time_set_core INTERFACE cxx_std_20)
target_include_directories(time_set_core INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
set_target_properties(time_set_core PROPERTIES EXPORT_NAME core)

function(time_set_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4)
        if(TIME_SET_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
        if(TIME_SET_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
        if(TIME_SET_SANITIZERS)
            target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
            target_link_options(${target} PRIVATE -fsanitize=address,undefined)
        endif()
    endif()
endfunction()

add_library(time_set_io STATIC src/time_set_io.cpp)
add_library(time_set::io ALIAS time_set_io)
target_link_libraries(time_set_io PUBLIC time_set_core)
set_target_properties(time_set_io PROPERTIES POSITION_INDEPENDENT_CODE ON EXPORT_NAME io)
time_set_warnings(time_set_io)

add_library(time_set_cli STATIC src/cli.cpp)
target_link_libraries(time_set_cli PUBLIC time_set_io)
set_target_properties(time_set_cli PROPERTIES POSITION_INDEPENDENT_CODE ON)
time_set_warnings(time_set_cli)
add_executable(time_set src/cli_main.cpp)
target_link_libraries(time_set PRIVATE time_set_cli)
time_set_warnings(time_set)

if(TIME_SET_BUILD_PYTHON)
    find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter Development.Module)
    Python3_add_library(_native MODULE WITH_SOABI src/python_native.cpp)
    target_link_libraries(_native PRIVATE time_set_cli)
    set_target_properties(_native PROPERTIES LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/python/time_set")
    time_set_warnings(_native)
    foreach(file __init__.py __main__.py py.typed)
        configure_file("python/time_set/${file}" "${CMAKE_BINARY_DIR}/python/time_set/${file}" COPYONLY)
    endforeach()
endif()

if(TIME_SET_BUILD_TESTS)
    enable_testing()
    foreach(name core io)
        add_executable(test_${name} tests/test_${name}.cpp)
        target_link_libraries(test_${name} PRIVATE time_set_io)
        time_set_warnings(test_${name})
        add_test(NAME ${name} COMMAND test_${name})
    endforeach()
    foreach(name algebra availability sectors precision)
        add_executable(example_${name} examples/${name}.cpp)
        target_link_libraries(example_${name} PRIVATE time_set_io)
        time_set_warnings(example_${name})
        add_test(NAME example_cpp_${name} COMMAND example_${name})
    endforeach()
    find_package(Python3 3.10 QUIET COMPONENTS Interpreter)
    if(Python3_Interpreter_FOUND)
        add_test(NAME documentation COMMAND ${CMAKE_COMMAND} -E env
            "TIME_SET_CLI=$<TARGET_FILE:time_set>" "TIME_SET_CXX=${CMAKE_CXX_COMPILER}"
            "TIME_SET_HAS_PYTHON=${TIME_SET_BUILD_PYTHON}" "PYTHONPATH=${CMAKE_BINARY_DIR}/python"
            ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_docs.py)
        add_test(NAME cli COMMAND ${CMAKE_COMMAND} -E env
            "TIME_SET_CLI=$<TARGET_FILE:time_set>" ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_cli.py)
        if(TIME_SET_BUILD_PYTHON)
            add_test(NAME python COMMAND ${CMAKE_COMMAND} -E env
                "PYTHONPATH=${CMAKE_BINARY_DIR}/python" ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_python.py)
            foreach(name availability sectors precision routines)
                add_test(NAME example_python_${name} COMMAND ${CMAKE_COMMAND} -E env
                    "PYTHONPATH=${CMAKE_BINARY_DIR}/python" ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/examples/${name}.py)
            endforeach()
            add_test(NAME python_cli COMMAND ${CMAKE_COMMAND} -E env
                "PYTHONPATH=${CMAKE_BINARY_DIR}/python" "TIME_SET_PYTHON_CLI=1"
                ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_cli.py)
        endif()
    endif()
    if(UNIX)
        add_test(NAME example_shell COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/examples/pipeline.sh $<TARGET_FILE:time_set>)
    endif()
endif()

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
install(TARGETS time_set_core time_set_io EXPORT time_setTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(TARGETS time_set RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT time_setTargets FILE time_setTargets.cmake NAMESPACE time_set::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/time_set)
configure_package_config_file(cmake/time_setConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/time_setConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/time_set)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/time_setConfig.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/time_set)
