cmake_minimum_required(VERSION 3.16)
project(fac_bl_tests C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Sanitizers when the compiler supports them
include(CheckCSourceCompiles)
# UBSan always; AddressSanitizer only on request (-DFAC_TESTS_ASAN=ON): in Docker on Windows (WSL2)
# its startup crashes at random (high entropy ASLR), which made the runs flaky
option(FAC_TESTS_ASAN "Also build with AddressSanitizer" OFF)
set(SAN_FLAGS "-fsanitize=undefined")
if(FAC_TESTS_ASAN)
	set(SAN_FLAGS "-fsanitize=address,undefined")
endif()
set(CMAKE_REQUIRED_FLAGS "${SAN_FLAGS}")
set(CMAKE_REQUIRED_LINK_OPTIONS "${SAN_FLAGS}")
check_c_source_compiles("int main(void){return 0;}" HAVE_SANITIZERS)
unset(CMAKE_REQUIRED_FLAGS)
unset(CMAKE_REQUIRED_LINK_OPTIONS)
if(HAVE_SANITIZERS)
	add_compile_options(${SAN_FLAGS} -fno-sanitize-recover=undefined -fno-omit-frame-pointer)
	add_link_options(${SAN_FLAGS})
endif()

set(LOGIC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../Core/Src/Bootloader)

# Logic under test (never the STM32 port or the info block)
add_library(bl_logic STATIC
	${LOGIC_DIR}/fac_bl.c
	${LOGIC_DIR}/fac_bl_crc.c
	${LOGIC_DIR}/fac_bl_app.c
	${LOGIC_DIR}/fac_bl_flash.c
	${LOGIC_DIR}/fac_bl_proto.c)
target_include_directories(bl_logic PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../Core/Inc)
target_compile_options(bl_logic PRIVATE -Wall -Wextra)

# Simulated hardware and helpers
add_library(bl_sim STATIC sim_port.c helpers.c)
target_include_directories(bl_sim PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/../Core/Inc)
target_compile_options(bl_sim PRIVATE -Wall -Wextra -Werror)

enable_testing()
foreach(name test_crc test_app test_fastpath test_proto)
	add_executable(${name} ${name}.c)
	target_compile_options(${name} PRIVATE -Wall -Wextra -Werror)
	target_link_libraries(${name} PRIVATE bl_sim bl_logic)
	add_test(NAME ${name} COMMAND ${name})
	set_tests_properties(${name} PROPERTIES TIMEOUT 60)
endforeach()
