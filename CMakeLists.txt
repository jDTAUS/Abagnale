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

if(MSVC)
	set(CMAKE_C_STANDARD	11)
	set(CMAKE_C_STANDARD_REQUIRED	ON)
	set(CMAKE_C_EXTENSIONS	OFF)
	add_compile_definitions(HAVE_ABAG_HOST_H)
endif()

include(CheckTypeSize)
include(CheckCSourceRuns)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
include(FindPackageHandleStandardArgs)
include(CheckIncludeFile)

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

if(NOT HAVE_THREADS_H)
	find_package(Threads REQUIRED)
endif()

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

if(PostgreSQL_FOUND AND NOT TARGET PostgreSQL::PostgreSQL)
	if(WIN32)
		add_library(PostgreSQL::PostgreSQL SHARED IMPORTED)
		set_target_properties(PostgreSQL::PostgreSQL PROPERTIES
			IMPORTED_LOCATION "${PostgreSQL_LIBRARY_DIR}/libpq.dll"
			IMPORTED_IMPLIB "${PostgreSQL_LIBRARY}"
			INTERFACE_INCLUDE_DIRECTORIES
				"${PostgreSQL_INCLUDE_DIRS}"
		)
	else()
		add_library(PostgreSQL::PostgreSQL UNKNOWN IMPORTED)
		set_target_properties(PostgreSQL::PostgreSQL PROPERTIES
			IMPORTED_LOCATION "${PostgreSQL_LIBRARY}"
			INTERFACE_INCLUDE_DIRECTORIES
				"${PostgreSQL_INCLUDE_DIRS}"
		)
	endif()
elseif(TARGET PostgreSQL::PostgreSQL)
	get_target_property(location PostgreSQL::PostgreSQL IMPORTED_LOCATION)
	if(NOT location)
		if(WIN32)
			set_target_properties(PostgreSQL::PostgreSQL PROPERTIES
				IMPORTED_LOCATION
					"${PostgreSQL_LIBRARY_DIR}/libpq.dll"
				IMPORTED_IMPLIB "${PostgreSQL_LIBRARY}"
				INTERFACE_INCLUDE_DIRECTORIES
					"${PostgreSQL_INCLUDE_DIRS}"
			)
		else()
			set_target_properties(PostgreSQL::PostgreSQL PROPERTIES
				IMPORTED_LOCATION "${PostgreSQL_LIBRARY}"
				INTERFACE_INCLUDE_DIRECTORIES
					"${PostgreSQL_INCLUDE_DIRS}"
			)
		endif()
	endif()
endif()

if(PostgreSQL_ECPG_LIBRARY AND NOT TARGET PostgreSQL::ECPG)
	if(WIN32)
		add_library(PostgreSQL::ECPG SHARED IMPORTED)
		set_target_properties(PostgreSQL::ECPG PROPERTIES
			IMPORTED_LOCATION
				"${PostgreSQL_LIBRARY_DIR}/libecpg.dll"
			IMPORTED_IMPLIB "${PostgreSQL_ECPG_LIBRARY}"
			INTERFACE_INCLUDE_DIRECTORIES
				"${PostgreSQL_INCLUDE_DIRS}"
		)
	else()
		add_library(PostgreSQL::ECPG UNKNOWN IMPORTED)
		set_target_properties(PostgreSQL::ECPG PROPERTIES
			IMPORTED_LOCATION "${PostgreSQL_ECPG_LIBRARY}"
			INTERFACE_INCLUDE_DIRECTORIES
				"${PostgreSQL_INCLUDE_DIRS}"
		)
	endif()
endif()

if(PostgreSQL_PGTYPES_LIBRARY AND NOT TARGET PostgreSQL::PGTYPES)
	if(WIN32)
		add_library(PostgreSQL::PGTYPES SHARED IMPORTED)
		set_target_properties(PostgreSQL::PGTYPES PROPERTIES
			IMPORTED_LOCATION
				"${PostgreSQL_LIBRARY_DIR/libpgtypes.dll}"
			IMPORTED_IMPLIB "${PostgreSQL_PGTYPES_LIBRARY}"
			INTERFACE_INCLUDE_DIRECTORIES
				"${PostgreSQL_INCLUDE_DIRS}"
		)
	else()
		add_library(PostgreSQL::PGTYPES UNKNOWN IMPORTED)
		set_target_properties(PostgreSQL::PGTYPES PROPERTIES
			IMPORTED_LOCATION "${PostgreSQL_PGTYPES_LIBRARY}"
			INTERFACE_INCLUDE_DIRECTORIES
				"${PostgreSQL_INCLUDE_DIRS}"
		)
	endif()
endif()

find_program(ECPG_EXECUTABLE ecpg REQUIRED)
find_program(YACC_EXECUTABLE
	NAMES yacc win_bison bison
	DOC "yacc compatible parser generator"
	REQUIRED
)

pkg_check_modules(stdthreads stdthreads IMPORTED_TARGET)

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
		"Running ecpg on database-postgresql.pgc"
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
		"Running yacc on config.y"
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

install(
	FILES
		abagnale.conf
	DESTINATION
		${CMAKE_INSTALL_SYSCONFDIR}/abagnale
)

if(WIN32)
	install(
		TARGETS
			abagnale
			abagnalectl
		RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
		LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
		ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
		INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/${PROJECT_NAME}
		RUNTIME_DEPENDENCY_SETS dependencies
	)

	install(
		RUNTIME_DEPENDENCY_SET dependencies
		DESTINATION ${CMAKE_INSTALL_BINDIR}
	)
else()
	install(
		TARGETS
			abagnale
			abagnalectl
		RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
		LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
		ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
		INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/${PROJECT_NAME}
	)
endif()
