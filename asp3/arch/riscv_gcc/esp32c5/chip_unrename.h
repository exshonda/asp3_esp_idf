/* This file is generated from chip_rename.def by genrename. */

/* This file is included only when chip_rename.h has been included. */
#ifdef TOPPERS_CHIP_RENAME_H
#undef TOPPERS_CHIP_RENAME_H

/*
 *  chip_kernel_impl.c
 */
#undef chip_initialize
#undef chip_terminate
#undef clic_initialize
#undef esp32c5_intmtx_route
#undef intmtx_srcmask
#undef intmtx_from_cpu

/*
 *  chip_support.S
 */
#undef trap_vector_table
#undef irc_begin_int
#undef irc_end_int
#undef irc_get_intpri
#undef irc_begin_exc
#undef irc_end_exc


#include "core_unrename.h"

#endif /* TOPPERS_CHIP_RENAME_H */
