cmake_minimum_required(VERSION 3.20)
project(libsim LANGUAGES C VERSION 0.1.0)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(NOT MSVC)
  add_compile_options(-Wall -Wextra -Wpedantic -Werror)
endif()

option(LIBSIM_NO_URING "Exclude sim_uring (class B/C builds)" OFF)

set(SIM_SOURCES
  src/sim_clock.c
  src/sim_timer.c
  src/sim_net.c
)
if(NOT LIBSIM_NO_URING)
  list(APPEND SIM_SOURCES src/sim_uring.c)
endif()

add_library(sim STATIC ${SIM_SOURCES})
target_include_directories(sim PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
if(LIBSIM_NO_URING)
  target_compile_definitions(sim PUBLIC LIBSIM_NO_URING=1)
endif()

enable_testing()
add_executable(sim_clock_timer_test tests/test_sim_clock_timer.c)
target_link_libraries(sim_clock_timer_test PRIVATE sim)
add_test(NAME sim_clock_timer COMMAND sim_clock_timer_test)

add_executable(sim_net_test tests/test_sim_net.c)
target_link_libraries(sim_net_test PRIVATE sim)
add_test(NAME sim_net COMMAND sim_net_test)

if(NOT LIBSIM_NO_URING)
  add_executable(sim_uring_test tests/test_sim_uring.c)
  target_link_libraries(sim_uring_test PRIVATE sim)
  add_test(NAME sim_uring COMMAND sim_uring_test)
endif()

include(GNUInstallDirs)
install(TARGETS sim ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  FILES_MATCHING PATTERN "*.h")
