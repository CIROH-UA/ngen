# SUMMA for the Next Gen framework.
#
# SUMMA's own build/CMakeLists.txt already carries the USE_NEXTGEN branch, which
# builds the BMI shared library instead of the standalone executables and expects
# the SUMMA source one directory down -- exactly the layout here. So include it
# rather than keeping a copy.
#
# This used to be a copy, and it drifted: it predated the domain-parallel MPI,
# mizuRoute and objective-function work and so was missing the SUMMA_NRTYPE and
# SUMMA_DATA_TYPES targets those introduced, which broke the NextGen build with
# "Cannot open module file 'nr_type.mod'". Including keeps the two in step.
#
# Paths inside the included file are resolved against F_MASTER (the SUMMA root) or
# CMAKE_CURRENT_SOURCE_DIR (this directory), both of which are correct here.

cmake_minimum_required(VERSION 3.21 FATAL_ERROR)

include(${CMAKE_CURRENT_SOURCE_DIR}/summa/build/CMakeLists.txt)
