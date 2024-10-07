# dependency macros
macro(find_dependency_path DEPENDENCY FILE)
	# searches extern for dependencies and if not checks the environment variable
	if(NOT ${DEPENDENCY} STREQUAL "")
		# Check extern
		message(
			STATUS
			"Searching for ${DEPENDENCY} using file ${FILE}"
		)
		find_path("${DEPENDENCY}Path"
			${FILE}
			PATHS "${CMAKE_CURRENT_SOURCE_DIR}/../extern/${DEPENDENCY}")

		if("${${DEPENDENCY}Path}" STREQUAL "${DEPENDENCY}Path-NOTFOUND")
			# Check path
			message(
				STATUS
				"Getting environment for ${DEPENDENCY}Path: $ENV{${DEPENDENCY}Path}"
			)
			set("${DEPENDENCY}Path" "$ENV{${DEPENDENCY}Path}")
		endif()

		message(
			STATUS
			"Found ${DEPENDENCY} in ${${DEPENDENCY}Path}; adding"
		)
		add_subdirectory("${${DEPENDENCY}Path}" ${DEPENDENCY})
	endif()
endmacro()


# standards & flags
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_DEBUG OFF)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)
set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE FILEPATH "")


# out-of-source builds only
if(${PROJECT_SOURCE_DIR} STREQUAL ${PROJECT_BINARY_DIR})
	message(FATAL_ERROR "In-source builds are not allowed.")
endif()
