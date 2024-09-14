/*
 * Copyright (c) 2021-2024, Arm Limited or its affiliates. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */

#include "val_framework.h"
#include "val_memory.h"
#include "val_ffa.h"
#include "val_irq.h"
#include "val_ffa_helpers.h"
#include "ArmPL610Gpio.h"

volatile bool loop = true;

static int npi_irq_handler(void)
{
	LOG(ALWAYS, "GPIO interrupt triggered\n", 0, 0);
	return 0;
}

/**
 *   @brief    - C entry function for endpoint
 *   @param    - void
 *   @return   - void (Never returns)
**/
void sp_main(void)
{
	ffa_args_t        payload;
	ffa_endpoint_id_t target_id, my_id;

	LOG(ALWAYS, "Entering main loop 9.. \n", 0, 0);

	// Intended Feature 1: reserved memory access
	for (uint64_t i = 0x000001001FFFE000; i < 0x000001001FFFF000; i+=sizeof(uint32_t))
	{
			LOG(ALWAYS, "%x ", (*(uint32_t*)i), 0);
	}
	LOG(ALWAYS, "\t\n", 0, 0);

	val_ffa_msg_wait(&payload);

	// print the incoming message
	uint16_t sender = SENDER_ID(payload.arg1);
	uint16_t receiver = RECEIVER_ID(payload.arg1);
	LOG(ALWAYS, "\tReceived message fid=0x%x, sender=0x%x\n", payload.fid, sender);
	LOG(ALWAYS, "\tReceived message receiver=0x%x, 0x%x\n", receiver, payload.arg1);

	target_id = sender;
	my_id = receiver;

	val_memset(&payload, 0, sizeof(ffa_args_t));
	payload.arg1 = ((uint32_t)my_id << 16) | target_id;
	payload.arg2 = 0;
	payload.arg3 = 0;
	payload.arg4 = 0xdeadbeef;
	val_ffa_msg_send_direct_resp_64(&payload);

	// print the incoming message
	sender = SENDER_ID(payload.arg1);
	receiver = RECEIVER_ID(payload.arg1);
	LOG(ALWAYS, "\tReceived 2nd message fid=0x%x, sender=0x%x\n", payload.fid, sender);
	LOG(ALWAYS, "\tReceived 2nd message receiver=0x%x, 0x%x\n", receiver, payload.arg1);

	target_id = sender;
	my_id = receiver;

	val_memset(&payload, 0, sizeof(ffa_args_t));
	payload.arg1 = ((uint32_t)my_id << 16) | target_id;
	payload.arg2 = 0;
	payload.arg3 = 0;
	payload.arg4 = 0xfeedf00d;
	val_ffa_msg_send_direct_resp2_64(&payload);

	// print the incoming message
	sender = SENDER_ID(payload.arg1);
	receiver = RECEIVER_ID(payload.arg1);
	LOG(ALWAYS, "\tReceived 3rd message fid=0x%x, sender=0x%x\n", payload.fid, sender);
	LOG(ALWAYS, "\tReceived 3rd message receiver=0x%x, 0x%x\n", receiver, payload.arg1);

	// Intended Feature 2: Register interrupt handler for the intended GPIO

	// Let's do GPIO_2 for now
	// GPIO_2 is mapped to 0x2
	uint32_t gpio_num = 0x2;

	// Program GPIOIDR to make sure it is set to input.
	uint32_t gpioidr = pal_mmio_read32 (GPIO_BASE + GPIODIR_OFFSET);
	gpioidr |= (uint32_t)(1 << gpio_num);
	pal_mmio_write32(GPIO_BASE + GPIODIR_OFFSET, gpioidr);

	// Program PADDR 
	uint32_t paddr = pal_mmio_read32 (GPIO_BASE + (uint32_t)(1 << (gpio_num + PADDR_9_2_OFFSET)));
	paddr &= (uint32_t)(~(1 << gpio_num));

	// Program GPIOIBE to make sure this is not triggering on both edges.
	uint32_t gpioibe = pal_mmio_read32 (GPIO_BASE + GPIOIBE_OFFSET);
	gpioibe &= (uint32_t)(~(1 << gpio_num));
	pal_mmio_write32(GPIO_BASE + GPIOIBE_OFFSET, gpioibe);

	// Program GPIOIEV to make sure this is rising edge trigger.
	uint32_t gpioiev = pal_mmio_read32 (GPIO_BASE + GPIOIEV_OFFSET);
	gpioiev |= (1 << gpio_num);
	pal_mmio_write32(GPIO_BASE + GPIOIEV_OFFSET, gpioiev);

	// Program GPIOIS to make sure this is edge trigger.
	uint32_t gpiois = pal_mmio_read32 (GPIO_BASE + GPIOIS_OFFSET);
	gpiois &= (uint32_t)(~(1 << gpio_num));
	pal_mmio_write32(GPIO_BASE + GPIOIS_OFFSET, gpiois);

	// Program GPIOC to clear its Interrupt.
	uint32_t gpioc = pal_mmio_read32 (GPIO_BASE + GPIOC_OFFSET);
	gpioc &= (uint32_t)(~(1 << gpio_num));
	pal_mmio_write32(GPIO_BASE + GPIOC_OFFSET, gpioc);

	// Program GPIOIE to make sure this is enabled.
	uint32_t gpioie = pal_mmio_read32 (GPIO_BASE + GPIOIE_OFFSET);
	gpioie |= (1 << gpio_num);
	pal_mmio_write32(GPIO_BASE + GPIOIE_OFFSET, gpioie);

	// Read back GPIOIE to make sure it is enabled.
	gpioie = pal_mmio_read32 (GPIO_BASE + GPIOIE_OFFSET);
	if ((gpioie & (1 << gpio_num)) == 0)
	{
		LOG(ALWAYS, "Failed to enable GPIO interrupt", 0, 0);
		while (loop) {}
	}

	uint32_t irq_num = 0x7;
	int ret = val_irq_register_handler(irq_num, npi_irq_handler);
	if (ret != 0)
	{
		LOG(ALWAYS, "Failed to register GPIO interrupt handler", 0, 0);
		while (loop) {}
	}

	LOG(ALWAYS, "GPIO interrupt registered", 0, 0);

	// Intended Feature 3: send notification to the VM0
	// send a message to whomever sent the message
	val_memset(&payload, 0, sizeof(ffa_args_t));
	payload.arg1 =  ((uint32_t)receiver << 16) | sender;
	payload.arg2 = FFA_NOTIFICATIONS_FLAG_PER_VCPU;
	payload.arg3 = 0x1; // BIT0 for our VCPU bitmap

	val_ffa_notification_set(&payload);
	if (payload.fid == FFA_ERROR_32)
	{
		LOG(ALWAYS, "Failed notification set err %x", payload.arg2, 0);
		while (loop) {}
	}

	while (1) {
		val_memset(&payload, 0, sizeof(ffa_args_t));
		val_ffa_msg_wait(&payload);

		if (payload.fid != FFA_MSG_SEND_DIRECT_REQ_64)
		{
				LOG(ALWAYS, "\tInvalid fid received, fid=0x%x, error=0x%x\n", payload.fid, payload.arg2);
				while (loop) {}
		}

		target_id = sender;
		my_id = receiver;

		val_memset(&payload, 0, sizeof(ffa_args_t));
		payload.arg1 = ((uint32_t)my_id << 16) | target_id;
		payload.arg3 = VAL_ERROR;
		val_ffa_msg_send_direct_resp2_64(&payload);
	}
}
