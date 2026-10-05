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
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
             ${RECOMP_PY} "${CMAKE_SOURCE_DIR}/tools/recomp/config.json" "${LULA_EXE}")

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
     "${CMAKE_SOURCE_DIR}/src/runtime/win32/*.c" "${CMAKE_SOURCE_DIR}/src/reconstructed/*.c")

add_executable(lula ${RUNTIME_SOURCES} ${GEN_SOURCES})
target_include_directories(lula PRIVATE "${CMAKE_SOURCE_DIR}/src/runtime" "${GEN_DIR}")
target_link_libraries(lula PRIVATE PkgConfig::SDL2 Threads::Threads m)
target_compile_options(lula PRIVATE -Wall -Wno-unused-label -Wno-unused-function -fwrapv)
# The generated code is large and simple; -O1 keeps compile times reasonable.
set_source_files_properties(${GEN_SOURCES} PROPERTIES COMPILE_OPTIONS "-O1;-Wno-unused-variable")
