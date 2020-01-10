
set(TIOGA_EXE_SOURCES
  modules.f90
  gridutils.f90
  poisson_utils.f90
)
if (TIOGA_ENABLE_ARBORX)
  list(APPEND TIOGA_EXE_SOURCES
      kokkos.f90
      kokkos.C
  )
endif()

add_library(tiogadriver ${TIOGA_EXE_SOURCES})
if (TIOGA_ENABLE_ARBORX)
  target_link_libraries(tiogadriver PRIVATE ArborX::ArborX)
endif()
# target_compile_definitions(tiogadriver PUBLIC ${MPI_Fortran_COMPILE_FLAGS})
target_include_directories(tiogadriver PUBLIC ${MPI_Fortran_INCLUDE_PATH})
target_compile_options(tiogadriver PUBLIC "-cpp")
target_link_libraries(tiogadriver PUBLIC
  tioga ${MPI_Fortran_LIBRARIES} ${CMAKE_DL_LIBS})

add_executable(tioga.exe testTioga.f90)
target_link_libraries(tioga.exe tiogadriver)
set_target_properties(tioga.exe PROPERTIES LINKER_LANGUAGE Fortran)

add_executable(tioga_read.exe testTioga_dataRead.f90)
target_link_libraries(tioga_read.exe tiogadriver)
set_target_properties(tioga_read.exe PROPERTIES LINKER_LANGUAGE Fortran)

add_executable(poisson_mms.exe poisson_mms.f90)
target_link_libraries(poisson_mms.exe tiogadriver)
set_target_properties(poisson_mms.exe PROPERTIES LINKER_LANGUAGE Fortran)

install(TARGETS tiogadriver
  RUNTIME DESTINATION bin
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib)

install(TARGETS tioga.exe
  RUNTIME DESTINATION bin
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib)

install(TARGETS tioga_read.exe
  RUNTIME DESTINATION bin
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib)

install(TARGETS poisson_mms.exe
  RUNTIME DESTINATION bin
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib)
