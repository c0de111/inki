#include "captive_portal.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "lwip/pbuf.h"
#include "lwip/udp.h"

static const uint8_t s_dhcp_offer[] = {
    0x02, 0x01, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,     0x00,      0x00,      0x00,      0,
    0,    0,    0,    192,  168,  4,    100,  192,  168,      4,         1,         0x00,      0x00,
    0x00, 0x00, 0,    0,    0,    0,    0,    0,    0,        0,         0,         0,         0,
    0,    0,    0,    0,    0,    0,    0,    0,    0,        0,         0,         0,         0,
    0,    0,    0,    0,    0,    0,    0,    0,    [44] = 0, [107] = 0, [108] = 0, [235] = 0, 99,
    130,  83,   99,   53,   1,    2,    54,   4,    192,      168,       4,         1,         51,
    4,    0x00, 0x01, 0x51, 0x80, 58,   4,    0x00, 0x00,     0x01,      0x2C,      59,        4,
    0x00, 0x00, 0x01, 0xE0, 1,    4,    255,  255,  255,      0,         3,         4,         192,
    168,  4,    1,    6,    4,    192,  168,  4,    1,        15,        4,         'i',       'n',
    'k',  'i',  119,  5,    4,    'i',  'n',  'k',  'i',      0,         255};

static const uint8_t s_dhcp_ack[] = {
    0x02, 0x01, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,     0x00,      0x00,      0x00,      0,
    0,    0,    0,    192,  168,  4,    100,  192,  168,      4,         1,         0x00,      0x00,
    0x00, 0x00, 0,    0,    0,    0,    0,    0,    0,        0,         0,         0,         0,
    0,    0,    0,    0,    0,    0,    0,    0,    0,        0,         0,         0,         0,
    0,    0,    0,    0,    0,    0,    0,    0,    [44] = 0, [107] = 0, [108] = 0, [235] = 0, 99,
    130,  83,   99,   53,   1,    5,    54,   4,    192,      168,       4,         1,         51,
    4,    0x00, 0x01, 0x51, 0x80, 58,   4,    0x00, 0x00,     0x01,      0x2C,      59,        4,
    0x00, 0x00, 0x01, 0xE0, 1,    4,    255,  255,  255,      0,         3,         4,         192,
    168,  4,    1,    6,    4,    192,  168,  4,    1,        15,        4,         'i',       'n',
    'k',  'i',  119,  5,    4,    'i',  'n',  'k',  'i',      0,         255};

static void captive_dhcp_recv(void *arg, struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t *addr,
                              u16_t port) {
    if (!p || p->len < 240) {
        if (p)
            pbuf_free(p);
        return;
    }
    const uint8_t *req = (const uint8_t *)p->payload;
    uint8_t msg_type = 0;
    for (int i = 240; i < (int)p->len - 2; i++) {
        if (req[i] == 53 && req[i + 1] == 1) {
            msg_type = req[i + 2];
            break;
        }
    }
    const uint8_t *tmpl = NULL;
    size_t tmpl_len = 0;
    if (msg_type == 1) {
        tmpl = s_dhcp_offer;
        tmpl_len = sizeof(s_dhcp_offer);
    } else if (msg_type == 3) {
        tmpl = s_dhcp_ack;
        tmpl_len = sizeof(s_dhcp_ack);
    } else {
        pbuf_free(p);
        return;
    }
    uint8_t resp[300] = {0};
    memcpy(resp, tmpl, tmpl_len);
    memcpy(resp + 4, req + 4, 4);
    memcpy(resp + 28, req + 28, 16);
    struct pbuf *rb = pbuf_alloc(PBUF_TRANSPORT, (u16_t)tmpl_len, PBUF_RAM);
    if (!rb) {
        pbuf_free(p);
        return;
    }
    memcpy(rb->payload, resp, tmpl_len);
    udp_sendto(pcb, rb, addr, port);
    pbuf_free(rb);
    pbuf_free(p);
}

void captive_start_dhcp(void) {
    struct udp_pcb *pcb = udp_new_ip_type(IPADDR_TYPE_V4);
    if (!pcb)
        return;
    if (udp_bind(pcb, IP_ADDR_ANY, 67) != ERR_OK) {
        udp_remove(pcb);
        return;
    }
    udp_recv(pcb, captive_dhcp_recv, NULL);
}

static bool dns_qname_lower(const uint8_t *pkt, size_t len, size_t *pos, char *out, size_t olen) {
    size_t in = *pos, oi = 0;
    while (in < len) {
        uint8_t ll = pkt[in++];
        if (ll == 0) {
            out[oi] = '\0';
            *pos = in;
            return true;
        }
        if ((ll & 0xC0) || in + ll > len)
            return false;
        if (oi > 0) {
            if (oi + 1 >= olen)
                return false;
            out[oi++] = '.';
        }
        for (uint8_t i = 0; i < ll; i++) {
            if (oi + 1 >= olen)
                return false;
            char c = (char)pkt[in++];
            out[oi++] = (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
        }
    }
    return false;
}

static void captive_dns_recv(void *arg, struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t *addr,
                             u16_t port) {
    (void)arg;
    if (!p || p->tot_len < 12 || p->tot_len > 512) {
        if (p)
            pbuf_free(p);
        return;
    }
    uint8_t req[512];
    pbuf_copy_partial(p, req, p->tot_len, 0);
    size_t req_len = p->tot_len;
    uint16_t flags = (uint16_t)((req[2] << 8) | req[3]);
    uint16_t qdcount = (uint16_t)((req[4] << 8) | req[5]);
    if ((flags & 0x8000u) || !qdcount) {
        pbuf_free(p);
        return;
    }
    size_t qpos = 12;
    char qname[128];
    if (!dns_qname_lower(req, req_len, &qpos, qname, sizeof(qname)) || qpos + 4 > req_len) {
        pbuf_free(p);
        return;
    }
    uint16_t qtype = (uint16_t)((req[qpos] << 8) | req[qpos + 1]);
    uint16_t qclass = (uint16_t)((req[qpos + 2] << 8) | req[qpos + 3]);
    size_t qend = qpos + 4;
    bool match = (strcmp(qname, "inki-setup") == 0 || strcmp(qname, "inki-setup.local") == 0 ||
                  (strncmp(qname, "inki-setup.", 11) == 0 && qname[11] != '\0'));
    bool add_ans = match && (qtype == 1 || qtype == 255) && (qclass == 1 || qclass == 255);
    uint16_t rcode = match ? 0u : 3u;
    uint8_t resp[512];
    memcpy(resp, req, qend);
    uint16_t rf = (uint16_t)(0x8400u | (flags & 0x0100u) | (rcode & 0x000Fu));
    resp[2] = (uint8_t)(rf >> 8);
    resp[3] = (uint8_t)rf;
    resp[4] = 0;
    resp[5] = 1;
    resp[6] = 0;
    resp[7] = add_ans ? 1 : 0;
    resp[8] = 0;
    resp[9] = 0;
    resp[10] = 0;
    resp[11] = 0;
    size_t off = qend;
    if (add_ans) {
        resp[off++] = 0xC0;
        resp[off++] = 0x0C;
        resp[off++] = 0x00;
        resp[off++] = 0x01;
        resp[off++] = 0x00;
        resp[off++] = 0x01;
        resp[off++] = 0x00;
        resp[off++] = 0x00;
        resp[off++] = 0x00;
        resp[off++] = 0x3C;
        resp[off++] = 0x00;
        resp[off++] = 0x04;
        resp[off++] = 192;
        resp[off++] = 168;
        resp[off++] = 4;
        resp[off++] = 1;
    }
    struct pbuf *rb = pbuf_alloc(PBUF_TRANSPORT, (u16_t)off, PBUF_RAM);
    if (!rb) {
        pbuf_free(p);
        return;
    }
    memcpy(rb->payload, resp, off);
    udp_sendto(pcb, rb, addr, port);
    pbuf_free(rb);
    pbuf_free(p);
}

void captive_start_dns(void) {
    struct udp_pcb *pcb = udp_new_ip_type(IPADDR_TYPE_V4);
    if (!pcb)
        return;
    if (udp_bind(pcb, IP_ADDR_ANY, 53) != ERR_OK) {
        udp_remove(pcb);
        return;
    }
    udp_recv(pcb, captive_dns_recv, NULL);
}
