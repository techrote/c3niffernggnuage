cmake_minimum_required(VERSION 3.16)
project(nggunage_host_tests LANGUAGES C)

enable_testing()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(NGN_CORE_DIR
    "${CMAKE_CURRENT_LIST_DIR}/../../firmware/c3/components/ngn_core"
)

add_library(ngn_core_host STATIC
    "${NGN_CORE_DIR}/ngn_node.c"
    "${NGN_CORE_DIR}/ngn_ble.c"
)

target_include_directories(ngn_core_host PUBLIC
    "${NGN_CORE_DIR}/include"
)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(ngn_core_host PRIVATE -Wall -Wextra -Werror -pedantic)
endif()

add_executable(test_ngn_node
    test_ngn_node.c
)

target_link_libraries(test_ngn_node PRIVATE ngn_core_host)

add_executable(test_ngn_ble
    test_ngn_ble.c
)

target_link_libraries(test_ngn_ble PRIVATE ngn_core_host)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(test_ngn_node PRIVATE -Wall -Wextra -Werror -pedantic)
    target_compile_options(test_ngn_ble PRIVATE -Wall -Wextra -Werror -pedantic)
endif()

add_test(NAME ngn_node COMMAND test_ngn_node)
add_test(NAME ngn_ble COMMAND test_ngn_ble)
