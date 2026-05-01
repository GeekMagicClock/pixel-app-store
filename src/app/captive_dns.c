#include "app/captive_dns.h"

#include <string.h>

#include "esp_log.h"
#include "esp_netif.h"
#include "lwip/ip4_addr.h"
#include "lwip/ip_addr.h"
#include "lwip/pbuf.h"
#include "lwip/udp.h"

static const char *TAG = "captive_dns";

static struct udp_pcb *s_dns_pcb = NULL;
static ip4_addr_t s_ap_ip;
static esp_netif_t *s_netif_ap = NULL;

void captive_dns_set_ap_netif(esp_netif_t *netif) { s_netif_ap = netif; }

static void refresh_ap_ip(void) {
  if (!s_netif_ap) {
    s_netif_ap = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    if (!s_netif_ap) {
      ESP_LOGW(TAG, "AP netif not set and WIFI_AP_DEF not found");
      IP4_ADDR(&s_ap_ip, 0, 0, 0, 0);
      return;
    }
  }

  esp_netif_ip_info_t ip = {};
  if (esp_netif_get_ip_info(s_netif_ap, &ip) == ESP_OK) {
    s_ap_ip.addr = ip.ip.addr;
    char iptxt[16];
    esp_ip4addr_ntoa(&ip.ip, iptxt, sizeof(iptxt));
    ESP_LOGI(TAG, "AP IP: %s", iptxt);
  } else {
    IP4_ADDR(&s_ap_ip, 0, 0, 0, 0);
    ESP_LOGW(TAG, "esp_netif_get_ip_info failed");
  }
}

static void dns_recv_cb(void *arg, struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t *addr, u16_t port) {
  (void)arg;
  (void)pcb;
  if (!p) return;

  if (p->len < 12) {
    pbuf_free(p);
    return;
  }

  if (s_ap_ip.addr == 0) {
    refresh_ap_ip();
    if (s_ap_ip.addr == 0) {
      pbuf_free(p);
      return;
    }
  }

  uint8_t out[256];
  int hdr_len = p->len;
  if (hdr_len > 180) hdr_len = 180;
  memcpy(out, p->payload, hdr_len);

  out[2] |= 0x80;
  out[3] = 0x80;
  out[6] = 0x00;
  out[7] = 0x01;

  int off = hdr_len;
  out[off++] = 0xC0;
  out[off++] = 0x0C;
  out[off++] = 0x00;
  out[off++] = 0x01;
  out[off++] = 0x00;
  out[off++] = 0x01;
  out[off++] = 0x00;
  out[off++] = 0x00;
  out[off++] = 0x00;
  out[off++] = 0x3C;
  out[off++] = 0x00;
  out[off++] = 0x04;
  memcpy(&out[off], &s_ap_ip.addr, 4);
  off += 4;

  struct pbuf *q = pbuf_alloc(PBUF_TRANSPORT, off, PBUF_RAM);
  if (q) {
    memcpy(q->payload, out, off);
    udp_sendto(s_dns_pcb, q, addr, port);
    pbuf_free(q);
  }

  pbuf_free(p);
}

void captive_dns_start(void) {
  if (s_dns_pcb) {
    ESP_LOGI(TAG, "DNS already running");
    return;
  }

  refresh_ap_ip();
  if (s_ap_ip.addr == 0) {
    ESP_LOGW(TAG, "AP IP is 0.0.0.0; starting DNS anyway");
  }

  s_dns_pcb = udp_new();
  if (!s_dns_pcb) {
    ESP_LOGE(TAG, "udp_new failed");
    return;
  }

  if (udp_bind(s_dns_pcb, IP_ANY_TYPE, 53) != ERR_OK) {
    ESP_LOGE(TAG, "udp_bind :53 failed");
    udp_remove(s_dns_pcb);
    s_dns_pcb = NULL;
    return;
  }

  udp_recv(s_dns_pcb, dns_recv_cb, NULL);
  ESP_LOGI(TAG, "Captive DNS started on :53");
}

void captive_dns_stop(void) {
  if (!s_dns_pcb) return;
  udp_remove(s_dns_pcb);
  s_dns_pcb = NULL;
  ESP_LOGI(TAG, "Captive DNS stopped");
}

