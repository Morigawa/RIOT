/*
 * SPDX-FileCopyrightText: 2026 TU Dresden
 * SPDX-License-Identifier: LGPL-2.1-only
 */

/**
 * @{
 *
 * @file
 * @brief       Openthread implementation of @ref net_sock_udp
 *
 * @author  Moritz Voigt <moritz.voigt@mailbox.tu-dresden.de>
 */

#include <stdio.h>

#include "ot.h"

#include "iolist.h"
#include "net/sock/udp.h"
#include "memarray.h"

#include "openthread/instance.h"
#include "openthread/ip6.h"
#include "openthread/udp.h"

#define MESSAGE_PTR 0
#define MESSAGE_INFO_PTR 1

#if IS_USED(MODULE_ZTIMER_USEC) || IS_USED(MODULE_ZTIMER_MSEC)
#  include "ztimer.h"
#endif

typedef struct {
    event_t super;
    sock_udp_t* sock;
    const sock_udp_ep_t* local;
    const sock_udp_ep_t* remote;
} socket_event_t;

typedef struct {
    event_t super;
    sock_udp_t* sock;
    const sock_udp_ep_t* remote;
    const iolist_t* snips;
    sock_udp_aux_tx_t* aux;
} send_event_t;

typedef struct {
    event_t super;
    otMessage* message_ptr;
    void* data;
    size_t max_len;
    uint16_t* packet_len;
} read_message_event_t;

typedef struct {
    otMessage* msg;
    otMessageInfo msg_info;
} ot_message_t;

// Keeps OpenThread Messages in buffer, after they are destroyed by OpenThread
memarray_t ot_messages_memarray;
ot_message_t ot_messages_buf[OT_SOCK_MBOX_SIZE];

// static int _str_from_ipv6_array(const uint8_t* array, char* address)
// {
//     // Loop over the array and convert each byte to hex, inserting a colon after every two bytes
//     for (int i = 0; i < 16; i++) {
//         if (i > 0 && i % 2 == 0) {
//             *address++ = ':';
//         }
//         // Format each byte as two hex digits and store it in the string
//         sprintf(address, "%02x", *array);
//         address += 2; // Move the pointer forward by 2 (for the two hex digits)
//         array++;
//     }
//     *address = '\0'; // Null-terminate the strings

//     return 0;
// }

static void _send_udp_message_handler(event_t *event)
{
    otInstance* instance = openthread_get_instance();
    send_event_t* send_event = container_of(event, send_event_t, super);
    otMessageInfo message_info;
    otMessage* message;

    // Set destination data
    memset(&message_info, 0, sizeof(message_info));
    memcpy(&message_info.mPeerAddr.mFields, send_event->remote->addr.ipv6, 16*sizeof(uint8_t));
    message_info.mPeerPort = send_event->remote->port;

    // Generate new OpenThread Udp Message
    message = otUdpNewMessage(instance,NULL);

    size_t payload_bytes = iolist_size(send_event->snips);
    char msg_buffer[payload_bytes];
    iolist_to_buffer(send_event->snips, &msg_buffer, payload_bytes);
 
    otError error = otMessageAppend(message, &msg_buffer, payload_bytes);
    // printf("Length of message after append: %d Length of message before append %d\n", otMessageGetLength(message),payload_bytes);
    // printf("Error: %s\n", otThreadErrorToString(error));  
    //maybe skip?
    if (error != OT_ERROR_NONE) {
        otMessageFree(message);
        return;
    }
 
    error = otUdpSend(instance, &send_event->sock->ot_udp_sock, message, &message_info);
    if (error != OT_ERROR_NONE) {
        otMessageFree(message);
    }
}

static void _read_message_handler(event_t *event)
{
    read_message_event_t* read_message_event = container_of(event, read_message_event_t, super);
    otMessage* msg = read_message_event->message_ptr;

    uint16_t packet_len = otMessageGetLength(msg);
    *read_message_event->packet_len = (packet_len > read_message_event->max_len) ? read_message_event->max_len : packet_len;
    otMessageRead(msg, otMessageGetOffset(msg), read_message_event->data, *read_message_event->packet_len);
    otMessageFree(msg);
}

static void _handle_udp_receive(void *context, otMessage* message, const otMessageInfo* message_info)
{
    (void) message_info;
    sock_udp_t* sock = context;

    // Allocate buffer for message pointer and otMessageInfo in RIOT buffer Pool
    ot_message_t* msg_buf = memarray_alloc(&ot_messages_memarray);
    if(msg_buf == NULL) return; // Log error no buf

    msg_buf->msg = otMessageClone(message);
    msg_buf->msg_info = *message_info;

    msg_t msg = {
        .content = {
            .ptr = msg_buf,
        }
    };
    mbox_try_put(&sock->mbox, &msg);
}

static void _create_udp_socket_handler(event_t *event)
{
    otError error = 0;
    otInstance* instance = openthread_get_instance();

    socket_event_t* socket_event = container_of(event, socket_event_t, super);
    otUdpSocket* ot_udp_sock = &socket_event->sock->ot_udp_sock;
    const sock_udp_ep_t* local = socket_event->local;
    const sock_udp_ep_t* remote = socket_event->remote;

    // Determine local endpoint
    otIp6Address ot_local_ip;
    // char local_addr_str[40];
    // memset(&local_addr_str, 0, sizeof(*local_addr_str));
    // _str_from_ipv6_array(local->addr.ipv6, local_addr_str);
    // otIp6AddressFromString(local_addr_str, &ot_local_ip);
    memcpy(&ot_local_ip.mFields, local->addr.ipv6, 16*sizeof(uint8_t));
    otSockAddr ot_local_ep = {ot_local_ip, local->port};

    // Open socket
    error = otUdpOpen(instance, ot_udp_sock, _handle_udp_receive, socket_event->sock);
    if(error != 0) return;
    puts("UDP socket succesfully opened");    
   
    // Bind socket
    otNetifIdentifier netif = OT_NETIF_UNSPECIFIED;
    error = otUdpBind(instance, ot_udp_sock, &ot_local_ep, netif);
    if(error != 0) return;
    printf("UDP socket succesfully bound at port %d\n", ot_local_ep.mPort);

    // Set remote endpoint, if existent
    if (remote != NULL) {
        
        otIp6Address ot_remote_ip;
        // char remote_addr_str[40];
        // memset(&remote_addr_str, 0, sizeof(*remote_addr_str));
        // _str_from_ipv6_array(remote->addr.ipv6, remote_addr_str);
        // otIp6AddressFromString(remote_addr_str, &ot_remote_ip);
        memcpy(&ot_local_ip.mFields, local->addr.ipv6, 16*sizeof(uint8_t));
        otSockAddr ot_remote_ep = {ot_remote_ip, remote->port};

        ot_udp_sock->mPeerName = ot_remote_ep;
    }
}

static void _close_udp_socket_handler(event_t *event)
{
    otError error = 0;
    otInstance* instance = openthread_get_instance();

    socket_event_t* socket_event = container_of(event, socket_event_t, super);
    otUdpSocket* socket = &socket_event->sock->ot_udp_sock;
    // remove after debug
    uint16_t port = socket->mSockName.mPort;

    error = otUdpClose(instance, socket);
    if(error != 0) return;
    printf("UDP socket successfully closed at port %d\n", port);
}

/**
 * @brief Converts a RIOT IPv6 address array into an OpenThread binary representation.
 * 
 * @param[in] array    The uint16_t array.
 * @param[out] address A pointer to an initilized char*.
 */

int sock_udp_create(sock_udp_t *sock, const sock_udp_ep_t *local,
                    const sock_udp_ep_t *remote, uint16_t flags)
{
    (void) flags;

    assert(sock);
    assert(remote == NULL || remote->port != 0);
    if ((local != NULL) && (remote != NULL) &&
        (local->netif != SOCK_ADDR_ANY_NETIF) &&
        (remote->netif != SOCK_ADDR_ANY_NETIF) &&
        (local->netif != remote->netif)) {
        return -EINVAL;
    }

    memarray_init(&ot_messages_memarray, &ot_messages_buf, sizeof(ot_message_t), OT_SOCK_MBOX_SIZE);

    // check and translate address and port of local endpoint
    if (local == NULL || local->port == 0) return -EINVAL;

    memset(sock, 0, sizeof(*sock));

    mbox_init(&sock->mbox, sock->mbox_queue, OT_SOCK_MBOX_SIZE);

    socket_event_t event_create_udp_socket = {
        .super.handler = _create_udp_socket_handler,
        .sock = sock,
        .local = local,
        .remote = remote,
    };

    event_queue_t* ot_evq = openthread_get_evq();
    event_post(ot_evq, &event_create_udp_socket.super);

    return 0;
}

void sock_udp_close(sock_udp_t *sock)
{
    socket_event_t event_close_udp_socket = {
        .super.handler = _close_udp_socket_handler,
        .sock = sock,
        // not required
        .local = NULL,
        .remote = NULL,
    };

    event_queue_t* ot_evq = openthread_get_evq();
    event_post(ot_evq, &event_close_udp_socket.super);
}

int sock_udp_get_local(sock_udp_t *sock, sock_udp_ep_t *local)
{
    assert(sock && local);
    otSockAddr* sock_addr = &sock->ot_udp_sock.mSockName;

    local->port = sock_addr->mPort;
    memcpy(&local->addr.ipv6, &sock_addr->mAddress.mFields.m8, sizeof(local->addr.ipv6));

    if (local->port == 0) return -ENOTCONN;
    return 0; 
}

int sock_udp_get_remote(sock_udp_t *sock, sock_udp_ep_t *remote)
{
    assert(sock && remote);
    otSockAddr* sock_addr = &sock->ot_udp_sock.mPeerName;

    remote->port = sock_addr->mPort;
    memcpy(&remote->addr.ipv6, &sock_addr->mAddress.mFields.m8, sizeof(remote->addr.ipv6));

    if (remote->port == 0) return -ENOTCONN;
    return 0; 
}

ssize_t sock_udp_recv_aux(sock_udp_t *sock, void *data, size_t max_len,
                         uint32_t timeout, sock_udp_ep_t *remote,
                         sock_udp_aux_rx_t *aux)
{
    (void) data;
    (void) max_len;
    (void) remote;
    (void) aux;

    mbox_t* mbox = &sock->mbox;
    msg_t msg;

    if (timeout == SOCK_NO_TIMEOUT) {
        mbox_get(mbox, &msg);
    }
    else if (timeout == 0) {
        if (!mbox_try_get(mbox, &msg)) {
            return -EAGAIN;
        }
    }
    else {
        /* Preferring low power over us precision here if both options are
         * possible. This is typically the better trade-off, as even on fast
         * networks round-trip-times are typically measured in ms rather than
         * in us */
        // ztimer_msec standard right now for openthread in riot
        if (IS_USED(MODULE_ZTIMER_MSEC)) {
            /* rounding up seems more sensible here */
            uint32_t timeout_ms = (timeout + US_PER_MS - 1) / US_PER_MS;
            if (ztimer_mbox_get_timeout(ZTIMER_MSEC, mbox, &msg, timeout_ms)) {
                return -ETIMEDOUT;
            }
        }
        else if (IS_USED(MODULE_ZTIMER_USEC)) {
            if (ztimer_mbox_get_timeout(ZTIMER_USEC, mbox, &msg, timeout)) {
                return -ETIMEDOUT;
            }
        }
        else {
            /* cannot do timeout without a timer */
            assert(0);
            return -ENOTSUP;
        }
    }

    ot_message_t* ot_message = msg.content.ptr;

    uint16_t packet_len = 0;
    read_message_event_t read_message_event = {
        .super.handler = _read_message_handler,
        .message_ptr = ot_message->msg,
        .data = data,
        .max_len = max_len,
        .packet_len = &packet_len,
    };
    event_queue_t* ot_evq = openthread_get_evq();
    event_post(ot_evq, &read_message_event.super);
    event_sync(ot_evq);

    // REMOTE KANN NULL SEIN WAS HEI?T DAS?

    // uint8_t addr[16] = {0xff, 0x02, 0x0 ,0x0 ,0x0, 0x0, 0x0 ,0x0 ,0x0, 0x0,0x0 ,0x0 ,0x0, 0x0, 0x0, 0x02};
    // memcpy(&remote->addr.ipv6, addr, 16*sizeof(uint8_t));
    // remote->port = 4404;
    if (remote != NULL) {
        memcpy(&remote->addr.ipv6, ot_message->msg_info.mPeerAddr.mFields.m8, 16*sizeof(uint8_t));
        remote->port = ot_message->msg_info.mPeerPort;
    }
    memarray_free(&ot_messages_memarray,ot_message);

    return packet_len;
}

ssize_t sock_udp_recv_buf_aux(sock_udp_t *sock, void **data, void **buf_ctx,
                              uint32_t timeout, sock_udp_ep_t *remote,
                              sock_udp_aux_rx_t *aux)
{
    (void) sock;
    (void) data;
    (void) buf_ctx;
    (void) timeout;
    (void) remote;
    (void) aux;

    return 0;
}

ssize_t sock_udp_sendv_aux(sock_udp_t *sock, const iolist_t *snips,
                        const sock_udp_ep_t *remote, sock_udp_aux_tx_t *aux)
{
    send_event_t send_event = {
        .super.handler = _send_udp_message_handler,
        .sock = sock,
        .remote = remote,
        .snips = snips,
        .aux = aux,
    };
    
    event_queue_t* ot_evq = openthread_get_evq();
    event_post(ot_evq, &send_event.super);

    return 0;
}
