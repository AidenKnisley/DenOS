#include "net.h"
#include "net_internal.h"
#include "kstring.h"
#include "timer.h"
#include "entropy.h"
#include "cpu.h"

#define TCP_FLAG_FIN 0x01u
#define TCP_FLAG_SYN 0x02u
#define TCP_FLAG_RST 0x04u
#define TCP_FLAG_PSH 0x08u
#define TCP_FLAG_ACK 0x10u
#define TCP_RX_CAPACITY 8192u
#define TCP_TX_MSS 1024u
#define TCP_HEADER_SIZE 20u
#define TCP_SYN_HEADER_SIZE 24u
#define TCP_DEFAULT_TIMEOUT 300u

typedef enum tcp_state {
    TCP_CLOSED = 0,
    TCP_SYN_SENT,
    TCP_ESTABLISHED,
    TCP_CLOSING
} tcp_state_t;

typedef struct tcp_client {
    tcp_state_t state;
    u8 remote_ip[4];
    u16 remote_port;
    u16 local_port;
    u32 send_unacknowledged;
    u32 send_next;
    u32 receive_next;
    u16 peer_window;
    u8 receive_buffer[TCP_RX_CAPACITY];
    u16 receive_length;
    int peer_closed;
    int failed;
    int local_fin_sent;
} tcp_client_t;

static tcp_client_t client;
static u8 tx_packet[TCP_HEADER_SIZE + 4u + TCP_TX_MSS];

static u16 get16(const u8 *p) { return (u16)(((u16)p[0] << 8) | p[1]); }
static u32 get32(const u8 *p) { return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3]; }
static void put16(u8 *p, u16 v) { p[0] = (u8)(v >> 8); p[1] = (u8)v; }
static void put32(u8 *p, u32 v) { p[0] = (u8)(v >> 24); p[1] = (u8)(v >> 16); p[2] = (u8)(v >> 8); p[3] = (u8)v; }
static u32 checksum_add(u32 sum, const u8 *p, u32 n) {
    while (n > 1u) { sum += ((u16)p[0] << 8) | p[1]; p += 2; n -= 2; }
    if (n) sum += (u16)p[0] << 8;
    return sum;
}
static u16 checksum_finish(u32 sum) {
    while (sum >> 16) sum = (sum & 0xFFFFu) + (sum >> 16);
    return (u16)~sum;
}
static u16 tcp_checksum(const u8 source[4], const u8 destination[4], const u8 *packet, u16 length) {
    u8 pseudo[4];
    u32 sum = 0;
    sum = checksum_add(sum, source, 4);
    sum = checksum_add(sum, destination, 4);
    pseudo[0] = 0; pseudo[1] = 6;
    put16(pseudo + 2, length);
    sum = checksum_add(sum, pseudo, 4);
    sum = checksum_add(sum, packet, length);
    return checksum_finish(sum);
}
static int sequence_after(u32 a, u32 b) { return (i32)(a - b) > 0; }
static int sequence_at_or_after(u32 a, u32 b) { return (i32)(a - b) >= 0; }

void net_tcp_init(void) {
    kmemset(&client, 0, sizeof(client));
    client.state = TCP_CLOSED;
}

static int transmit(u8 flags, const void *payload, u16 payload_length, int syn_options) {
    u8 source_ip[4];
    u16 header_length = syn_options ? TCP_SYN_HEADER_SIZE : TCP_HEADER_SIZE;
    u16 total = (u16)(header_length + payload_length);
    if (!client.local_port || payload_length > TCP_TX_MSS || total > sizeof(tx_packet)) return -1;
    kmemset(tx_packet, 0, header_length);
    put16(tx_packet, client.local_port);
    put16(tx_packet + 2, client.remote_port);
    put32(tx_packet + 4, client.send_next);
    put32(tx_packet + 8, client.receive_next);
    tx_packet[12] = (u8)((header_length / 4u) << 4);
    tx_packet[13] = flags;
    put16(tx_packet + 14, (u16)(TCP_RX_CAPACITY - client.receive_length));
    if (syn_options) {
        tx_packet[20] = 2; tx_packet[21] = 4; put16(tx_packet + 22, 1460u);
    }
    if (payload_length) kmemcpy(tx_packet + header_length, payload, payload_length);
    net_get_ipv4_config(source_ip, 0, 0, 0);
    put16(tx_packet + 16, 0);
    put16(tx_packet + 16, tcp_checksum(source_ip, client.remote_ip, tx_packet, total));
    return net_ipv4_send_from(source_ip, client.remote_ip, 6, tx_packet, total);
}
static int send_ack(void) { return transmit(TCP_FLAG_ACK, 0, 0, 0); }
static int wait_for_ack(u32 target, u32 timeout_ticks) {
    u64 start = timer_ticks();
    while (timer_ticks() - start < timeout_ticks) {
        net_poll();
        if (client.failed || client.state == TCP_CLOSED) return -1;
        if (sequence_at_or_after(client.send_unacknowledged, target)) return 0;
        if (client.peer_closed && !sequence_at_or_after(client.send_unacknowledged, target)) return -1;
    }
    return -1;
}

int net_tcp_connect(const u8 address[4], u16 port, u32 timeout_ticks) {
    u64 start;
    u32 retry_interval, next_retry;
    if (!net_is_ready() || !address || !port ||
        (address[0] == 0 && address[1] == 0 && address[2] == 0 && address[3] == 0)) return -1;
    if (client.state != TCP_CLOSED) net_tcp_close(50u);
    kmemset(&client, 0, sizeof(client));
    client.state = TCP_SYN_SENT;
    kmemcpy(client.remote_ip, address, 4);
    client.remote_port = port;
    {
        u8 initial_random[6];
        if (cpu_random_bytes(initial_random, sizeof(initial_random)) == 0) {
            u16 random_port = (u16)(((u16)initial_random[0] << 8) | initial_random[1]);
            client.local_port = (u16)(49152u + (random_port % 16384u));
            client.send_next = ((u32)initial_random[2] << 24) |
                               ((u32)initial_random[3] << 16) |
                               ((u32)initial_random[4] << 8) | initial_random[5];
            kmemset(initial_random, 0, sizeof(initial_random));
        } else {
            /* Legacy non-TLS network use remains available on older CPUs;
             * TLS itself refuses to run without a hardware entropy source. */
            client.local_port = (u16)(49152u + (entropy_u32() % 16384u));
            client.send_next = entropy_u32();
        }
    }
    if (client.local_port == port) client.local_port = (u16)(49152u + ((client.local_port - 49152u + 1u) % 16384u));
    client.peer_window = 65535u;
    client.send_unacknowledged = client.send_next;
    if (transmit(TCP_FLAG_SYN, 0, 0, 1) < 0) { net_tcp_init(); return -2; }
    client.send_next++;
    if (timeout_ticks < 100u) timeout_ticks = TCP_DEFAULT_TIMEOUT;
    retry_interval = timeout_ticks / 4u;
    if (retry_interval < 10u) retry_interval = 10u;
    next_retry = retry_interval;
    start = timer_ticks();
    while (timer_ticks() - start < timeout_ticks) {
        net_poll();
        if (client.state == TCP_ESTABLISHED) return 0;
        if (client.failed || client.state == TCP_CLOSED) return -3;
        if (timer_ticks() - start >= next_retry && next_retry < timeout_ticks) {
            client.send_next--;
            if (transmit(TCP_FLAG_SYN, 0, 0, 1) < 0) { net_tcp_init(); return -2; }
            client.send_next++;
            next_retry += retry_interval;
        }
    }
    client.state = TCP_CLOSED;
    return -4;
}

void net_tcp_input(const u8 source[4], const u8 destination[4],
                   const u8 *packet, u16 length) {
    u16 source_port, destination_port, header_length, data_length, window;
    u32 sequence, acknowledgment;
    u8 flags;
    u8 local_ip[4];
    if (!source || !destination || !packet || length < TCP_HEADER_SIZE || client.state == TCP_CLOSED) return;
    source_port = get16(packet); destination_port = get16(packet + 2);
    if (source_port != client.remote_port || destination_port != client.local_port ||
        kmemcmp(source, client.remote_ip, 4) != 0) return;
    header_length = (u16)((packet[12] >> 4) * 4u);
    if (header_length < TCP_HEADER_SIZE || header_length > length) return;
    net_get_ipv4_config(local_ip, 0, 0, 0);
    if (kmemcmp(destination, local_ip, 4) != 0 || tcp_checksum(source, destination, packet, length) != 0) return;
    sequence = get32(packet + 4);
    acknowledgment = get32(packet + 8);
    flags = packet[13];
    window = get16(packet + 14);
    if (flags & TCP_FLAG_RST) {
        if ((client.state == TCP_SYN_SENT && (flags & TCP_FLAG_ACK) && acknowledgment == client.send_next) ||
            (client.state != TCP_SYN_SENT && sequence == client.receive_next)) {
            client.failed = 1; client.state = TCP_CLOSED;
        }
        return;
    }
    if (client.state == TCP_SYN_SENT) {
        if ((flags & (TCP_FLAG_SYN | TCP_FLAG_ACK)) == (TCP_FLAG_SYN | TCP_FLAG_ACK) &&
            acknowledgment == client.send_next) {
            client.send_unacknowledged = acknowledgment;
            client.receive_next = sequence + 1u;
            client.peer_window = window;
            client.state = TCP_ESTABLISHED;
            (void)send_ack();
        }
        return;
    }
    data_length = (u16)(length - header_length);

    /* ACK state is independent of receive sequence state. A segment can carry
     * a valid ACK while its payload is out of order; discarding the ACK too
     * would stall our stop-and-wait sender and can deadlock TLS handshakes. */
    if (flags & TCP_FLAG_ACK) {
        if (sequence_after(acknowledgment, client.send_unacknowledged) &&
            !sequence_after(acknowledgment, client.send_next))
            client.send_unacknowledged = acknowledgment;
    }

    /* This small stack does not perform TCP segment reassembly. For future
     * data, send the current cumulative ACK so the peer can retransmit the
     * missing segment. Old duplicate data is acknowledged again as well. */
    if (sequence != client.receive_next) {
        if (sequence_after(sequence, client.receive_next)) {
            (void)send_ack();
        } else if ((u32)(client.receive_next - sequence) <= TCP_RX_CAPACITY &&
                   (data_length || (flags & TCP_FLAG_FIN))) {
            (void)send_ack();
        }
        return;
    }

    client.peer_window = window;
    if (sequence == client.receive_next && data_length) {
        u16 available = (u16)(TCP_RX_CAPACITY - client.receive_length);
        if (data_length <= available) {
            kmemcpy(client.receive_buffer + client.receive_length, packet + header_length, data_length);
            client.receive_length = (u16)(client.receive_length + data_length);
            client.receive_next += data_length;
        }
    }
    if ((flags & TCP_FLAG_FIN) && sequence + data_length == client.receive_next) {
        client.receive_next++;
        client.peer_closed = 1;
    }
    if (data_length || (flags & (TCP_FLAG_FIN | TCP_FLAG_SYN))) (void)send_ack();
}

int net_tcp_send(const void *data, u16 length, u32 timeout_ticks) {
    const u8 *bytes = (const u8 *)data;
    u16 sent = 0;
    if ((!data && length) || client.state != TCP_ESTABLISHED || client.failed) return -1;
    while (sent < length) {
        u16 chunk = (u16)(length - sent);
        u32 target, attempts = 0;
        if (chunk > TCP_TX_MSS) chunk = TCP_TX_MSS;
        if (!client.peer_window) {
            u64 window_start = timer_ticks();
            u32 window_timeout = timeout_ticks ? timeout_ticks : TCP_DEFAULT_TIMEOUT;
            while (!client.peer_window && !client.failed &&
                   client.state == TCP_ESTABLISHED &&
                   timer_ticks() - window_start < window_timeout)
                net_poll();
            if (client.failed || client.state != TCP_ESTABLISHED) return -4;
            if (!client.peer_window) return -2;
        }
        if (chunk > client.peer_window) chunk = client.peer_window;
        if (!chunk) return -2;
        target = client.send_next + chunk;
        {
            u32 segment_sequence = client.send_next;
            while (attempts < 3u) {
                client.send_next = segment_sequence;
                if (transmit(TCP_FLAG_ACK | TCP_FLAG_PSH, bytes + sent, chunk, 0) < 0) return -3;
                client.send_next = target;
                if (wait_for_ack(target, timeout_ticks ? timeout_ticks : 100u) == 0) break;
                if (client.failed) return -4;
                ++attempts;
            }
        }
        if (attempts >= 3u) return -5;
        sent = (u16)(sent + chunk);
    }
    return (int)sent;
}

int net_tcp_receive(void *buffer, u16 capacity, u32 timeout_ticks) {
    u64 start;
    u16 amount;
    if (!capacity) return 0;
    if (!buffer) return NET_TCP_IO_INVALID;
    if (client.failed) return NET_TCP_IO_RESET;
    if (client.state == TCP_CLOSED) return NET_TCP_IO_INVALID;
    start = timer_ticks();
    if (!timeout_ticks) timeout_ticks = TCP_DEFAULT_TIMEOUT;
    while (!client.receive_length && !client.peer_closed && !client.failed &&
           timer_ticks() - start < timeout_ticks) net_poll();
    if (!client.receive_length) {
        if (client.peer_closed) return 0;
        if (client.failed) return NET_TCP_IO_RESET;
        return NET_TCP_IO_TIMEOUT;
    }
    amount = client.receive_length;
    if (amount > capacity) amount = capacity;
    kmemcpy(buffer, client.receive_buffer, amount);
    client.receive_length = (u16)(client.receive_length - amount);
    if (client.receive_length)
        kmemmove(client.receive_buffer, client.receive_buffer + amount, client.receive_length);
    (void)send_ack(); /* Re-open the advertised receive window after consumption. */
    return (int)amount;
}

void net_tcp_close(u32 timeout_ticks) {
    if (client.state == TCP_CLOSED) return;
    if (client.state == TCP_ESTABLISHED && !client.local_fin_sent) {
        if (transmit(TCP_FLAG_FIN | TCP_FLAG_ACK, 0, 0, 0) == 0) {
            client.local_fin_sent = 1;
            client.send_next++;
            client.state = TCP_CLOSING;
            (void)wait_for_ack(client.send_next, timeout_ticks ? timeout_ticks : 50u);
        }
    }
    if (client.local_fin_sent) {
        u64 start = timer_ticks();
        while (!client.peer_closed && timer_ticks() - start < (timeout_ticks ? timeout_ticks : 50u)) net_poll();
    }
    client.state = TCP_CLOSED;
}
