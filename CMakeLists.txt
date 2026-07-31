cmake_minimum_required(VERSION 3.10.0)
project(arm_sme VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE INTERNAL "")

add_subdirectory(ILAng)

add_executable(main src/main.cc src/arm.cc src/instr.cc src/test_helpers.cc test/test_pstate.cc test/test_zero_mova.cc test/test_quick.cc test/test_integer_operation.cc)
target_include_directories(main PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(main PRIVATE ilang::ilang)