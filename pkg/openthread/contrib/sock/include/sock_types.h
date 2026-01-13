/*
 * SPDX-FileCopyrightText: 2025 TU Dresden
 * SPDX-License-Identifier: LGPL-2.1-only
 */

#pragma once

/**
 * @brief   Default size for OT_sock_reg_t::mbox_queue (as exponent of 2^n).
 *
 *          As the queue size ALWAYS needs to be power of two, this option
 *          represents the exponent of 2^n, which will be used as the size of
 *          the queue.
 */
#ifndef CONFIG_OT_SOCK_MBOX_SIZE_EXP
#define CONFIG_OT_SOCK_MBOX_SIZE_EXP      (3)
#endif
/** @} */

/**
 * @brief Size for OT_sock_reg_t::mbox_queue
 */
#ifndef OT_SOCK_MBOX_SIZE
#define OT_SOCK_MBOX_SIZE  (1 << CONFIG_OT_SOCK_MBOX_SIZE_EXP)
#endif

/**
 * @{
 *
 * @file
 * @brief   OpenThread-specific types and function definitions
 *
 * @author  Moritz Voigt <moritz.voigt@mailbox.tu-dresden.de>
 */

#include "net/sock/udp.h"
#include "openthread/udp.h"

/**
 * @brief   UDP sock type
 * @internal
 */
struct sock_udp {
    otUdpSocket ot_udp_sock;
    mbox_t mbox;
    msg_t mbox_queue[OT_SOCK_MBOX_SIZE];
    // maybe add instance and ot event queue?
};

/** @} */
