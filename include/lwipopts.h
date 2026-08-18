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
#define MDNS_RESP_USENETIF_EXTCALLBACK  1
#define MEMP_NUM_SYS_TIMEOUT (LWIP_NUM_SYS_TIMEOUT_INTERNAL + 3)
#define MEMP_NUM_TCP_PCB 12

// Enable some httpd features
#define LWIP_HTTPD_CGI 1
#define LWIP_HTTPD_SSI 1
#define LWIP_HTTPD_SSI_MULTIPART 1
#define LWIP_HTTPD_DYNAMIC_HEADERS 1
#define LWIP_HTTPD_SUPPORT_POST 1
#define LWIP_HTTPD_SSI_INCLUDE_TAG 0

#endif
