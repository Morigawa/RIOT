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
 * @brief       Implementation of OpenThread main functions
 *
 * @author      Jose Ignacio Alamos <jialamos@uc.cl>
 * @}
 */

#include "ot.h"
#include "random.h"
#include "thread.h"

#ifdef MODULE_AT86RF2XX
#include "at86rf2xx.h"
#include "at86rf2xx_params.h"
#endif

#ifdef MODULE_KW41ZRF
#include "kw41zrf.h"
#endif

#ifdef MODULE_CC2538_RF
#include "cc2538_rf.h"
#endif

#ifdef MODULE_NRF802154
#include "nrf802154.h"
#endif

// #if IS_USED(MODULE_NETDEV_IEEE802154_SUBMAC)
// #include "net/netdev/ieee802154_submac.h"
// #endif

#define ENABLE_DEBUG 0
#include "debug.h"

static ieee802154_dev_t dev;

static uint8_t rx_buf[OPENTHREAD_NETDEV_BUFLEN];
static uint8_t tx_buf[OPENTHREAD_NETDEV_BUFLEN];
static char ot_thread_stack[2 * THREAD_STACKSIZE_MAIN];

void openthread_bootstrap(void)
{
    /* setup netdev modules */
// #ifdef MODULE_AT86RF2XX
//     at86rf2xx_setup(&at86rf2xx_dev, &at86rf2xx_params[0], 0);
//     netdev_t *netdev = &at86rf2xx_dev.netdev.netdev;
// #endif
// #ifdef MODULE_KW41ZRF
//     kw41zrf_setup(&kw41z_dev, 0);
//     netdev_t *netdev = &kw41z_dev.netdev.netdev;
// #endif
// #ifdef MODULE_CC2538_RF
//     cc2538_rf_hal_setup(&cc2538_rf_netdev.submac.dev);
//     cc2538_init();
//     netdev_t *netdev = &cc2538_rf_netdev.dev.netdev;
// #endif
#ifdef MODULE_NRF802154
    nrf802154_hal_setup(&dev);
    nrf802154_init();
#endif

    if (openthread_radio_init(&dev, tx_buf, rx_buf) < 0) {
        printf("Failed to initialize Radio");
    }
    openthread_hal_init(ot_thread_stack, sizeof(ot_thread_stack), THREAD_PRIORITY_MAIN - 5, "openthread", &dev);
}
