#ifndef _LWIPOPTS_H
#define _LWIPOPTS_H

// The default MEM_SIZE (4000) is too small for the matrix endpoint: the
// base64 response is ~6.4 KB and tcp_write(TCP_WRITE_FLAG_COPY) allocates
// queued chunks from this heap. Give it enough room for a full response.
// Must be defined before lwipopts_examples_common.h (which has an #ifndef
// MEM_SIZE fallback).
#define MEM_SIZE 16000

#include "lwipopts_examples_common.h"

#define LWIP_MDNS_RESPONDER 1
#define LWIP_IGMP 1
#define LWIP_NUM_NETIF_CLIENT_DATA 1
// Required for MDNS_RESP_USENETIF_EXTCALLBACK to take effect: lwIP guards the
// mDNS netif callback on this define (mdns.c), so without it the responder
// never restarts on link up / IP change and '<hostname>.local' goes stale.
#define LWIP_NETIF_EXT_STATUS_CALLBACK 1
#define MDNS_RESP_USENETIF_EXTCALLBACK  1
#define MEMP_NUM_SYS_TIMEOUT (LWIP_NUM_SYS_TIMEOUT_INTERNAL + 3)
#define MEMP_NUM_TCP_PCB 12

#endif
