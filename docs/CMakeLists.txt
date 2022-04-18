


# Find all the public headers
set(SOURCE_DIR ${PROJECT_SOURCE_DIR}/src)
get_target_property(BTRFS_ASSISTANT_PUBLIC_HEADER_DIR BtrfsAssistant INTERFACE_INCLUDE_DIRECTORIES)
file(GLOB_RECURSE BTRFS_ASSISTANT_PUBLIC_HEADERS
    ${SOURCE_DIR}/model/*.h
    ${SOURCE_DIR}/ui/*.h
    ${SOURCE_DIR}/util/*h
    ${SOURCE_DIR}/widgets/*.h )

find_package(Doxygen)
if (Doxygen_FOUND)
    set(DOXYGEN_INPUT_DIR ${PROJECT_SOURCE_DIR}/src)
    set(DOXYGEN_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/doxygen)
    set(DOXYGEN_INDEX_FILE ${DOXYGEN_OUTPUT_DIR}/xml/index.xml)
    set(DOXYFILE_IN ${PROJECT_SOURCE_DIR}/docs/Doxyfile.in)
    set(DOXYFILE_OUT ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile)

    #Replace variables inside @@ with the current values
    configure_file(${DOXYFILE_IN} ${DOXYFILE_OUT} @ONLY)

    file(MAKE_DIRECTORY ${DOXYGEN_OUTPUT_DIR}) #Doxygen won't create this for us
    add_custom_command(OUTPUT ${DOXYGEN_INDEX_FILE}
                       DEPENDS ${BTRFS_ASSISTANT_PUBLIC_HEADERS}
                       COMMAND ${DOXYGEN_EXECUTABLE} ${DOXYFILE_OUT}
                       MAIN_DEPENDENCY ${DOXYFILE_OUT} ${DOXYFILE_IN}
                       COMMENT "Generating docs with Doxygen")

    add_custom_target(Doxygen ALL DEPENDS ${DOXYGEN_INDEX_FILE})
else()
    message(STATUS "Doxygen not found, not building docs")
endif()

find_package(Sphinx)
if (Sphinx_FOUND)
    set(SPHINX_SOURCE ${CMAKE_CURRENT_SOURCE_DIR})
    set(SPHINX_BUILD ${CMAKE_CURRENT_BINARY_DIR}/sphinx)
    set(SPHINX_INDEX_FILE ${SPHINX_BUILD}/index.html)

    add_custom_target(Sphinx ALL
                    COMMAND ${SPHINX_EXECUTABLE} -b html
                    # Tell Breathe where to find the Doxygen output
                    -Dbreathe_projects.BtrfsAssistant=${DOXYGEN_OUTPUT_DIR}/xml
                    ${SPHINX_SOURCE} ${SPHINX_BUILD}
                    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
                    COMMENT "Generating documentation with Sphinx")
else()
    message(STATUS "Sphinx not found, not building docs")
endif()


