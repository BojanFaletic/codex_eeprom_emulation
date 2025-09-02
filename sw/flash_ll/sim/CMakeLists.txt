set(SIM_SOURCES
    src/flash_sim.c
    src/axi_spi_sim.c
    src/flash_ll_io_sim.c
    ../src/flash_ll.c
    src/sim_main.c
)

add_executable(sim_flash_ll ${SIM_SOURCES})
target_include_directories(sim_flash_ll PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include ${CMAKE_CURRENT_SOURCE_DIR}/../include)

if(MSVC)
  target_compile_options(sim_flash_ll PRIVATE /W4 /permissive-)
else()
  target_compile_options(sim_flash_ll PRIVATE -Wall -Wextra -Werror=implicit-function-declaration)
endif()
