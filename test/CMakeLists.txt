# PC tests for the firmware logic.
#
# These build with an ordinary PC compiler (not the ARM one) and run on the
# PC. They cover the parts of the firmware that are pure logic - scaling,
# fault codes, plausibility checks, calibration, debug text - with the
# hardware replaced by the fakes in sim.c.
#
#   cmake -S test -B build-test -G Ninja
#   cmake --build build-test
#   ctest --test-dir build-test --output-on-failure

cmake_minimum_required(VERSION 3.20)
project(diu-tests C)

enable_testing()

set(SRC ${CMAKE_CURRENT_LIST_DIR}/../src)
set(INC ${CMAKE_CURRENT_LIST_DIR}/../include)

function(diu_test name)
    add_executable(${name} ${name}.c sim.c ${ARGN})
    target_include_directories(${name} PRIVATE ${INC} ${CMAKE_CURRENT_LIST_DIR})
    set_target_properties(${name} PROPERTIES C_STANDARD 11 C_EXTENSIONS ON)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

diu_test(test_safety      ${SRC}/sensors.c ${SRC}/safety.c)
diu_test(test_calibration ${SRC}/sensors.c ${SRC}/safety.c ${SRC}/calibration.c)
diu_test(test_debug       ${SRC}/debug.c)
