TEMPLATE = library
CONFIG -= app_bundle
CONFIG -= qt
DEFINES = _GPU

SOURCES += main.cpp \
    ADT.C \
    bookKeeping.C \
    CartBlock.C \
    CartGrid.C \
    cartOps.C \
    checkContainment.C \
    dataUpdate.C \
    exchangeAMRDonors.C \
    exchangeBoxes.C \
    exchangeDonors.C \
    exchangeSearchData.C \
    funcs.cpp \
    getCartReceptors.C \
    highOrder.C \
    highOrder_kernels.cu \
    holeMap.C \
    MeshBlock.C \
    parallelComm.C \
    points.cpp \
    search.C \
    searchADTrecursion.C \
    tioga.C \
    tiogaInterface.C \
    writeOutput.C \
    buildADTrecursion.c \
    computeCellVolume.c \
    get_amr_index_xyz.c \
    linklist.c \
    math.c \
    test_amr_functions.c \
    utils.c \
    math.C \
    utils.C

DISTFILES += \
    cellVolume.f90 \
    computeCellVolume.f90 \
    findline \
    kaiser.f \
    median.F90 \
    tioga.pro.user \
    writeqnode.f90 \
    makefile \

HEADERS += \
    ADT.h \
    CartBlock.h \
    CartGrid.h \
    codetypes.h \
    error.hpp \
    funcs.hpp \
    globals.h \
    MeshBlock.h \
    parallelComm.h \
    points.hpp \
    tioga.h \
    cuda_funcs.h \
    tiogaInterface.h

