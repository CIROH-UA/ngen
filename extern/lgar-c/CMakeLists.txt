cmake_minimum_required(VERSION 3.10)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

string(ASCII 27 Esc)
set(ColourReset "${Esc}[m")
set(Red         "${Esc}[31m")
# module setup options

# set the project name
project(lasambmi VERSION 1.0.0 DESCRIPTION "OWP LASAM BMI Module Shared Library")

IF(CMAKE_BUILD_TYPE MATCHES Debug)
    message("Debug build.")
ENDIF(CMAKE_BUILD_TYPE MATCHES Debug)

message(CMAKE_CXX_COMPILER " ${CMAKE_CXX_COMPILER}")
message(CMAKE_C_COMPILER " ${CMAKE_C_COMPILER}")
message("CMAKE_BUILD_TYPE = ${CMAKE_BUILD_TYPE}")

# Based on other submodules pattern, support relative paths from ngen
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/LGAR-C")
    set(LGAR_SRC_PREFIX "LGAR-C/")
else()
    set(LGAR_SRC_PREFIX "")
endif()

# standalone
add_executable(lasam_standalone ${LGAR_SRC_PREFIX}src/bmi_main_lgar.cxx ${LGAR_SRC_PREFIX}src/bmi_lgar.cxx ${LGAR_SRC_PREFIX}src/lgar.cxx ${LGAR_SRC_PREFIX}src/soil_funcs.cxx ${LGAR_SRC_PREFIX}src/conceptual_reservoir.cxx
            ${LGAR_SRC_PREFIX}src/linked_list.cxx ${LGAR_SRC_PREFIX}src/mem_funcs.cxx ${LGAR_SRC_PREFIX}src/util_funcs.cxx ${LGAR_SRC_PREFIX}src/aet.cxx
            ${LGAR_SRC_PREFIX}giuh/giuh.h ${LGAR_SRC_PREFIX}giuh/giuh.c)
target_link_libraries(lasam_standalone PRIVATE m)

# unittest
add_executable(lasam_unitest ${LGAR_SRC_PREFIX}tests/main_unit_test_bmi.cxx ${LGAR_SRC_PREFIX}src/bmi_lgar.cxx ${LGAR_SRC_PREFIX}src/lgar.cxx ${LGAR_SRC_PREFIX}src/soil_funcs.cxx ${LGAR_SRC_PREFIX}src/conceptual_reservoir.cxx
            ${LGAR_SRC_PREFIX}src/linked_list.cxx ${LGAR_SRC_PREFIX}src/mem_funcs.cxx ${LGAR_SRC_PREFIX}src/util_funcs.cxx ${LGAR_SRC_PREFIX}src/aet.cxx ${LGAR_SRC_PREFIX}giuh/giuh.h
            ${LGAR_SRC_PREFIX}giuh/giuh.c)
target_link_libraries(lasam_unitest PRIVATE m)

# Make sure these are compiled with this directive
add_compile_definitions(BMI_ACTIVE)

add_library(lasambmi SHARED ${LGAR_SRC_PREFIX}src/bmi_lgar.cxx ${LGAR_SRC_PREFIX}src/lgar.cxx ${LGAR_SRC_PREFIX}src/soil_funcs.cxx ${LGAR_SRC_PREFIX}src/linked_list.cxx ${LGAR_SRC_PREFIX}src/mem_funcs.cxx ${LGAR_SRC_PREFIX}src/conceptual_reservoir.cxx
        ${LGAR_SRC_PREFIX}src/util_funcs.cxx ${LGAR_SRC_PREFIX}src/aet.cxx ${LGAR_SRC_PREFIX}giuh/giuh.c ${LGAR_SRC_PREFIX}include/all.hxx ${LGAR_SRC_PREFIX}giuh/giuh.h)

target_compile_definitions(lasambmi PRIVATE NGEN)
target_include_directories(lasambmi PRIVATE ${LGAR_SRC_PREFIX}include)

set_target_properties(lasambmi PROPERTIES VERSION ${PROJECT_VERSION})

set_target_properties(lasambmi PROPERTIES PUBLIC_HEADER ./include/bmi_lgar.hxx)

include(GNUInstallDirs)

install(TARGETS lasambmi
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

configure_file(lasambmi.pc.in lasambmi.pc @ONLY)

install(FILES ${CMAKE_BINARY_DIR}/lasambmi.pc DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/pkgconfig)
