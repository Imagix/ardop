#	ardopcf CMakeLists.txt
#		For Linux, the default build requires gcc, cmake, and development
#		libraries for ALSA.
#			sudo apt install build-essential cmake libasound2-dev
#			cmake -B build
#			cmake --build build
#
#		For Windows, the default build requires installation of a MinGW build
#		environment (see the Makefile for details).
#			cmake -B build -G "MinGW Makefiles"
#			cmake --build build
#
#	Unit tests are built when cmocka is found (sudo apt install libcmocka-dev).
#		cmake --build build --target buildtest   # build tests only
#		ctest --test-dir build                   # build first, then run tests
#
#	To cross-compile for Windows on Linux, a native txt2c is required since
#	it runs on the build machine.  Build it natively first, then pass it in:
#		sudo apt install mingw-w64
#		gcc lib/txt2c/txt2c.c -o txt2c
#		cmake -B build-win \
#			-DCMAKE_SYSTEM_NAME=Windows \
#			-DCMAKE_C_COMPILER=i686-w64-mingw32-gcc-posix \
#			-DTXT2C=$PWD/txt2c
#		cmake --build build-win

cmake_minimum_required(VERSION 3.16)
project(ardopcf C)

# The Makefile builds with -g and no optimization
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
	set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

# Path to txt2c executable; will be built if not provided
set(TXT2C "" CACHE FILEPATH "Path to a native txt2c executable")

if(NOT TXT2C)
	# build txt2c directly and without our link libraries (none are required)
	add_executable(txt2c lib/txt2c/txt2c.c)
	set(TXT2C txt2c)
endif()

# Use txt2c to convert webgui/FOO.xyz → gen-FOO.xyz.c
#   The C symbol name will be FOO_xyz.
#   This is used to convert HTML and JavaScript to C sources.
set(GEN_SOURCES)
foreach(webfile webgui.html webgui.js)
	string(REPLACE "." "_" symbol "${webfile}")
	set(out "${CMAKE_CURRENT_BINARY_DIR}/gen-${webfile}.c")
	add_custom_command(
		OUTPUT "${out}"
		COMMAND ${TXT2C} "${CMAKE_CURRENT_SOURCE_DIR}/webgui/${webfile}" "${out}" ${symbol}
		DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/webgui/${webfile}" ${TXT2C}
		COMMENT "Generating gen-${webfile}.c"
		VERBATIM)
	list(APPEND GEN_SOURCES "${out}")
endforeach()

# list all common sources
# keep sorted by filename
set(ARDOP_SOURCES
	lib/rawhid/rawhid.c
	lib/rockliff/rrs.c
	lib/ws_server/ws_server.c
	src/common/ARDOPC.c
	src/common/ARDOPCommon.c
	src/common/ardopSampleArrays.c
	src/common/ARQ.c
	src/common/BusyDetect.c
	src/common/FEC.c
	src/common/FFT.c
	src/common/HostInterface.c
	src/common/Locator.c
	src/common/log_file.c
	src/common/log.c
	src/common/Modulate.c
	src/common/Packed6.c
	src/common/RXO.c
	src/common/sdft.c
	src/common/SoundInput.c
	src/common/StationId.c
	src/common/TCPHostInterface.c
	src/common/txframe.c
	src/common/wav.c
	src/common/Webgui.c
	src/common/noise.c
	${GEN_SOURCES}
)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

if(WIN32)
	# Windows-only sources
	list(APPEND ARDOP_SOURCES
		src/windows/Waveout.c
		lib/hid/hid.c
	)
	set(ARDOP_PLATFORM_LIBS wsock32 winmm setupapi ws2_32)
else()
	# Linux-only sources
	list(APPEND ARDOP_SOURCES
		src/linux/ALSASound.c
		src/linux/LinSerial.c
	)
	find_package(ALSA REQUIRED)
	set(ARDOP_PLATFORM_LIBS rt ALSA::ALSA)
endif()

# Common code is compiled once and linked into ardopcf and the unit tests,
# just as the Makefile links $(OBJS) into each of them.
add_library(ardop_objs OBJECT ${ARDOP_SOURCES})
target_include_directories(ardop_objs PUBLIC src lib)
target_link_libraries(ardop_objs PUBLIC m Threads::Threads ${ARDOP_PLATFORM_LIBS})

# user-facing executables, like ardopcf
add_executable(ardopcf src/common/ardopcf.c)
target_link_libraries(ardopcf PRIVATE ardop_objs)
target_link_options(ardopcf PRIVATE "LINKER:-Map=${CMAKE_CURRENT_BINARY_DIR}/output.map")

# unit tests (the Makefile's `make buildtest` / `make test`)
include(CTest)
find_library(CMOCKA_LIBRARY cmocka)
find_path(CMOCKA_INCLUDE_DIR cmocka.h)

if(BUILD_TESTING AND CMOCKA_LIBRARY AND CMOCKA_INCLUDE_DIR)
	set(TESTS
		test_ARDOPCommon
		test_HostInterface
		test_Locator
		test_log
		test_Packed6
		test_StationId
	)

	add_custom_target(buildtest)

	foreach(test ${TESTS})
		add_executable(${test} test/ardop/${test}.c test/ardop/setup.c)
		target_include_directories(${test} PRIVATE ${CMOCKA_INCLUDE_DIR})
		target_link_libraries(${test} PRIVATE ${CMOCKA_LIBRARY})
		add_test(NAME ${test} COMMAND ${test})
		add_dependencies(buildtest ${test})
	endforeach()

	# linkage overrides for unit tests
	#   for tests that need only a subset of production code,
	#   link only the sources you want instead of ardop_objs
	#
	#   for tests that need mock functions injected,
	#   wrap each function with LINKER:--wrap=
	target_sources(test_log PRIVATE src/common/log_file.c src/common/log.c)
	target_include_directories(test_log PRIVATE src lib)
	target_link_libraries(test_log PRIVATE m Threads::Threads ${ARDOP_PLATFORM_LIBS})
	foreach(fn fopen fclose fwrite fflush freopen)
		target_link_options(test_log PRIVATE "LINKER:--wrap=${fn}")
	endforeach()

	# all other tests link against the full set of common code
	foreach(test ${TESTS})
		if(NOT test STREQUAL "test_log")
			target_link_libraries(${test} PRIVATE ardop_objs)
		endif()
	endforeach()
elseif(BUILD_TESTING)
	message(STATUS "cmocka not found; unit tests will not be built")
endif()
