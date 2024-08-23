/*
 * Copyright (c) 2021-2024, Arm Limited or its affiliates. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */

#include "val_test_dispatch.h"

#ifndef TARGET_LINUX
extern uint64_t val_image_load_offset;
#else
uint64_t val_image_load_offset = 0;
#endif
mp_test_status_t g_mp_state = {
    .g_other_pe_test_state = {[0 ... PLATFORM_NO_OF_CPUS - 1] = VAL_MP_STATE_WAIT},
    .g_other_pe_test_result = {[0 ... (PLATFORM_NO_OF_CPUS - 1)] = VAL_STATUS_INVALID}
};

extern const uint32_t  total_tests;

/**
 *   @brief    - Handshake with given server endpoint to execute server test fn
 *   @param    - test runtime data such as test_num, client-server ids
 *               participating in the test scenario
 *   @param    - arg4 to arg7 - test specific data
 *   @return   - FFA_MSG_SEND_DIRECT_RESP_32 return args
**/
ffa_args_t val_select_server_fn_direct(uint32_t test_run_data,
                                       uint32_t arg4,
                                       uint32_t arg5,
                                       uint32_t arg6,
                                       uint32_t arg7)
{
    ffa_args_t      payload;
    uint32_t        client_logical_id = GET_CLIENT_LOGIC_ID(test_run_data);
    uint32_t        server_logical_id = GET_SERVER_LOGIC_ID(test_run_data);

    /* Add server_test type */
    test_run_data = ADD_TEST_TYPE(test_run_data, SERVER_TEST);

    /* Release the given server endpoint from wait
     * (in val_wait_for_test_fn_req) so that it can
     * execute given server test fn.
     */
    val_memset(&payload, 0, sizeof(ffa_args_t));
    payload.arg1 = ((uint32_t)val_get_endpoint_id(client_logical_id) << 16) |
                                val_get_endpoint_id(server_logical_id);
    payload.arg3 = test_run_data;
    payload.arg4 = arg4;
    payload.arg5 = arg5;
    payload.arg6 = arg6;
    payload.arg7 = arg7;
    val_ffa_msg_send_direct_req_32(&payload);

    return payload;
}

/**
 *   @brief    - Unblock the client and wait for new message request
 *   @param    - test runtime data such as test_num, client-server ids
 *               participating in the test scenario
 *   @param    - arg4 to arg7 - test specific data
 *   @return   - ffa_msg_send_direct_req_32 return values
**/
ffa_args_t val_resp_client_fn_direct(uint32_t test_run_data,
                                       uint32_t arg3,
                                       uint32_t arg4,
                                       uint32_t arg5,
                                       uint32_t arg6,
                                       uint32_t arg7)
{
    ffa_args_t      payload;
    uint32_t        client_logical_id = GET_CLIENT_LOGIC_ID(test_run_data);
    uint32_t        server_logical_id = GET_SERVER_LOGIC_ID(test_run_data);

    val_memset(&payload, 0, sizeof(ffa_args_t));
    payload.arg1 = ((uint32_t)val_get_endpoint_id(server_logical_id) << 16) |
                                val_get_endpoint_id(client_logical_id);
    payload.arg3 = arg3;
    payload.arg4 = arg4;
    payload.arg5 = arg5;
    payload.arg6 = arg6;
    payload.arg7 = arg7;

    val_ffa_msg_send_direct_resp_32(&payload);
    return payload;
}

/**
 *   @brief    - VAL API to execute test on secondary cpu
 *   @param    - Void
 *   @return   - Void
**/
void val_secondary_cpu_test_entry(void)
{
    uint32_t status = VAL_ERROR;
    uint64_t mpid = val_read_mpidr() & MPID_MASK;
    uint32_t cpu_id = val_get_cpuid(mpid);;

    /* unsused */
    (void)status;
    (void)cpu_id;
    (void)mpid;

    val_sec_cpu_wait_for_test_fn_req();
}

/**
 *   @brief    - Test entry for non-dispatcher Sec CPU endpoints. This executes
 *               client or server test functions based on dispatcher's command
 *   @param    - void
 *   @return   - void
**/
void val_sec_cpu_wait_for_test_fn_req(void)
{
    ffa_args_t        payload;
    uint32_t          status = VAL_ERROR;
    ffa_endpoint_id_t target_id, my_id;

    val_memset(&payload, 0, sizeof(ffa_args_t));

    /* Receive the test_num and client_fn/server_fn to run
     * OR receiver service id for nvm and wd functionality
     */

    val_ffa_msg_wait(&payload);

    while (1)
    {
        if (payload.fid != FFA_MSG_SEND_DIRECT_REQ_32)
        {
            LOG(ERROR, "\tInvalid fid received, fid=0x%x, error=0x%x\n", payload.fid, payload.arg2);
            return;
        }

        target_id = SENDER_ID(payload.arg1);
        my_id = RECEIVER_ID(payload.arg1);

        // TODO Sec CPU Non Primary VM Client handling
        VAL_PANIC("\tNo Support for Sec CPU non Primary VM Client \n");

        /* Send test status back */
        val_memset(&payload, 0, sizeof(ffa_args_t));
        payload.arg1 = ((uint32_t)my_id << 16) | target_id;
        payload.arg3 = status;
        val_ffa_msg_send_direct_resp_32(&payload);
    }
}


/**
 *   @brief    - Retrieve other pe current test status
 *   @param    - mpid , current test number
 *   @return   - test result
**/
uint32_t val_get_multi_pe_test_status(uint64_t mpid, uint32_t test_num)
{
    uint32_t cpu_id = val_get_cpuid(mpid);
    uint32_t mp_test_num = 0;
    uint32_t count = 5;
    uint32_t status = VAL_ERROR;

    val_dataCacheInvalidateVA((uint64_t)&g_mp_state.g_current_test_num);
    mp_test_num = g_mp_state.g_current_test_num;

    if (mp_test_num != test_num)
    {
        VAL_PANIC("\tInconsistent test_num and g_test_num \n");
    }

    /** check and wait for all pe to reach test completion */
    while (1)
    {
        val_dataCacheInvalidateVA((uint64_t)&g_mp_state.g_other_pe_test_state);
        if (g_mp_state.g_other_pe_test_state[cpu_id] != VAL_MP_STATE_INPROGRESS || count == 5)
        {
            break;
        }

        /** sleep for 1ms */
        val_sleep(1);
        count--;
    }

    val_dataCacheInvalidateVA((uint64_t)&g_mp_state.g_other_pe_test_result);
    val_dataCacheInvalidateVA((uint64_t)&g_mp_state.g_other_pe_test_state);

    if (g_mp_state.g_other_pe_test_state[cpu_id] == VAL_MP_STATE_COMPLETE)
    {
        status = g_mp_state.g_other_pe_test_result[cpu_id];

        /* Reset Global MP Test data */
        g_mp_state.g_other_pe_test_state[cpu_id] = VAL_MP_STATE_WAIT;
        g_mp_state.g_other_pe_test_result[cpu_id] = VAL_STATUS_INVALID;
        val_dataCacheInvalidateVA((uint64_t)&g_mp_state.g_other_pe_test_result);
        val_dataCacheInvalidateVA((uint64_t)&g_mp_state.g_other_pe_test_state);
    }
    else
    {
        VAL_PANIC("\tOther-PE test state incomplete \n");
    }

    return status;
}
