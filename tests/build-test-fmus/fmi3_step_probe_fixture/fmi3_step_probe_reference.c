#include "fmi3Functions.h"

/* Rename just the reference definition after the header declares the FMI export. */
#undef fmi3DoStep
#define fmi3DoStep reference_fmi3DoStep

#include "fmi3Functions.c"
