# $JDTAUS$

# Copyright (c) 2026 Christian Schulte <cs@schulte.it>
#
# Permission to use, copy, modify, and distribute this software for any
# purpose with or without fee is hereby granted, provided that the above
# copyright notice and this permission notice appear in all copies.
#
# THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
# WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
# MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
# ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
# WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
# ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
# OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

cmake_minimum_required(VERSION 3.31)

project(
	Abagnale
	VERSION 0.0
	DESCRIPTION "Abagnale - Algorithmic Trading"
	HOMEPAGE_URL "http://www.jdtaus.org"
	LANGUAGES C
)

add_compile_definitions(MULTI_THREADED)
add_compile_definitions(STRING_INTERNING)

if(MSVC)
	set(CMAKE_C_STANDARD	11)
	set(CMAKE_C_STANDARD_REQUIRED	ON)
	set(CMAKE_C_EXTENSIONS	OFF)
	file(REAL_PATH "~" HOME EXPAND_TILDE)
	add_compile_definitions(HAVE_HOST_H)
  add_compile_definitions(ABAG_CONFIG_FILE=${HOME}/abagnale.conf)
endif()

include(CheckTypeSize)
include(CheckCSourceRuns)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
include(FindPackageHandleStandardArgs)
include(CheckIncludeFile)
include(FetchContent)

check_include_file("threads.h" HAVE_THREADS_H)
check_type_size("wchar_t" SIZEOF_WCHAR_T)

check_c_source_runs([[
	#include <wchar.h>
	int main(int argc, char *argv[]) {
		wchar_t *s = L"\\U0010FFFF";
		return sizeof(wchar_t) >= 4 && wcslen(s) == 1
		    && *s == 0x10FFFF ? 0 : -1;
	}
]] WCHAR_T_UTF32)

check_c_source_runs([[
	#include <wchar.h>
	int main(int argc, char *argv[]) {
		wchar_t *s = L"\\U0010FFFF";
		return sizeof(wchar_t) >= 2 && wcslen(s) == 2
		    && s[0] == 0xdbff && s[1] == 0xdfff ? 0 : -1;
	}
]] WCHAR_T_UTF16)

check_c_source_runs([[
	#include <wchar.h>
	int main(int argc, char *argv[]) {
		wchar_t *s = L"\\U0010FFFF";
		return sizeof(wchar_t) >= 1 && wcslen(s) == 4
		    && s[0] == 0xf4 && s[1] == 0x8f && s[2] == 0xbf
		    && s[3] == 0xbf ? 0 : -1;
	}
]] WCHAR_T_UTF8)

if(HAVE_SIZEOF_WCHAR_T)
	add_compile_definitions(SIZEOF_WCHAR_T=${SIZEOF_WCHAR_T})
endif()

if(WCHAR_T_UTF32)
	add_compile_definitions(WCHAR_T_UTF32)
endif()

if(WCHAR_T_UTF16)
	add_compile_definitions(WCHAR_T_UTF16)
endif()

if(WCHAR_T_UTF8)
	add_compile_definitions(WCHAR_T_UTF8)
endif()

find_package(OpenSSL REQUIRED)
find_package(PostgreSQL REQUIRED)
find_package(PkgConfig REQUIRED)

find_library(PostgreSQL_ECPG_LIBRARY
	NAMES ecpg
	HINTS ${PostgreSQL_LIBRARY_DIR}
	PATHS ${PostgreSQL_LIBRARY_DIR}
	REQUIRED
)

find_library(PostgreSQL_PGTYPES_LIBRARY
	NAMES pgtypes
	HINTS ${PostgreSQL_LIBRARY_DIR}
	PATHS ${PostgreSQL_LIBRARY_DIR}
	REQUIRED
)

macro(target_info TARGET_NAME)
	if(NOT TARGET ${TARGET_NAME})
		message(WARNING "${TARGET_NAME} does not exist")
		return()
	endif()

	get_target_property(
		loc
		${TARGET_NAME}
		IMPORTED_LOCATION
	)
	get_target_property(
		imp
		${TARGET_NAME}
		IMPORTED_IMPLIB
	)
	get_target_property(
		inc
		${TARGET_NAME}
		INTERFACE_INCLUDE_DIRECTORIES
	)

	message(STATUS "${TARGET_NAME}: IMPORTED_LOCATION: ${loc}")
	message(STATUS "${TARGET_NAME}: IMPORTED_IMPLIB: ${imp}")
	message(STATUS "${TARGET_NAME}: INTERFACE_INCLUDE_DIRECTORIES: ${inc}")
endmacro()

macro(patch_postgresql_includes TARGET_NAME)
	if(NOT TARGET ${TARGET_NAME})
		message(WARNING "${TARGET_NAME} does not exist")
		return()
	endif()

	if(IS_DIRECTORY "${PostgreSQL_INCLUDE_DIR}/postgresql")
		get_target_property(
			inc
			${TARGET_NAME}
			INTERFACE_INCLUDE_DIRECTORIES
		)

		list(APPEND inc "${PostgreSQL_INCLUDE_DIR}/postgresql")
		list(REMOVE_DUPLICATES inc)

		set_target_properties(
			${TARGET_NAME}
			PROPERTIES
				INTERFACE_INCLUDE_DIRECTORIES "${inc}"
		)
	endif()
endmacro()

message(CHECK_START "Checking target PostgreSQL::PostgreSQL")
if(PostgreSQL_FOUND AND NOT TARGET PostgreSQL::PostgreSQL)
	message(CHECK_FAIL "not provided - creating")
	add_library(PostgreSQL::PostgreSQL UNKNOWN IMPORTED)
	set_target_properties(PostgreSQL::PostgreSQL PROPERTIES
		IMPORTED_LOCATION "${PostgreSQL_LIBRARY}"
		INTERFACE_INCLUDE_DIRECTORIES "${PostgreSQL_INCLUDE_DIRS}"
	)
elseif(TARGET PostgreSQL::PostgreSQL)
	get_target_property(
		loc
		PostgreSQL::PostgreSQL
		IMPORTED_LOCATION
	)

	if(NOT loc)
		message(CHECK_FAIL "no location - creating")
		set_target_properties(PostgreSQL::PostgreSQL PROPERTIES
			IMPORTED_LOCATION "${PostgreSQL_LIBRARY}"
			INTERFACE_INCLUDE_DIRECTORIES "${PostgreSQL_INCLUDE_DIRS}"
		)
	else()
		message(CHECK_PASS "provided")
	endif()
	patch_postgresql_includes(PostgreSQL::PostgreSQL)
endif()
target_info(PostgreSQL::PostgreSQL)

message(CHECK_START "Checking target PostgreSQL::ECPG")
if(PostgreSQL_ECPG_LIBRARY AND NOT TARGET PostgreSQL::ECPG)
	message(CHECK_FAIL "not provided - creating")
	add_library(PostgreSQL::ECPG UNKNOWN IMPORTED)
	set_target_properties(PostgreSQL::ECPG PROPERTIES
		IMPORTED_LOCATION "${PostgreSQL_ECPG_LIBRARY}"
		INTERFACE_INCLUDE_DIRECTORIES "${PostgreSQL_INCLUDE_DIRS}"
	)
	patch_postgresql_includes(PostgreSQL::ECPG)
else()
	message(CHECK_PASS "provided")
endif()
target_info(PostgreSQL::ECPG)

message(CHECK_START "Checking target PostgreSQL::PGTYPES")
if(PostgreSQL_PGTYPES_LIBRARY AND NOT TARGET PostgreSQL::PGTYPES)
	message(CHECK_FAIL "not provided - creating")
	add_library(PostgreSQL::PGTYPES UNKNOWN IMPORTED)
	set_target_properties(PostgreSQL::PGTYPES PROPERTIES
		IMPORTED_LOCATION "${PostgreSQL_PGTYPES_LIBRARY}"
		INTERFACE_INCLUDE_DIRECTORIES "${PostgreSQL_INCLUDE_DIRS}"
	)
	patch_postgresql_includes(PostgreSQL::PGTYPES)
else()
	message(CHECK_PASS "provided")
endif()
target_info(PostgreSQL::PGTYPES)

message(CHECK_START "Checking target OpenSSL::Crypto")
if(OpenSSL_FOUND AND NOT TARGET OpenSSL::Crypto)
	message(CHECK_FAIL "not provided - ignoring")
else()
	message(CHECK_PASS "provided")
endif()
target_info(OpenSSL::Crypto)

message(CHECK_START "Checking target OpenSSL::SSL")
if(OpenSSL_FOUND AND NOT TARGET OpenSSL:SSL)
	message(CHECK_FAIL "not provided - ignoring")
else()
	message(CHECK_PASS "provided")
endif()
target_info(OpenSSL::SSL)

pkg_check_modules(stdthreads stdthreads IMPORTED_TARGET)

if(stdthreads_FOUND)
	message(CHECK_START "Checking target PkgConfig::stdthreads")
	if(NOT TARGET PkgConfig::stdthreads)
		message(CHECK_FAIL "not provided - ignoring")
	else()
		message(CHECK_PASS "provided")
	endif()
	target_info(PkgConfig::stdthreads)
elseif(NOT HAVE_THREADS_H)
	FetchContent_declare(
		static-stdthreads
		GIT_REPOSITORY https://github.com/ChristianSchulte/libstdthreads.git
		GIT_TAG b279390634cbdcec56d4c3d5facff3920914068f
	)
	FetchContent_MakeAvailable(static-stdthreads)
endif()

find_program(ECPG_EXECUTABLE ecpg REQUIRED)
find_program(YACC_EXECUTABLE
	NAMES yacc bison win_bison
	DOC "yacc compatible parser generator"
	REQUIRED
)

add_custom_command(
	OUTPUT
		${CMAKE_CURRENT_SOURCE_DIR}/database-postgresql.c
	COMMAND
		${ECPG_EXECUTABLE}
		-o ${CMAKE_CURRENT_SOURCE_DIR}/database-postgresql.c
		${CMAKE_CURRENT_SOURCE_DIR}/database-postgresql.pgc
	DEPENDS
		${CMAKE_CURRENT_SOURCE_DIR}/database-postgresql.pgc
	COMMENT
		"Running ${ECPG_EXECUTABLE} on database-postgresql.pgc"
	VERBATIM
)

add_custom_command(
	OUTPUT
		${CMAKE_CURRENT_SOURCE_DIR}/config.c
	COMMAND
		${YACC_EXECUTABLE}
		-o ${CMAKE_CURRENT_SOURCE_DIR}/config.c
		${CMAKE_CURRENT_SOURCE_DIR}/config.y
	DEPENDS
		${CMAKE_CURRENT_SOURCE_DIR}/config.y
	COMMENT
		"Running ${YACC_EXECUTABLE} on config.y"
	VERBATIM
)

add_library(objects OBJECT
	abagnale.c
	abagnalectl.c
	algorithm-trend.c
	array.c
	exchange-coinbase.c
	exchange.c
	heap.c
	main.c
	map.c
	math-postgresql.c
	mongoose.c
	patterns.c
	proc.c
	queue.c
	string.c
	thread.c
	time.c
	wcjson.c
	wcjson-document.c
	${CMAKE_CURRENT_SOURCE_DIR}/config.c
	${CMAKE_CURRENT_SOURCE_DIR}/database-postgresql.c
)

set_source_files_properties(
	${CMAKE_CURRENT_SOURCE_DIR}/config.c
	${CMAKE_CURRENT_SOURCE_DIR}/database-postgresql.c
	PROPERTIES
		GENERATED TRUE
)

add_executable(abagnale $<TARGET_OBJECTS:objects>)
add_executable(abagnalectl $<TARGET_OBJECTS:objects>)

set_target_properties(
	abagnale
	abagnalectl
	PROPERTIES
		MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
	VERSION
		${PROJECT_VERSION}
)

target_include_directories(objects PRIVATE
	${PostgreSQL_INCLUDE_DIRS}
)

target_link_libraries(objects PRIVATE
	OpenSSL::Crypto
	OpenSSL::SSL
	PostgreSQL::ECPG
	PostgreSQL::PGTYPES
	PostgreSQL::PostgreSQL
)

if(NOT WIN32)
	target_link_libraries(objects PRIVATE m)
endif()

if(stdthreads_FOUND)
	target_link_libraries(objects PRIVATE PkgConfig::stdthreads)
elseif(NOT HAVE_THREADS_H)
	target_include_directories(objects PRIVATE
		${static-stdthreads_SOURCE_DIR}
	)
	target_link_libraries(abagnale PRIVATE bsd-stdthreads)
	target_link_libraries(abagnalectl PRIVATE bsd-stdthreads)
endif()

target_link_libraries(abagnale PRIVATE objects)
target_link_libraries(abagnalectl PRIVATE objects)

add_custom_command(
	TARGET abagnale POST_BUILD
	COMMAND
		${CMAKE_COMMAND} -E remove -f
		${CMAKE_CURRENT_SOURCE_DIR}/config.c
		${CMAKE_CURRENT_SOURCE_DIR}/database-postgresql.c
)

if(BUILD_TESTING)
	enable_testing()
	add_test(
		NAME
			abagnale
		COMMAND
			abagnale -n -f ${CMAKE_SOURCE_DIR}/abagnale.conf
	)
	add_test(
		NAME
			abagnalectl
		COMMAND
			abagnalectl -n -f ${CMAKE_SOURCE_DIR}/abagnale.conf
	)
endif()

install(
	FILES
		abagnale.conf
	DESTINATION
		${CMAKE_INSTALL_SYSCONFDIR}/abagnale
)

install(
	TARGETS
		abagnale
		abagnalectl
	RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
	LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
	ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
	INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/${PROJECT_NAME}
)
