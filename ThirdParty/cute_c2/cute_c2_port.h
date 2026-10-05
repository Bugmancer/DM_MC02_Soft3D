#ifndef SOFT3D_CUTE_C2_PORT_H
#define SOFT3D_CUTE_C2_PORT_H

/* Keep upstream warning policy separate from the project's strict C checks. */
#if defined(__GNUC__)
#pragma GCC system_header
#endif
#if defined(__CC_ARM)
/* Upstream dispatchers retain break statements after return statements. */
#pragma push
#pragma diag_suppress 111
#endif
#include "cute_c2.h"
#if defined(__CC_ARM)
#pragma pop
#endif

#endif
