/*
 * Copyright (C) 2017 Fundacion Inria Chile
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

/**
 * @{
 * @ingroup     net
 * @file
 * @brief       Radio HAL adoption for OpenThread
 *
 * @author      Jose Ignacio Alamos <jialamos@uc.cl>
 * @author      Baptiste Clenet <bapclenet@gmail.com>
 * @author      Moritz Voigt <moritz.voigt@mailbox.tu-dresden.de>
 * @}
 */

#define OT_NETWORK_KEY_SIZE 16

#include <ctype.h>
#include <errno.h>
#include <string.h>
#include "msg.h"
#include "openthread/dataset_ftd.h" // TODO only if ftd
#include "openthread/instance.h"
#include "openthread/ip6.h"
#include "openthread/platform/alarm-milli.h"
#include "openthread/thread.h"
#include "openthread/dataset_ftd.h" // TODO only if ftd
#include "random.h"
#include "ot.h"
#include "event.h"

#define ENABLE_DEBUG 0
#include "debug.h"

static otInstance *sInstance;   /**< global OpenThread instance */
static ieee802154_dev_t *_dev;  /**< radio hal descriptor for OpenThread */
static event_queue_t ev_queue;  /**< the event queue for OpenThread */

static int bytes_from_str(uint8_t *buf, int buf_len, const char *src)
{
    size_t i;
    size_t src_len = strlen(src);
    char *endptr;

    for (i = 0U; i < src_len; i++) {
        if (!isxdigit((unsigned char)src[i]) &&
            src[i] != ':') {
            return -EINVAL;
        }
    }

    (void)memset(buf, 0, buf_len);

    for (i = 0U; i < (size_t)buf_len; i++) {
        buf[i] = (uint8_t)strtol(src, &endptr, 16);
        src = ++endptr;
    }

    return 0;
}

static void _ev_recv_handler(event_t *event)
{
    (void)event;
    recv_pkt(sInstance);
}

static event_t ev_recv = {
    .handler = _ev_recv_handler
};

static void _ev_process_tx_done_handler(event_t *event)
{
    (void)event;
    process_tx_done(sInstance);
}

static event_t _ev_process_tx_done = {
    .handler = _ev_process_tx_done_handler
};

event_queue_t *openthread_get_evq(void)
{
    return &ev_queue;
}

otInstance *openthread_get_instance(void)
{
    return sInstance;
}

static void _hal_radio_cb(ieee802154_dev_t *dev, ieee802154_trx_ev_t status)
{
    /* What about start indications esp. TxStarted */
    switch (status) {
    case IEEE802154_RADIO_CONFIRM_TX_DONE:
        event_post(&ev_queue, &_ev_process_tx_done);
        break;
    case IEEE802154_RADIO_INDICATION_CRC_ERROR:
        /* Just drop the packet */
        while (ieee802154_radio_set_idle(dev, false) < 0) {}
        ieee802154_radio_read(dev, NULL, 0, NULL);
        /* TODO: status change necessary? Dependent on previous state */
        break;
    case IEEE802154_RADIO_INDICATION_RX_DONE:
        event_post(&ev_queue, &ev_recv);
        break;
    default:
        break;
    }
}

static void _openthread_manual_config(otInstance *sInstance)
{
    /* Init default parameters */
    otPanId panid = OPENTHREAD_PANID;
    // char *extpanid = "de:ad:00:be:ef:00:ca:fe";
    uint8_t channel = OPENTHREAD_CHANNEL;
    char *networkkey = OPENTHREAD_NETWORK_KEY;
    char *meshprefix = "fd:05:77:bd:d2:c1:da:be";
    char *networkname = "OT-nrf1";

    otThreadSetNetworkName(sInstance, networkname);
    otLinkSetChannel(sInstance, channel);
    otLinkSetPanId(sInstance, panid);

    otNetworkKey otNetKey;
    otMeshLocalPrefix otMeshLocalPrefix;
    bytes_from_str(otNetKey.m8, OT_NETWORK_KEY_SIZE, networkkey);
    bytes_from_str(otMeshLocalPrefix.m8, OT_MESH_LOCAL_PREFIX_SIZE, meshprefix);
    // bytes_from_str(dataset.mExtendedPanId.m8, OT_EXT_PAN_ID_SIZE, extpanid);

    otThreadSetNetworkKey(sInstance, &otNetKey);
    otThreadSetMeshLocalPrefix(sInstance, &otMeshLocalPrefix);
}

static void *_openthread_event_loop(void *arg)
{
    otError error;

    _dev = arg;
    _dev->cb = _hal_radio_cb;

    event_queue_init(&ev_queue);

    /* Init OpenThread Instance */
    sInstance = otInstanceInitSingle();

#if defined(MODULE_OPENTHREAD_CLI_FTD) || defined(MODULE_OPENTHREAD_CLI_MTD)
    ot_shell_init(sInstance);
#endif

    /* Bring up the IPv6 interface  */
    error = otIp6SetEnabled(sInstance, true);

    /* Manually configure OpenThread requires -DOT_OPERATIONAL_DATASET_AUTO_INIT=1 */
    _openthread_manual_config(sInstance);

    /* Start Thread protocol operation */
    error = otThreadSetEnabled(sInstance, true);
    if (error != OT_ERROR_NONE) {
        printf("pkg/openthread: Error in initialization\n");
    }

#if OPENTHREAD_ENABLE_DIAG
    diagInit(sInstance);
#endif

    while (1) {
        event_loop(&ev_queue);
    }

    return NULL;
}

/* starts OpenThread thread */
int openthread_hal_init(char *stack, int stacksize, char priority,
                        const char *name, ieee802154_dev_t *dev)
{
    if (thread_create(stack, stacksize,
                      priority, 0,
                      _openthread_event_loop, dev, name) < 0) {
        return -EINVAL;
    }

    return 0;
}
