/*****************************************************************************
 * dsp1.h
 *****************************************************************************/

#ifndef __DSP1_H__
#define __DSP1_H__

// general includes
#include <stdio.h>     /* Get declaration of puts and definition of NULL. */
#include <stdint.h>    /* Get definition of uint32_t. */
#include <assert.h>    /* Get the definition of support for standard C asserts. */
#include <builtins.h>  /* Get definitions of compiler built-in functions */
#include <sys/platform.h>
#include <processor_include.h>	   /* Get definitions of the part being built*/
#include <services/int/adi_int.h>  /* Interrupt HAndler API header. */
#include "adi_initialize.h"

// includes for hardware-pins
#include <SRU.h>
#include <sysreg.h>
#include <signal.h>

// own includes
#include "system.h"

#endif /* __DSP1_H__ */
