cmake_minimum_required(VERSION 3.20)

project(ExchangeCore VERSION 0.3 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(exchangecore_core STATIC
    src/order_book.cpp
)

target_include_directories(exchangecore_core PUBLIC
    ${PROJECT_SOURCE_DIR}/include
)

if(MSVC)
    target_compile_options(exchangecore_core PRIVATE /W4 /permissive-)
else()
    target_compile_options(exchangecore_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(exchangecore src/main.cpp)
target_link_libraries(exchangecore PRIVATE exchangecore_core)

include(CTest)
if(BUILD_TESTING)
    add_executable(exchangecore_tests tests/order_book_tests.cpp)
    target_link_libraries(exchangecore_tests PRIVATE exchangecore_core)
    add_test(NAME order_book_tests COMMAND exchangecore_tests)
endif()
