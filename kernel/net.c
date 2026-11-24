#include "net.h"
#include "e1000.h"
#include "kstring.h"
#include "timer.h"

#define ETH_ARP 0x0806u
#define ETH_IPV4 0x0800u
#define ARP_REQUEST 1u
#define ARP_REPLY 2u
#define IP_ICMP 1u
#define ICMP_ECHO_REPLY 0u
#define ICMP_ECHO_REQUEST 8u
#define NET_FRAME_MAX 1500u

static int net_ready;
static u8 local_ip[4]={10,0,2,15};
static u8 gateway_ip[4]={10,0,2,2};
static u8 arp_ip[4];
static u8 arp_mac[6];
static int arp_valid;
static volatile int ping_reply;
static u16 ip_id=1;
static u8 tx_frame[NET_FRAME_MAX] __attribute__((aligned(16)));

static u16 read_be16(const u8 *p) { return (u16)(((u16)p[0]<<8)|p[1]); }
static void write_be16(u8 *p,u16 v) { p[0]=(u8)(v>>8); p[1]=(u8)v; }
static u16 checksum(const u8 *data,u32 length) {
    u32 sum=0;
    while (length>1) { sum+=((u16)data[0]<<8)|data[1]; data+=2; length-=2; }
    if (length) sum+=(u16)data[0]<<8;
    while (sum>>16) sum=(sum&0xFFFFu)+(sum>>16);
    return (u16)~sum;
}
static int ip_equal(const u8 *a,const u8 *b) { return kmemcmp(a,b,4)==0; }
static void ethernet_header(u8 *f,const u8 *dst,u16 type) {
    kmemcpy(f,dst,6); kmemcpy(f+6,e1000_mac_address(),6); write_be16(f+12,type);
}
static int send_arp(u16 operation,const u8 *target_mac,const u8 *target_ip) {
    u8 *p=tx_frame+14;
    ethernet_header(tx_frame,target_mac,ETH_ARP);
    write_be16(p,1); write_be16(p+2,ETH_IPV4); p[4]=6; p[5]=4;
    write_be16(p+6,operation); kmemcpy(p+8,e1000_mac_address(),6);
    kmemcpy(p+14,local_ip,4); kmemcpy(p+18,target_mac,6); kmemcpy(p+24,target_ip,4);
    return e1000_send(tx_frame,42);
}
static void arp_remember(const u8 *ip,const u8 *mac) { kmemcpy(arp_ip,ip,4); kmemcpy(arp_mac,mac,6); arp_valid=1; }
static int arp_lookup(const u8 *ip,u8 *mac) {
    if (!arp_valid || !ip_equal(ip,arp_ip)) return 0;
    kmemcpy(mac,arp_mac,6); return 1;
}
static void handle_arp(const u8 *p,u16 length) {
    if (length<28 || read_be16(p)!=1 || read_be16(p+2)!=ETH_IPV4 || p[4]!=6 || p[5]!=4) return;
    if (ip_equal(p+24,local_ip)) arp_remember(p+14,p+8);
    if (read_be16(p+6)==ARP_REQUEST && ip_equal(p+24,local_ip))
        (void)send_arp(ARP_REPLY,p+8,p+14);
}
static void handle_ipv4(const u8 *p,u16 length,const u8 *source_mac) {
    u8 ihl,protocol; u16 total;
    const u8 *src,*dst,*icmp; u16 icmp_len;
    if (length<20 || (p[0]>>4)!=4) return;
    ihl=(u8)((p[0]&15u)*4u); total=read_be16(p+2);
    if (ihl<20 || total<ihl || total>length || checksum(p,ihl)!=0) return;
    src=p+12; dst=p+16; protocol=p[9];
    if (!ip_equal(dst,local_ip)) return;
    /* Learn the peer only after the IPv4 header has passed length/checksum
       validation and the packet is actually addressed to this interface. */
    if (source_mac) arp_remember(src,source_mac);
    if (protocol!=IP_ICMP || total<ihl+8) return;
    icmp=p+ihl; icmp_len=(u16)(total-ihl);
    if (checksum(icmp,icmp_len)!=0) return;
    if (icmp[0]==ICMP_ECHO_REQUEST) {
        u8 *out=tx_frame+14; u16 ip_len=(u16)(20+icmp_len);
        ethernet_header(tx_frame,arp_mac,ETH_IPV4);
        kmemset(out,0,20); out[0]=0x45; write_be16(out+2,ip_len);
        write_be16(out+4,ip_id++); write_be16(out+6,0x4000); out[8]=64; out[9]=IP_ICMP;
        kmemcpy(out+12,local_ip,4); kmemcpy(out+16,src,4);
        write_be16(out+10,checksum(out,20));
        kmemcpy(out+20,icmp,icmp_len); out[20]=ICMP_ECHO_REPLY; out[22]=out[23]=0;
        write_be16(out+22,checksum(out+20,icmp_len));
        (void)e1000_send(tx_frame,(u16)(14+ip_len));
    } else if (icmp[0]==ICMP_ECHO_REPLY) ping_reply=1;
}
void net_receive_frame(const u8 *frame,u16 length) {
    u16 type;
    if (!frame || length<14) return;
    type=read_be16(frame+12);
    if (type==ETH_ARP) handle_arp(frame+14,(u16)(length-14));
    else if (type==ETH_IPV4) handle_ipv4(frame+14,(u16)(length-14),frame+6);
}
int net_init(void) {
    if (net_ready) return 0;
    if (e1000_init()<0) return -1;
    net_ready=1;
    /* QEMU user-mode networking defaults; this is deliberately explicit and
       is not advertised as DHCP configuration. */
    (void)send_arp(ARP_REQUEST,(const u8*)"\xff\xff\xff\xff\xff\xff",gateway_ip);
    return 0;
}
void net_poll(void) { if (net_ready) e1000_poll(); }
int net_get_status(net_status_t *s) {
    if (!s) return -1;
    kmemset(s,0,sizeof(*s)); s->initialized=net_ready;
    if (!net_ready) return -1;
    s->link_up=e1000_link_up(); kmemcpy(s->mac,e1000_mac_address(),6);
    kmemcpy(s->ipv4,local_ip,4); kmemcpy(s->gateway,gateway_ip,4);
    s->rx_frames=e1000_received_frames(); s->tx_frames=e1000_transmitted_frames(); return 0;
}
int net_ping(const u8 address[4],u32 timeout_ticks) {
    u8 target_mac[6], *ip, *icmp; u16 ip_len=28; u64 start;
    if (!net_ready || !address) return -1;
    if (!arp_lookup(address,target_mac)) {
        (void)send_arp(ARP_REQUEST,(const u8*)"\xff\xff\xff\xff\xff\xff",address);
        start=timer_ticks();
        while (timer_ticks()-start<timeout_ticks) {
            net_poll(); if (arp_lookup(address,target_mac)) break;
        }
        if (!arp_lookup(address,target_mac)) return -2;
    }
    ping_reply=0; ethernet_header(tx_frame,target_mac,ETH_IPV4);
    ip=tx_frame+14; kmemset(ip,0,20); ip[0]=0x45; write_be16(ip+2,ip_len);
    write_be16(ip+4,ip_id++); write_be16(ip+6,0x4000); ip[8]=64; ip[9]=IP_ICMP;
    kmemcpy(ip+12,local_ip,4); kmemcpy(ip+16,address,4); write_be16(ip+10,checksum(ip,20));
    icmp=ip+20; kmemset(icmp,0,8); icmp[0]=ICMP_ECHO_REQUEST; write_be16(icmp+4,0xD309); write_be16(icmp+6,1);
    write_be16(icmp+2,checksum(icmp,8));
    if (e1000_send(tx_frame,42)<0) return -3;
    start=timer_ticks();
    while (timer_ticks()-start<timeout_ticks) { net_poll(); if (ping_reply) return 0; }
    return -4;
}
