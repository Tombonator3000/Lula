# Native build of the statically recompiled game.
#
#   cmake -S . -B build/game -DCMAKE_BUILD_TYPE=Release
#   cmake --build build/game -j
#   build/game/lula -- -novideo
#
# Configuration runs tools/recomp on original/app/WET.EXE and compiles the
# generated C together with the runtime in src/runtime.
cmake_minimum_required(VERSION 3.16)
project(lula C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_EXTENSIONS ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

find_package(Python3 REQUIRED COMPONENTS Interpreter)
find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
find_package(Threads REQUIRED)

set(LULA_EXE "${CMAKE_SOURCE_DIR}/original/app/WET.EXE" CACHE FILEPATH "Original WET.EXE to recompile")
set(GEN_DIR "${CMAKE_BINARY_DIR}/gen")
file(GLOB RECOMP_PY CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/tools/recomp/*.py")
# RT_RECONSTRUCTED markers in src/reconstructed decide which functions the
# recompiler leaves out, so their contents are inputs to the generator too.
file(GLOB RECONSTRUCTED_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/reconstructed/*.c")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
             ${RECOMP_PY} ${RECONSTRUCTED_SOURCES} "${CMAKE_SOURCE_DIR}/tools/recomp/config.json" "${LULA_EXE}")

message(STATUS "Recompiling ${LULA_EXE}")
execute_process(
  COMMAND "${Python3_EXECUTABLE}" -m tools.recomp --exe "${LULA_EXE}" --out "${GEN_DIR}"
  WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
  RESULT_VARIABLE recomp_result)
if(NOT recomp_result EQUAL 0)
  message(FATAL_ERROR "tools/recomp failed (is capstone installed? pip install -r tools/recomp/requirements.txt)")
endif()
file(STRINGS "${GEN_DIR}/sources.txt" gen_names)
set(GEN_SOURCES "")
foreach(name IN LISTS gen_names)
  list(APPEND GEN_SOURCES "${GEN_DIR}/${name}")
endforeach()

file(GLOB RUNTIME_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/runtime/*.c"
     "${CMAKE_SOURCE_DIR}/src/runtime/win32/*.c")
list(REMOVE_ITEM RUNTIME_SOURCES "${CMAKE_SOURCE_DIR}/src/runtime/main.c")

# Everything except main() goes into one object library shared by the game
# and the tools.
add_library(lula_core OBJECT ${RUNTIME_SOURCES} ${RECONSTRUCTED_SOURCES} ${GEN_SOURCES})
target_include_directories(lula_core PUBLIC "${CMAKE_SOURCE_DIR}/src/runtime"
                           "${CMAKE_SOURCE_DIR}/src/reconstructed" "${GEN_DIR}")
target_link_libraries(lula_core PUBLIC PkgConfig::SDL2 Threads::Threads m)
target_compile_options(lula_core PRIVATE -Wall -Wno-unused-label -Wno-unused-function -fwrapv)
# The generated code is large and simple; -O1 keeps compile times reasonable.
# -frounding-math: the x87 rounding and precision bits are loaded into the
# host FPU at run time, so the compiler must not fold or reorder FP code
# across those changes.
set_source_files_properties(${GEN_SOURCES} PROPERTIES COMPILE_OPTIONS "-O1;-Wno-unused-variable;-frounding-math")

add_executable(lula "${CMAKE_SOURCE_DIR}/src/runtime/main.c")
target_link_libraries(lula PRIVATE lula_core)
target_compile_options(lula PRIVATE -Wall)

# tools/fncheck: compares hand-reconstructed functions with the generated ones.
add_executable(lula-fncheck "${CMAKE_SOURCE_DIR}/src/tools/fncheck.c")
target_link_libraries(lula-fncheck PRIVATE lula_core)
target_compile_options(lula-fncheck PRIVATE -Wall)
