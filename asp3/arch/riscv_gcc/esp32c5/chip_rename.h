/* This file is generated from chip_rename.def by genrename. */

#ifndef TOPPERS_CHIP_RENAME_H
#define TOPPERS_CHIP_RENAME_H

/*
 *  chip_kernel_impl.c
 */
#define chip_initialize				_kernel_chip_initialize
#define chip_terminate				_kernel_chip_terminate
#define clic_initialize				_kernel_clic_initialize
#define esp32c5_intmtx_route		_kernel_esp32c5_intmtx_route
#define intmtx_srcmask				_kernel_intmtx_srcmask
#define intmtx_from_cpu				_kernel_intmtx_from_cpu

/*
 *  chip_support.S
 */
#define trap_vector_table			_kernel_trap_vector_table
#define irc_begin_int				_kernel_irc_begin_int
#define irc_end_int					_kernel_irc_end_int
#define irc_get_intpri				_kernel_irc_get_intpri
#define irc_begin_exc				_kernel_irc_begin_exc
#define irc_end_exc					_kernel_irc_end_exc


#include "core_rename.h"

#endif /* TOPPERS_CHIP_RENAME_H */
