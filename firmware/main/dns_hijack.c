#include "dns_hijack.h"

#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"

static const char *TAG = "dns";

#define DNS_PORT 53
#define DNS_MAX_PACKET 512
/// All answers point here: the SoftAP's own address.
static const uint8_t PORTAL_IP[4] = {192, 168, 4, 1};

static int s_sock = -1;
static TaskHandle_t s_task;

/// Rewrite a DNS query in place into a response answering with PORTAL_IP.
/// Returns the response length, or 0 if the packet isn't a usable query.
static int build_response(uint8_t *packet, int len, int capacity)
{
    if (len < 12) {
        return 0;
    }
    uint16_t flags = (packet[2] << 8) | packet[3];
    if (flags & 0x8000) {
        return 0; // already a response
    }

    // Walk the first question's labels to find its end.
    int pos = 12;
    while (pos < len && packet[pos] != 0) {
        pos += packet[pos] + 1;
    }
    pos += 1 + 4; // terminating zero + QTYPE + QCLASS
    if (pos > len) {
        return 0;
    }

    // Response header: QR=1, AA=1, keep RD, RCODE=0; one question, one answer.
    packet[2] = 0x84 | (packet[2] & 0x01);
    packet[3] = 0x00;
    packet[4] = 0x00;
    packet[5] = 0x01; // QDCOUNT = 1
    packet[6] = 0x00;
    packet[7] = 0x01; // ANCOUNT = 1
    memset(&packet[8], 0, 4); // NSCOUNT, ARCOUNT

    // Answer: pointer to the question name, A record, TTL 60 s.
    static const uint8_t answer_head[] = {
        0xC0, 0x0C,             // name: pointer to offset 12
        0x00, 0x01, 0x00, 0x01, // TYPE A, CLASS IN
        0x00, 0x00, 0x00, 0x3C, // TTL
        0x00, 0x04,             // RDLENGTH
    };
    if (pos + (int)sizeof(answer_head) + 4 > capacity) {
        return 0;
    }
    memcpy(&packet[pos], answer_head, sizeof(answer_head));
    memcpy(&packet[pos + sizeof(answer_head)], PORTAL_IP, 4);
    return pos + sizeof(answer_head) + 4;
}

static void dns_task(void *arg)
{
    uint8_t packet[DNS_MAX_PACKET];
    while (s_sock >= 0) {
        struct sockaddr_in from;
        socklen_t from_len = sizeof(from);
        int len = recvfrom(s_sock, packet, sizeof(packet), 0, (struct sockaddr *)&from,
                           &from_len);
        if (len <= 0) {
            break; // socket closed by dns_hijack_stop
        }
        int out_len = build_response(packet, len, sizeof(packet));
        if (out_len > 0) {
            sendto(s_sock, packet, out_len, 0, (struct sockaddr *)&from, from_len);
        }
    }
    s_task = NULL;
    vTaskDelete(NULL);
}

void dns_hijack_start(void)
{
    if (s_sock >= 0) {
        return; // already running
    }
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        ESP_LOGE(TAG, "socket() failed");
        return;
    }
    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(DNS_PORT),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        ESP_LOGE(TAG, "bind(:53) failed");
        close(sock);
        return;
    }
    s_sock = sock;
    xTaskCreate(dns_task, "dns_hijack", 3072, NULL, 5, &s_task);
    ESP_LOGI(TAG, "captive-portal DNS up");
}

void dns_hijack_stop(void)
{
    if (s_sock < 0) {
        return;
    }
    int sock = s_sock;
    s_sock = -1;
    close(sock); // unblocks recvfrom; the task exits itself
    ESP_LOGI(TAG, "captive-portal DNS stopped");
}
