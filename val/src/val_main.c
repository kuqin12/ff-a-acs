/*
 * Copyright (c) 2021-2024, Arm Limited or its affiliates. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */

#include "val_framework.h"
#include "val_memory.h"
#include "val_ffa.h"

/* Stack memory */
__attribute__ ((aligned (4096))) uint8_t val_stack[STACK_SIZE];
volatile bool loop1 = true;
/**
 *   @brief    - C entry function for endpoint
 *   @param    - void
 *   @return   - void (Never returns)
**/
void sp_main(void)
{
	ffa_args_t        payload;
	ffa_endpoint_id_t target_id, my_id;

	LOG(ALWAYS, "Entering main loop 1.. \n", 0, 0);

	val_ffa_msg_wait(&payload);

	while (1) {
		if (payload.fid != FFA_MSG_SEND_DIRECT_REQ_64)
		{
				LOG(ERROR, "\tInvalid fid received, fid=0x%x, error=0x%x\n", payload.fid, payload.arg2);
				return;
		}

		target_id = SENDER_ID(payload.arg1);
		my_id = RECEIVER_ID(payload.arg1);

		val_memset(&payload, 0, sizeof(ffa_args_t));
		payload.arg1 = ((uint32_t)my_id << 16) | target_id;
		payload.arg3 = VAL_ERROR;
		val_ffa_msg_send_direct_resp_64(&payload);
	}
}
