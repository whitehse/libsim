cmake_minimum_required(VERSION 3.20)
project(libsim LANGUAGES C VERSION 0.1.0)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(NOT MSVC)
  add_compile_options(-Wall -Wextra -Wpedantic -Werror)
endif()

add_library(sim STATIC
  src/sim_clock.c
  src/sim_timer.c
)
target_include_directories(sim PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)

enable_testing()
add_executable(sim_clock_timer_test tests/test_sim_clock_timer.c)
target_link_libraries(sim_clock_timer_test PRIVATE sim)
add_test(NAME sim_clock_timer COMMAND sim_clock_timer_test)

include(GNUInstallDirs)
install(TARGETS sim ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  FILES_MATCHING PATTERN "*.h")
