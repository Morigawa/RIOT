#include "ot.h"

#include "net/netif.h"
#include "openthread/udp.h"


int netif_get_name(const netif_t *iface, char *name)
{
    (void) iface;
    name[0] = 'o';
    name[1] = 't';
    name[2] = '\0';
    return 2;
}