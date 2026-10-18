cmake_minimum_required(VERSION 3.28.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(gb_player)

target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
target_sources(app PRIVATE
	src/main.c
	src/menu.c
	src/rom_loader.c
	../src/gb.c
	../src/gb_cpu.c
	../src/gb_mbc.c
	../src/gb_ppu.c
)
