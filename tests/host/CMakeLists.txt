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
    "${NGN_CORE_DIR}/ngn_protocol.c"
    "${NGN_CORE_DIR}/ngn_schedule.c"
    "${NGN_CORE_DIR}/ngn_radio.c"
    "${NGN_CORE_DIR}/ngn_csi.c"
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

foreach(NGN_SUITE protocol schedule radio radio_adversarial csi)
    add_executable(test_ngn_${NGN_SUITE} test_ngn_${NGN_SUITE}.c)
    target_link_libraries(test_ngn_${NGN_SUITE} PRIVATE ngn_core_host)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(test_ngn_${NGN_SUITE} PRIVATE
            -Wall -Wextra -Werror -pedantic)
    endif()
    add_test(NAME ngn_${NGN_SUITE} COMMAND test_ngn_${NGN_SUITE})
endforeach()

set(NGN_DISPLAY_DIR
    "${CMAKE_CURRENT_LIST_DIR}/../../firmware/c3/components/ngn_display"
)

add_library(ngn_display_host STATIC
    "${NGN_DISPLAY_DIR}/ngn_framebuffer.c"
    "${NGN_DISPLAY_DIR}/ngn_renderer.c"
)

target_include_directories(ngn_display_host PUBLIC
    "${NGN_DISPLAY_DIR}/include"
)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(ngn_display_host PRIVATE -Wall -Wextra -Werror -pedantic)
endif()

add_executable(test_ngn_display
    test_ngn_display.c
)

target_link_libraries(test_ngn_display PRIVATE ngn_display_host)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(test_ngn_display PRIVATE -Wall -Wextra -Werror -pedantic)
endif()

add_test(NAME ngn_display COMMAND test_ngn_display)
