cmake_minimum_required(VERSION 3.16)
project(trailsense_triangulate LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Parity depends on standard IEEE-754 double semantics — never -ffast-math.
add_compile_options(-Wall -Wextra -fno-fast-math)

set(TS_TRI_CORE_SOURCES
  src/pathloss.c
  src/enu.c
  src/multilaterate.c
  src/correlate.c
  src/solve.c
  src/triangulate.c
)

# Static + shared, both C-ABI, same sources. libm for pow/cos/hypot/round/sqrt.
add_library(trailsense_triangulate STATIC ${TS_TRI_CORE_SOURCES})
target_include_directories(trailsense_triangulate PUBLIC include PRIVATE src)
target_link_libraries(trailsense_triangulate PUBLIC m)

add_library(trailsense_triangulate_shared SHARED ${TS_TRI_CORE_SOURCES})
set_target_properties(trailsense_triangulate_shared PROPERTIES OUTPUT_NAME trailsense_triangulate)
target_include_directories(trailsense_triangulate_shared PUBLIC include PRIVATE src)
target_link_libraries(trailsense_triangulate_shared PUBLIC m)

# CLI: JSON stdin -> positions JSON stdout.
add_executable(trailsense-triangulate cli/main.c)
target_link_libraries(trailsense-triangulate PRIVATE trailsense_triangulate)

# Parity test suite (the acceptance gate).
enable_testing()
add_executable(parity_test test/parity_test.c)
target_link_libraries(parity_test PRIVATE trailsense_triangulate)
# The harness includes the private internal.h contracts and the generated
# fixtures.gen.h (committed alongside the test). PRIVATE include on the lib does
# not propagate, so add both dirs here.
target_include_directories(parity_test PRIVATE src test)

# Per-group ctests (each runs one group in isolation) + an all-groups run.
# WORKING_DIRECTORY is the test dir for consistency; the harness consumes the
# compiled-in fixtures.gen.h, so no runtime fixture path is required.
set(TS_PARITY_GROUPS pathloss enu compute_weight multilaterate bilaterate ellipse correlate solve triangulate)
foreach(grp IN LISTS TS_PARITY_GROUPS)
  add_test(NAME parity_${grp}
           COMMAND parity_test ${grp}
           WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/test)
endforeach()
add_test(NAME parity
         COMMAND parity_test
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/test)

# CLI golden test: runs the built CLI on examples/sample-input.json and diffs
# stdout against the committed examples/sample-output.json. Proves the JSON
# plumbing is stable; the math is proven by the parity suite above.
add_test(NAME cli_golden
         COMMAND ${CMAKE_COMMAND}
                 -DCLI=$<TARGET_FILE:trailsense-triangulate>
                 -DIN=${CMAKE_CURRENT_SOURCE_DIR}/examples/sample-input.json
                 -DEXP=${CMAKE_CURRENT_SOURCE_DIR}/examples/sample-output.json
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/test/cli_golden.cmake)
