#ifndef SOFTSIM_STRUCT_H
#define SOFTSIM_STRUCT_H

#include  "sw_types.h"
#include  "softsim_porting.h"

typedef struct
{
    U8   	     ref_count;
    U16 	     msg_len;			/* LOCAL_PARA_HDR */

	SoftsimEvent_st  event;
} softsim_msg_struct;

#endif	// SOFTSIM_STRUCT_H


