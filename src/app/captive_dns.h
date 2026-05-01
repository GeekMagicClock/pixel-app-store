#pragma once

#include "esp_netif.h"

#ifdef __cplusplus
extern "C" {
#endif

void captive_dns_set_ap_netif(esp_netif_t *netif);
void captive_dns_start(void);
void captive_dns_stop(void);

#ifdef __cplusplus
}
#endif

