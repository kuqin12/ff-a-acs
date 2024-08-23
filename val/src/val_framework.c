/*
 * Copyright (c) 2021-2024, Arm Limited or its affiliates. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */

#include "val_framework.h"
#include "val_interfaces.h"

#if (PLATFORM_SP_EL == -1)
#define SKIP_WD_PROGRAMMING
#define SKIP_NVM_PROGRAMMING
#endif

#ifdef SKIP_NVM_PROGRAMMING
#define NVM_SIZE (1024)
static uint8_t g_nvmem[NVM_SIZE];
#else
#define NVM_SIZE PLATFORM_NVM_SIZE
#endif

extern const uint32_t  total_tests;

/* Global */
test_status_buffer_t    g_status_buffer = {
                            .state       = TEST_FAIL,
                            .status_code = VAL_STATUS_INVALID
                        };

/**
 *   @brief    - This function prints the given string and data onto the uart
 *   @param    - str      : Input String
 *             - data1    : Value for first format specifier
 *             - data2    : Value for second format specifier
 *   @return   - SUCCESS(0)/FAILURE
**/
uint32_t val_printf(const char *msg, uint64_t data1, uint64_t data2)
{
    char s[TOTAL_PRINT_LIMIT] = "";

    if (VERBOSITY <= DBG)
    {
        /* Prefix endpoint name to the string */
        val_strcat(s, (char *)val_get_curr_endpoint_name(), TOTAL_PRINT_LIMIT);
        val_strcat(s, ":  ", TOTAL_PRINT_LIMIT);
        val_strcat(s, (char *)msg, TOTAL_PRINT_LIMIT);
        return pal_printf(s, data1, data2);
    }
    else
    {
        return pal_printf(msg, data1, data2);
    }
}

/**
 *   @brief    - Records the state and status of test
 *   @param    - Test status bit field - (state|status_code)
 *   @return   - void
**/
void val_set_status(uint32_t status)
{
    uint8_t state = ((status >> TEST_STATE_SHIFT) & TEST_STATE_MASK);

    g_status_buffer.status_code  = (status & TEST_STATUS_CODE_MASK);

    if ((g_status_buffer.state == TEST_PASS_WITH_SKIP && state == TEST_PASS) ||
        (g_status_buffer.state == TEST_PASS_WITH_SKIP && state == TEST_SKIP) ||
        (g_status_buffer.state == TEST_PASS && state == TEST_SKIP) ||
        (g_status_buffer.state == TEST_SKIP && state == TEST_PASS))
    {
        g_status_buffer.state = TEST_PASS_WITH_SKIP;
    }
    else
    {
        g_status_buffer.state = state;
    }
}

/**
 *   @brief    - Returns the state and status for a given test
 *   @param    - Void
 *   @return   - test status
**/
uint32_t val_get_status(void)
{
    return (uint32_t)(((g_status_buffer.state) << TEST_STATE_SHIFT) |
            (g_status_buffer.status_code));
}

/**
 * @brief  This API reloads the watchdog timer
 * @param  none
 * @return none
**/
void val_reprogram_watchdog(void)
{
   if (val_watchdog_disable())
   {
      VAL_PANIC("\tWatchdog disable failed\n");
   }
   if (val_watchdog_enable())
   {
      VAL_PANIC("\tWatchdog enable failed\n");
   }
}

/**
 *   @brief    - Check that an nvm access is within the bounds of the nvm
 *   @param    - offset  : Offset into nvm
 *               buffer  : Buffer address
 *               size    : Number of bytes
 *   @return   - SUCCESS/FAILURE
**/
static uint32_t nvm_check_bounds(uint32_t offset, void *buffer, size_t size)
{
    if (buffer == NULL)
        return VAL_ERROR;
    else if (offset > NVM_SIZE)
        return VAL_ERROR;
    else if (offset + size > NVM_SIZE)
        return VAL_ERROR;
    else if (size != sizeof(uint32_t))
        return VAL_ERROR;

    return VAL_SUCCESS;
}

/**
 *    @brief     - Writes 'size' bytes from buffer into non-volatile memory at a given
 *                 'base + offset'.
 *               - offset    : Offset
 *               - buffer    : Pointer to source address
 *               - size      : Number of bytes
 *    @return    - SUCCESS/FAILURE
**/
uint32_t val_nvm_write(uint32_t offset, void *buffer, size_t size)
{
#ifndef SKIP_NVM_PROGRAMMING
   ffa_args_t  payload;
   uint32_t    data32 = *(uint32_t *)buffer;

   if (nvm_check_bounds(offset, buffer, size))
        return VAL_ERROR;

   if (val_get_curr_endpoint_logical_id() == SP1)
   {
      return pal_nvm_write(offset, buffer, size);
   }
   else
   {
      val_memset(&payload, 0, sizeof(ffa_args_t));
      payload.arg1 = ((uint32_t)val_get_curr_endpoint_id() << 16) |
                                  val_get_endpoint_id(SP1);
      payload.arg3 = NVM_WRITE_SERVICE;
      payload.arg4 = offset;
      payload.arg5 = size;
      payload.arg6 = data32;
      val_ffa_msg_send_direct_req_32(&payload);
      if (payload.fid != FFA_MSG_SEND_DIRECT_RESP_32)
      {
         LOG(ERROR, "\tInvalid fid received, fid=0x%x, err=0x%x\n", payload.fid, payload.arg2);
         return VAL_ERROR;
      }

      return VAL_SUCCESS;
   }
#else
    if (nvm_check_bounds(offset, buffer, size))
    {
        return VAL_ERROR;
    }

    val_memcpy(g_nvmem + offset, buffer, size);
    return VAL_SUCCESS;
#endif
}

/**
 *   @brief - Reads 'size' bytes from Non-volatile memory 'base + offset' into given buffer.
 *              - offset    : Offset from NV MEM base address
 *              - buffer    : Pointer to destination address
 *              - size      : Number of bytes
 *   @return    - SUCCESS/FAILURE
**/
uint32_t val_nvm_read(uint32_t offset, void *buffer, size_t size)
{
#ifndef SKIP_NVM_PROGRAMMING
   ffa_args_t  payload;
   if (nvm_check_bounds(offset, buffer, size))
        return VAL_ERROR;

   if (val_get_curr_endpoint_logical_id() == SP1)
   {
      return pal_nvm_read(offset, buffer, size);
   }
   else
   {
      val_memset(&payload, 0, sizeof(ffa_args_t));
      payload.arg1 = ((uint32_t)val_get_curr_endpoint_id() << 16) |
                                  val_get_endpoint_id(SP1);
      payload.arg3 = NVM_READ_SERVICE;
      payload.arg4 = offset;
      payload.arg5 = size;
      val_ffa_msg_send_direct_req_32(&payload);
      if (payload.fid != FFA_MSG_SEND_DIRECT_RESP_32)
      {
         LOG(ERROR, "\tInvalid fid received, fid=0x%x, err=0x%x\n", payload.fid, payload.arg2);
         return VAL_ERROR;
      }

      *(uint32_t *)buffer = (uint32_t)payload.arg3;
      return VAL_SUCCESS;
   }
#else
    if (nvm_check_bounds(offset, buffer, size))
    {
        return VAL_ERROR;
    }

    val_memcpy(buffer, g_nvmem + offset, size);
    return VAL_SUCCESS;
#endif
}

/**
 *   @brief    - Initializes and enable the hardware watchdog timer
 *   @param    - void
 *   @return   - SUCCESS/FAILURE
**/
uint32_t val_watchdog_enable(void)
{
#ifndef SKIP_WD_PROGRAMMING
   ffa_args_t  payload;

   if (val_get_curr_endpoint_logical_id() == SP1)
   {
      return pal_watchdog_enable();
   }
   else
   {
      val_memset(&payload, 0, sizeof(ffa_args_t));
      payload.arg1 = ((uint32_t)val_get_curr_endpoint_id() << 16) |
                                  val_get_endpoint_id(SP1);
      payload.arg3 = WD_ENABLE_SERVICE;
      val_ffa_msg_send_direct_req_32(&payload);
      if (payload.fid != FFA_MSG_SEND_DIRECT_RESP_32)
      {
         LOG(ERROR, "\tInvalid fid received, fid=0x%x, err=0x%x\n", payload.fid, payload.arg2);
         return VAL_ERROR;
      }

      return VAL_SUCCESS;
   }
#else
    return VAL_SUCCESS;
#endif
}

/**
 *   @brief    - Disables the hardware watchdog timer
 *   @param    - void
 *   @return   - SUCCESS/FAILURE
**/
uint32_t val_watchdog_disable(void)
{
#ifndef SKIP_WD_PROGRAMMING
   ffa_args_t  payload;
   uint64_t ep_info;

   if (val_get_curr_endpoint_logical_id() == SP1)
   {
      return pal_watchdog_disable();
   }
   else
   {
      val_memset(&payload, 0, sizeof(ffa_args_t));
      payload.arg1 = ((uint32_t)val_get_curr_endpoint_id() << 16) |
                                  val_get_endpoint_id(SP1);
      payload.arg3 = WD_DISABLE_SERVICE;
      val_ffa_msg_send_direct_req_32(&payload);
      while (payload.fid == FFA_INTERRUPT_32)
      {
          ep_info = payload.arg1;
          val_memset(&payload, 0, sizeof(ffa_args_t));
          payload.arg1 = ep_info;
          val_ffa_run(&payload);
      }
      if (payload.fid != FFA_MSG_SEND_DIRECT_RESP_32)
      {
         LOG(ERROR, "\tInvalid fid received, fid=0x%x, err=0x%x\n", payload.fid, payload.arg2);
         return VAL_ERROR;
      }

      return VAL_SUCCESS;
   }
#else
    return VAL_SUCCESS;
#endif
}

uint32_t val_smmu_device_configure(uint32_t stream_id, uint64_t source, uint64_t dest,
                                     uint64_t size, bool secure)
{
    return pal_smmu_device_configure(stream_id, source, dest, size, secure);
}

/**
 *   @brief    - This function notifies the framework about test
 *               intension of rebooting the platform. Test returns
 *               to framework on reset.
 *   @param    - Void
 *   @return   - Void
**/
void val_set_reboot_flag(void)
{
   uint32_t test_progress  = TEST_REBOOTING;

   LOG(INFO, "\tSetting reboot flag\n", 0, 0);
   if (val_nvm_write(VAL_NVM_OFFSET(NVM_TEST_PROGRESS_INDEX),
                 &test_progress, sizeof(uint32_t)))
   {
      VAL_PANIC("\tnvm write failed\n");
   }
}

/**
 *   @brief    - This function notifies the framework about test
 *               intension of not rebooting the platform.
 *   @param    - Void
 *   @return   - Void
**/
void val_reset_reboot_flag(void)
{
   uint32_t test_progress  = TEST_FAIL;

   LOG(INFO, "\tResetting reboot flag\n", 0, 0);
   if (val_nvm_write(VAL_NVM_OFFSET(NVM_TEST_PROGRESS_INDEX),
                 &test_progress, sizeof(uint32_t)))
   {
      VAL_PANIC("\tnvm write failed\n");
   }
}
