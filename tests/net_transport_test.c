#include "net.h"
#include "net_internal.h"
#include "e1000.h"
#include "kstring.h"
#include <stdio.h>
#include <stdlib.h>

extern int net_http_test_resolve_url(const char *base, const char *href, char *out, usize capacity);
extern int net_http_test_attribute(const char *tag, const char *name, char *out, usize capacity);

static const u8 guest_mac[6] = {0x52, 0x54, 0x00, 0x12, 0x34, 0x56};
static const u8 peer_mac[6]  = {0x52, 0x54, 0x00, 0xAA, 0xBB, 0xCC};
static u8 pending_frame[1600];
static u16 pending_length;
static int pending;
static u64 ticks;
static u32 dns_queries;
static u32 dns_ttl = 60u;
static int dns_reply_cname;
static u32 dhcp_discovers;
static u32 dhcp_requests;
static int dhcp_ack_missing_server;
static int dhcp_ack_from_other_ip;
static int dhcp_bad_mask_length;
static u32 tcp_syns;
static u32 tcp_http_requests;
static int inject_out_of_order_ack;
static int waiting_for_client_reack;
static u32 tcp_duplicate_acks;
static u32 http_extra_declared_length;
static int http_duplicate_length;
static char serial_capture[32768];
static usize serial_used;
static u32 tcp_server_sequence = 0x12345678u;
static const char page_body[] =
    "<html><head><script>BAD_SCRIPT_TEXT</script><style>.den-hide { display: none; } #secret { visibility: hidden; }</style></head>"
    "<body><div class='den-hide'>BAD_CSS_CLASS</div><span id='secret'>BAD_CSS_ID</span>"
    "<script>document.write('<p>JS static write works</p>');</script>"
    "<h1>DenOS test page</h1><p>Hello &amp; welcome.</p>"
    "<p><a href='/next'>Next page link</a></p><img src='/logo.qoi' alt='DenOS logo'>"
    "<ul><li>First item</li><li>Second item</li></ul>"
    "<p>Unsafe &#27;[31m and &#0; control bytes.</p></body></html>";

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)

static u16 get16(const u8 *p) { return (u16)(((u16)p[0] << 8) | p[1]); }
static u32 get32(const u8 *p) { return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3]; }
static void put16(u8 *p, u16 v) { p[0] = (u8)(v >> 8); p[1] = (u8)v; }
static void put32(u8 *p, u32 v) { p[0] = (u8)(v >> 24); p[1] = (u8)(v >> 16); p[2] = (u8)(v >> 8); p[3] = (u8)v; }
static u16 checksum(const u8 *data, u32 length) {
    u32 sum = 0;
    while (length > 1u) { sum += ((u16)data[0] << 8) | data[1]; data += 2; length -= 2; }
    if (length) sum += (u16)data[0] << 8;
    while (sum >> 16) sum = (sum & 0xFFFFu) + (sum >> 16);
    return (u16)~sum;
}
static u16 transport_checksum(const u8 src[4], const u8 dst[4], u8 proto,
                              const u8 *packet, u16 length) {
    u8 pseudo[4];
    u32 sum = 0;
    for (u32 i = 0; i < 4; i += 2) {
        sum += ((u16)src[i] << 8) | src[i + 1];
        sum += ((u16)dst[i] << 8) | dst[i + 1];
    }
    pseudo[0] = 0; pseudo[1] = proto; put16(pseudo + 2, length);
    sum += ((u16)pseudo[0] << 8) | pseudo[1];
    sum += get16(pseudo + 2);
    for (u32 i = 0; i + 1u < length; i += 2u) sum += ((u16)packet[i] << 8) | packet[i + 1u];
    if (length & 1u) sum += (u16)packet[length - 1u] << 8;
    while (sum >> 16) sum = (sum & 0xFFFFu) + (sum >> 16);
    return (u16)~sum;
}
static void queue_frame(const u8 *frame, u16 length) {
    CHECK(length <= sizeof(pending_frame));
    kmemcpy(pending_frame, frame, length);
    pending_length = length;
    pending = 1;
}
static void make_arp_reply(const u8 *request) {
    u8 frame[42] = {0};
    const u8 *arp = request + 14;
    kmemcpy(frame, request + 6, 6); kmemcpy(frame + 6, peer_mac, 6); put16(frame + 12, 0x0806);
    put16(frame + 14, 1); put16(frame + 16, 0x0800); frame[18] = 6; frame[19] = 4; put16(frame + 20, 2);
    kmemcpy(frame + 22, peer_mac, 6); kmemcpy(frame + 28, arp + 24, 4);
    kmemcpy(frame + 32, arp + 8, 6); kmemcpy(frame + 38, arp + 14, 4);
    queue_frame(frame, sizeof(frame));
}
static void make_ip_frame(const u8 src[4], const u8 dst[4], u8 protocol,
                          const u8 *payload, u16 payload_length) {
    u8 frame[1600] = {0};
    u8 *ip = frame + 14;
    kmemcpy(frame, guest_mac, 6); kmemcpy(frame + 6, peer_mac, 6); put16(frame + 12, 0x0800);
    ip[0] = 0x45; put16(ip + 2, (u16)(20u + payload_length));
    put16(ip + 4, 1); put16(ip + 6, 0); ip[8] = 64; ip[9] = protocol;
    kmemcpy(ip + 12, src, 4); kmemcpy(ip + 16, dst, 4); put16(ip + 10, checksum(ip, 20));
    kmemcpy(ip + 20, payload, payload_length);
    queue_frame(frame, (u16)(14u + 20u + payload_length));
}
static void make_udp_reply(const u8 src[4], const u8 dst[4], u16 src_port,
                           u16 dst_port, const u8 *payload, u16 payload_length) {
    u8 udp[1500] = {0};
    put16(udp, src_port); put16(udp + 2, dst_port); put16(udp + 4, (u16)(8u + payload_length));
    kmemcpy(udp + 8, payload, payload_length);
    /* UDP checksum zero is legal for IPv4 and keeps this fake peer small. */
    make_ip_frame(src, dst, 17, udp, (u16)(8u + payload_length));
}
static void make_dns_reply(const u8 *query, u16 query_length, u16 source_port,
                           const u8 dns_server[4], const u8 guest_ip[4]) {
    u8 dns[512] = {0};
    u16 question_length;
    u16 qpos = 12;
    while (qpos < query_length && query[qpos] != 0) qpos = (u16)(qpos + query[qpos] + 1u);
    if (qpos >= query_length || qpos + 5u > query_length) return;
    question_length = (u16)(qpos + 5u - 12u);
    put16(dns, get16(query)); put16(dns + 2, 0x8180); put16(dns + 4, 1);
    put16(dns + 6, dns_reply_cname ? 3u : 2u);
    kmemcpy(dns + 12, query + 12, question_length);
    u16 pos = (u16)(12u + question_length);
    /* A validly encoded but unrelated A record must not satisfy this lookup. */
    dns[pos++] = 4; kmemcpy(dns + pos, "evil", 4); pos += 4;
    dns[pos++] = 4; kmemcpy(dns + pos, "test", 4); pos += 4; dns[pos++] = 0;
    put16(dns + pos, 1); pos += 2; put16(dns + pos, 1); pos += 2;
    put32(dns + pos, dns_ttl); pos += 4; put16(dns + pos, 4); pos += 2;
    dns[pos++] = 203; dns[pos++] = 0; dns[pos++] = 113; dns[pos++] = 9;
    if (dns_reply_cname) {
        static const u8 alias_name[] = {5, 'a','l','i','a','s', 7, 'e','x','a','m','p','l','e', 4, 't','e','s','t', 0};
        /* The question name aliases alias.example.test; the final A owner
         * points into the compressed CNAME RDATA, exercising pointer decode. */
        dns[pos++] = 0xC0; dns[pos++] = 0x0C;
        put16(dns + pos, 5); pos += 2; put16(dns + pos, 1); pos += 2;
        put32(dns + pos, 30u); pos += 4; put16(dns + pos, sizeof(alias_name)); pos += 2;
        u16 alias_offset = pos;
        kmemcpy(dns + pos, alias_name, sizeof(alias_name)); pos = (u16)(pos + sizeof(alias_name));
        dns[pos++] = (u8)(0xC0u | (alias_offset >> 8)); dns[pos++] = (u8)alias_offset;
        put16(dns + pos, 1); pos += 2; put16(dns + pos, 1); pos += 2;
        put32(dns + pos, dns_ttl); pos += 4; put16(dns + pos, 4); pos += 2;
        dns[pos++] = 93; dns[pos++] = 184; dns[pos++] = 216; dns[pos++] = 34;
    } else {
        dns[pos++] = 0xC0; dns[pos++] = 0x0C;
        put16(dns + pos, 1); pos += 2; put16(dns + pos, 1); pos += 2;
        put32(dns + pos, dns_ttl); pos += 4; put16(dns + pos, 4); pos += 2;
        dns[pos++] = 93; dns[pos++] = 184; dns[pos++] = 216; dns[pos++] = 34;
    }
    ++dns_queries;
    make_udp_reply(dns_server, guest_ip, 53, source_port, dns, pos);
}
static void make_dhcp_reply(const u8 *request, u16 request_length, u8 message_type) {
    u8 bootp[300] = {0};
    u8 source[4] = {10, 1, 2, 1};
    u8 broadcast[4] = {255, 255, 255, 255};
    u16 pos = 240;
    const u8 assigned[4] = {10, 1, 2, 100};
    const u8 mask[4] = {255, 255, 255, 0};
    const u8 router[4] = {10, 1, 2, 1};
    const u8 dns[4] = {10, 1, 2, 1};
    const u8 server[4] = {10, 1, 2, 1};
    (void)request_length;
    bootp[0] = 2; bootp[1] = 1; bootp[2] = 6;
    kmemcpy(bootp + 4, request + 4, 4); kmemcpy(bootp + 16, assigned, 4);
    kmemcpy(bootp + 28, guest_mac, 6); put32(bootp + 236, 0x63825363u);
#define OPT(code, len, data) do { bootp[pos++] = (code); bootp[pos++] = (len); kmemcpy(bootp + pos, (data), (len)); pos = (u16)(pos + (len)); } while (0)
    OPT(53, 1, &message_type);
    if (!(message_type == 5u && dhcp_ack_missing_server)) OPT(54, 4, server);
    if (message_type == 5u && dhcp_bad_mask_length) {
        bootp[pos++] = 1; bootp[pos++] = 3;
        kmemcpy(bootp + pos, mask, 3); pos = (u16)(pos + 3u);
    } else {
        OPT(1, 4, mask);
    }
    OPT(3, 4, router); OPT(6, 4, dns);
    { u8 lease[4] = {0, 0, 0x0E, 0x10}; OPT(51, 4, lease); }
    bootp[pos++] = 255;
#undef OPT
    if (message_type == 2) {
        ++dhcp_discovers;
        make_udp_reply(source, broadcast, 67, 68, bootp, pos);
    } else {
        /* Exercise valid ACK addressing and, in a negative test, ensure the
         * server cannot change when option 54 is omitted. */
        ++dhcp_requests;
        if (dhcp_ack_from_other_ip) source[3] = 2;
        make_udp_reply(source, assigned, 67, 68, bootp, pos);
    }
}
static void send_test_http_response(const u8 source[4], const u8 destination[4],
                                    u16 destination_port, u32 client_sequence) {
    u8 tcp[1400] = {0};
    char response[900];
    char duplicate_header[64] = "";
    int length;
    if (http_duplicate_length)
        (void)snprintf(duplicate_header, sizeof(duplicate_header), "Content-Length: %lu\r\n",
                       (unsigned long)(kstrlen(page_body) + http_extra_declared_length + 1u));
    length = snprintf(response, sizeof(response),
        "HTTP/1.0 200 OK\r\nContent-Type: text/html\r\nContent-Length: %lu\r\n%sConnection: close\r\n\r\n%s",
        (unsigned long)(kstrlen(page_body) + http_extra_declared_length), duplicate_header, page_body);
    CHECK(length > 0 && (usize)length < sizeof(response));
    put16(tcp, 80); put16(tcp + 2, destination_port);
    put32(tcp + 4, tcp_server_sequence + 1u);
    put32(tcp + 8, client_sequence);
    tcp[12] = 0x50; tcp[13] = 0x19; put16(tcp + 14, 8192);
    kmemcpy(tcp + 20, response, (usize)length);
    put16(tcp + 16, 0);
    put16(tcp + 16, transport_checksum(source, destination, 6, tcp, (u16)(20 + length)));
    make_ip_frame(source, destination, 6, tcp, (u16)(20 + length));
    ++tcp_http_requests;
}

static void make_tcp_reply(const u8 *outgoing, u16 frame_length) {
    const u8 *out_ip = outgoing + 14;
    u8 tcp[64] = {0};
    u8 source[4], destination[4];
    u16 ip_header = (u16)((out_ip[0] & 15u) * 4u);
    u16 ip_total = get16(out_ip + 2);
    const u8 *out_tcp = out_ip + ip_header;
    u16 tcp_header = (u16)((out_tcp[12] >> 4) * 4u);
    u16 data_length = (u16)(ip_total - ip_header - tcp_header);
    u16 destination_port = get16(out_tcp);
    u32 client_sequence = get32(out_tcp + 4);
    (void)frame_length;
    kmemcpy(source, out_ip + 16, 4); kmemcpy(destination, out_ip + 12, 4);

    if (out_tcp[13] & 0x02u) {
        put16(tcp, 80); put16(tcp + 2, destination_port);
        put32(tcp + 4, tcp_server_sequence);
        put32(tcp + 8, client_sequence + 1u);
        tcp[12] = 0x50; tcp[13] = 0x12; put16(tcp + 14, 8192);
        put16(tcp + 16, 0);
        put16(tcp + 16, transport_checksum(source, destination, 6, tcp, 20));
        make_ip_frame(source, destination, 6, tcp, 20);
        ++tcp_syns;
        return;
    }

    if (waiting_for_client_reack && !data_length && (out_tcp[13] & 0x10u)) {
        /* The client sent the duplicate cumulative ACK for our intentionally
         * out-of-order segment. Now retransmit the missing in-order response. */
        waiting_for_client_reack = 0;
        ++tcp_duplicate_acks;
        send_test_http_response(source, destination, destination_port, client_sequence);
        return;
    }
    if (waiting_for_client_reack && data_length) {
        /* A broken client that discarded the ACK with the future sequence
         * will retransmit its request instead of issuing the needed dup-ACK. */
        return;
    }
    if (data_length) {
        if (inject_out_of_order_ack) {
            /* Valid ACK for client data, but server sequence is one byte ahead
             * of RCV.NXT. TCP ACK processing must not be discarded with data. */
            inject_out_of_order_ack = 0;
            waiting_for_client_reack = 1;
            put16(tcp, 80); put16(tcp + 2, destination_port);
            put32(tcp + 4, tcp_server_sequence + 2u);
            put32(tcp + 8, client_sequence + data_length);
            tcp[12] = 0x50; tcp[13] = 0x18; put16(tcp + 14, 8192);
            tcp[20] = '!';
            put16(tcp + 16, 0);
            put16(tcp + 16, transport_checksum(source, destination, 6, tcp, 21));
            make_ip_frame(source, destination, 6, tcp, 21);
            return;
        }
        send_test_http_response(source, destination, destination_port,
                                client_sequence + data_length);
    } else if (out_tcp[13] & 0x10u) {
        ++tcp_duplicate_acks;
    }
}

int e1000_init(void) { return 0; }
int e1000_send(const void *frame_data, u16 length) {
    const u8 *frame = (const u8 *)frame_data;
    if (length < 14u) return -1;
    if (get16(frame + 12) == 0x0806u) {
        const u8 *arp = frame + 14;
        if (get16(arp + 6) == 1u) make_arp_reply(frame);
    } else if (get16(frame + 12) == 0x0800u) {
        const u8 *ip = frame + 14;
        u16 ihl = (u16)((ip[0] & 15u) * 4u);
        if (ip[9] == 17u && get16(ip + ihl + 2) == 53u) {
            make_dns_reply(ip + ihl + 8, (u16)(get16(ip + 2) - ihl - 8u), get16(ip + ihl),
                           ip + 16, ip + 12);
        } else if (ip[9] == 17u && get16(ip + ihl) == 68u && get16(ip + ihl + 2) == 67u) {
            const u8 *bootp = ip + ihl + 8;
            u16 bootp_length = (u16)(get16(ip + 2) - ihl - 8u);
            u8 msg = 0;
            for (u16 i = 240; i + 2u < bootp_length;) {
                u8 code = bootp[i++];
                if (code == 255) break;
                if (!code) continue;
                u8 n = bootp[i++];
                if (code == 53 && n == 1 && i < bootp_length) { msg = bootp[i]; break; }
                i = (u16)(i + n);
            }
            if (msg == 1) make_dhcp_reply(bootp, bootp_length, 2);
            else if (msg == 3) make_dhcp_reply(bootp, bootp_length, 5);
        } else if (ip[9] == 6u && get16(ip + ihl + 2) == 80u) {
            make_tcp_reply(frame, length);
        }
    }
    return 0;
}
void e1000_poll(void) {
    if (pending) {
        u16 length = pending_length;
        pending = 0;
        net_receive_frame(pending_frame, length);
    }
}
int e1000_link_up(void) { return 1; }
const u8 *e1000_mac_address(void) { return guest_mac; }
u32 e1000_received_frames(void) { return 0; }
u32 e1000_transmitted_frames(void) { return dns_queries + dhcp_discovers + dhcp_requests + tcp_syns + tcp_http_requests; }
u64 timer_ticks(void) { return ticks++; }
void serial_write(const char *s) {
    while (*s && serial_used + 1u < sizeof(serial_capture)) serial_capture[serial_used++] = *s++;
    serial_capture[serial_used] = 0;
}
void serial_putc(char c) {
    if (serial_used + 1u < sizeof(serial_capture)) serial_capture[serial_used++] = c;
    serial_capture[serial_used] = 0;
}

int main(void) {
    net_status_t status;
    char resolved[1024];
    CHECK(net_http_test_resolve_url("https://example.test/dir/page.html", "next.html", resolved, sizeof(resolved)) == 0);
    CHECK(kstrcmp(resolved, "https://example.test/dir/next.html") == 0);
    CHECK(net_http_test_resolve_url("https://example.test/dir/page.html", "../next.html", resolved, sizeof(resolved)) == 0);
    CHECK(kstrcmp(resolved, "https://example.test/next.html") == 0);
    CHECK(net_http_test_resolve_url("https://example.test/a/b/page.html?old=1", "?new=2", resolved, sizeof(resolved)) == 0);
    CHECK(kstrcmp(resolved, "https://example.test/a/b/page.html?new=2") == 0);
    CHECK(net_http_test_resolve_url("http://example.test/a/b/", "/root/./page/../index.html", resolved, sizeof(resolved)) == 0);
    CHECK(kstrcmp(resolved, "http://example.test/root/index.html") == 0);
    CHECK(net_http_test_resolve_url("https://example.test/a/page", "//cdn.example.test/img.qoi", resolved, sizeof(resolved)) == 0);
    CHECK(kstrcmp(resolved, "https://cdn.example.test/img.qoi") == 0);
    CHECK(net_http_test_resolve_url("https://example.test/a/page", "javascript:alert(1)", resolved, sizeof(resolved)) < 0);
    CHECK(net_http_test_attribute("<a href='/find?q=a&amp;b=2'>", "href", resolved, sizeof(resolved)) == 1);
    CHECK(kstrcmp(resolved, "/find?q=a&b=2") == 0);
    CHECK(net_http_test_resolve_url("https://example.test/base/page", resolved, resolved, sizeof(resolved)) == 0);
    CHECK(kstrcmp(resolved, "https://example.test/find?q=a&b=2") == 0);
    CHECK(net_http_test_attribute("<a\n href='/line-break'>", "href", resolved, sizeof(resolved)) == 1);
    CHECK(kstrcmp(resolved, "/line-break") == 0);
    u8 address[4];
    CHECK(net_init() == 0);
    CHECK(net_get_status(&status) == 0 && status.link_up);
    CHECK(status.ipv4[0] == 10 && status.ipv4[3] == 15);

    /* DNS: real query/reply path, validated A answer and cache hit. */
    CHECK(net_dns_resolve("example.test", address, 300) == 0);
    CHECK(address[0] == 93 && address[1] == 184 && address[2] == 216 && address[3] == 34);
    CHECK(dns_queries == 1);
    CHECK(net_dns_resolve("EXAMPLE.TEST", address, 300) == 0);
    CHECK(dns_queries == 1);

    /* TTL zero must not create a synthetic 60-second cache entry. */
    net_dns_reset();
    dns_ttl = 0;
    CHECK(net_dns_resolve("example.test", address, 300) == 0);
    CHECK(dns_queries == 2);
    CHECK(net_dns_resolve("example.test", address, 300) == 0);
    CHECK(dns_queries == 3);
    dns_ttl = 60u;
    net_dns_reset();

    /* CNAME chains, including compressed names in CNAME RDATA, must resolve
     * to their terminal A record and use the minimum TTL across the chain. */
    dns_reply_cname = 1;
    u32 cname_query_count = dns_queries;
    CHECK(net_dns_resolve("example.test", address, 300) == 0);
    CHECK(address[0] == 93 && address[1] == 184 && address[2] == 216 && address[3] == 34);
    CHECK(dns_queries == cname_query_count + 1u);
    CHECK(net_dns_resolve("example.test", address, 300) == 0);
    CHECK(dns_queries == cname_query_count + 1u);
    dns_reply_cname = 0;
    net_dns_reset();

    /* DHCP: offer/request/ack with mask, router and DNS options. */
    CHECK(net_dhcp_configure(500) == 0);
    CHECK(dhcp_discovers == 1 && dhcp_requests == 1);
    CHECK(net_get_status(&status) == 0 && status.dhcp_configured);
    CHECK(status.ipv4[0] == 10 && status.ipv4[1] == 1 && status.ipv4[2] == 2 && status.ipv4[3] == 100);
    CHECK(status.netmask[0] == 255 && status.netmask[3] == 0);
    CHECK(status.gateway[0] == 10 && status.gateway[1] == 1 && status.gateway[2] == 2 && status.gateway[3] == 1);
    CHECK(status.dns[0] == 10 && status.dns[1] == 1 && status.dns[2] == 2 && status.dns[3] == 1);

    /* An ACK without option 54 must still come from the OFFER-selected
     * server. A different packet source must not silently replace it. */
    dhcp_ack_missing_server = 1;
    dhcp_ack_from_other_ip = 1;
    CHECK(net_dhcp_configure(500u) == -5);
    dhcp_ack_missing_server = 0;
    dhcp_ack_from_other_ip = 0;

    /* Malformed subnet-mask option lengths are discarded rather than read
     * as a four-byte address from the following option. */
    dhcp_bad_mask_length = 1;
    CHECK(net_dhcp_configure(500u) == -5);
    dhcp_bad_mask_length = 0;
    CHECK(net_get_status(&status) == 0 && status.ipv4[3] == 100);

    /* TCP three-way handshake and CLI HTTP/HTML rendering. The first
     * response segment carries a valid ACK but a future sequence number.
     * The client must consume its ACK field and send a duplicate ACK. */
    serial_used = 0; serial_capture[0] = 0;
    inject_out_of_order_ack = 1;
    CHECK(net_http_get("http://example.test/") == 0);
    CHECK(tcp_syns == 1 && tcp_http_requests == 1);
    CHECK(inject_out_of_order_ack == 0 && waiting_for_client_reack == 0);
    CHECK(tcp_duplicate_acks > 0);
    CHECK(kstrstr(serial_capture, "HTTP 200") != 0);
    CHECK(kstrstr(serial_capture, "DenOS test page") != 0);
    CHECK(kstrstr(serial_capture, "Hello & welcome.") != 0);
    CHECK(kstrstr(serial_capture, "First item") != 0);
    CHECK(kstrstr(serial_capture, "BAD_SCRIPT_TEXT") == 0);
    CHECK(kstrstr(serial_capture, "BAD_STYLE_TEXT") == 0);
    CHECK(kstrstr(serial_capture, "BAD_CSS_CLASS") == 0);
    CHECK(kstrstr(serial_capture, "BAD_CSS_ID") == 0);
    CHECK(kstrstr(serial_capture, "JS static write works") != 0);
    CHECK(kstrstr(serial_capture, "Next page link") != 0);
    CHECK(kstrstr(serial_capture, "[image: DenOS logo]") != 0);
    CHECK(kstrstr(serial_capture, "\033[31m") == 0);
    CHECK(kstrstr(serial_capture, "control bytes") != 0);

    /* A short response body must not be shown as a complete document when
     * the peer's Content-Length says more bytes were expected. */
    serial_used = 0; serial_capture[0] = 0;
    http_extra_declared_length = 25u;
    CHECK(net_http_get("http://example.test/") == 0);
    http_extra_declared_length = 0;
    CHECK(kstrstr(serial_capture, "Page incomplete") != 0);

    /* Duplicate/conflicting body framing must be rejected, not silently
     * interpreted using whichever Content-Length happened to appear first. */
    serial_used = 0; serial_capture[0] = 0;
    http_duplicate_length = 1;
    CHECK(net_http_get("http://example.test/") == -8);
    http_duplicate_length = 0;
    CHECK(kstrstr(serial_capture, "duplicate HTTP body-framing header") != 0);

    serial_used = 0; serial_capture[0] = 0;
    CHECK(net_http_get("https://example.test/") < 0);
    CHECK(kstrstr(serial_capture, "HTTPS is not enabled in this image") != 0);
    serial_used = 0; serial_capture[0] = 0;
    CHECK(net_http_get("http://example.test/?ok=yes\r\nInjected:bad") < 0);
    CHECK(kstrstr(serial_capture, "invalid URL") != 0);
    puts("Network transport tests passed: DNS/cache including TTL zero and CNAME chains, DHCP server/option validation, out-of-order TCP ACK processing, HTTP framing, HTML layout, simple CSS visibility, and static document.write rendering.");
    return 0;
}
