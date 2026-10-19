cmake_minimum_required(VERSION 3.20)
project(Lide25Driver VERSION 0.3.0 LANGUAGES NONE)
# The tested native build is tools/build.py. Avoid a second, divergent source list.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_custom_target(driver ALL
  COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_SOURCE_DIR}/tools/build.py"
  WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
  USES_TERMINAL
  COMMENT "Building the Windows 10 x64 LiDE 25 user-mode driver")
