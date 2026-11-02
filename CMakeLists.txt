cmake_minimum_required(VERSION 3.20)

project(ExchangeCore VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(EXCHANGECORE_LEGACY_PREFLIGHT
    "Use v0.4 quantity preflight for controlled performance comparisons" OFF)

option(EXCHANGECORE_LEGACY_CANCEL
    "Use v0.7 price-level lookup during cancellation for A/B comparison" OFF)

add_library(exchangecore_core STATIC
    src/order_book.cpp
)

if(EXCHANGECORE_LEGACY_PREFLIGHT)
    target_compile_definitions(exchangecore_core PUBLIC EXCHANGECORE_LEGACY_PREFLIGHT=1)
endif()

if(EXCHANGECORE_LEGACY_CANCEL)
    target_compile_definitions(exchangecore_core PUBLIC EXCHANGECORE_LEGACY_CANCEL=1)
endif()

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

# Dedicated benchmark executable: not mixed with the application demo or unit tests.
add_executable(exchangecore_bench benchmarks/matching_bench.cpp)
target_link_libraries(exchangecore_bench PRIVATE exchangecore_core)
if(MSVC)
    target_compile_options(exchangecore_bench PRIVATE /W4 /permissive-)
else()
    target_compile_options(exchangecore_bench PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(BUILD_TESTING)
    foreach(benchmark_workload IN ITEMS passive crossing mixed cancel-heavy)
        add_test(NAME benchmark_smoke_${benchmark_workload}
            COMMAND exchangecore_bench --orders 1000 --repeats 1 --workload ${benchmark_workload} --seed 31)
    endforeach()
endif()

# Profiling smoke test verifies that interleaved configurations agree on replay outcomes.
if(BUILD_TESTING)
    add_test(NAME profile_smoke_grid
        COMMAND exchangecore_bench --orders 2000 --workload mixed --seed 31
                --profile-grid auto --profile-rounds 2)
endif()

# Offline market-data integrity tests (optional Python interpreter).
find_package(Python3 COMPONENTS Interpreter QUIET)
if(BUILD_TESTING AND Python3_Interpreter_FOUND)
    add_test(NAME market_data_offline_tests
        COMMAND ${Python3_EXECUTABLE} -m unittest discover -s market_data/tests -v)
    set_tests_properties(market_data_offline_tests PROPERTIES WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
endif()
