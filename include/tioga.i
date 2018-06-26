%module tioga

// -----------------------------------------------------------------------------
// Header files required by any of the following C++ code
// -----------------------------------------------------------------------------
%header
%{
#include <mpi.h>
#include "tiogaInterface.h"
%}

// -----------------------------------------------------------------------------
// Header files and other declarations to be parsed as SWIG input
// -----------------------------------------------------------------------------

// SWIG interface file for MPI, and typemap for MPI_Comm to Python Comm
%include mpi4py/mpi4py.i
%mpi4py_typemap(Comm,MPI_Comm);

%pythoncallback;
// Functions declared here will be able to act like (C-style) function pointers
void tioga_dataupdate_ab(int nvar, int gradFlag);
void tioga_dataupdate_ab_send(int nvar, int gradFlag = 0);
void tioga_dataupdate_ab_recv(int nvar, int gradFlag = 0);
void tioga_preprocess_grids_(void);
void tioga_performconnectivity_(void);
void tioga_do_point_connectivity(void);
void tioga_set_transform(double *mat, double *off, int ndim);
void tioga_unblank_part_1(void);
void tioga_unblank_part_2(int nvar);
%nopythoncallback;

%ignore tioga_dataupdate_ab;
%ignore tioga_dataupdate_ab_send;
%ignore tioga_dataupdate_ab_recv;
%ignore tioga_preprocess_grids_;
%ignore tioga_performconnectivity_;
%ignore tioga_do_point_connectivity;
%ignore tioga_set_transform;
%ignore tioga_unblank_part_1;
%ignore tioga_unblank_part_2;
%include "tiogaInterface.h"

// <-- Additional C++ declations [anything that would normally go in a header]

// -----------------------------------------------------------------------------
// Additional functions which have been declared, but not defined (including
// definition in other source files which will be linked in later)
// -----------------------------------------------------------------------------

%inline
%{
// <-- Additional C++ definitions [anything that would normally go in a .cpp]
%}

// -----------------------------------------------------------------------------
// Additional Python functions to add to module
// [can use any functions/variables declared above]
// -----------------------------------------------------------------------------

%pythoncode
%{
# Python functions here
%}
