#include "net.h"
#include "e1000.h"
#include "kstring.h"
#include <stdio.h>
#include <stdlib.h>

static const u8 test_mac[6]={0x52,0x54,0x00,0x12,0x34,0x56};
static const u8 peer_mac[6]={0x52,0x54,0x00,0xAA,0xBB,0xCC};
static u8 sent[2048]; static u16 sent_len; static u64 ticks;
int e1000_init(void) { return 0; }
int e1000_send(const void *p,u16 n) { if(n>sizeof(sent)) return -1; kmemcpy(sent,p,n); sent_len=n; return 0; }
void e1000_poll(void) {}
int e1000_link_up(void) { return 1; }
const u8 *e1000_mac_address(void) { return test_mac; }
u32 e1000_received_frames(void) { return 0; }
u32 e1000_transmitted_frames(void) { return 0; }
u64 timer_ticks(void) { return ticks++; }
static void be16(u8 *p,u16 v) { p[0]=(u8)(v>>8); p[1]=(u8)v; }
static u16 sum(const u8 *p,u32 n) { u32 s=0; while(n>1){s+=((u16)p[0]<<8)|p[1];p+=2;n-=2;} if(n)s+=(u16)p[0]<<8; while(s>>16)s=(s&65535)+(s>>16); return (u16)~s; }
#define CHECK(x) do { if(!(x)){fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#x);exit(1);} } while(0)

int main(void) {
    u8 frame[128]={0}; net_status_t status;
    CHECK(net_init()==0); CHECK(net_get_status(&status)==0); CHECK(status.link_up);
    /* ARP who-has 10.0.2.15 from the QEMU gateway. */
    for(int i=0;i<6;i++) frame[i]=0xFF;
    kmemcpy(frame+6,peer_mac,6); be16(frame+12,0x0806);
    be16(frame+14,1); be16(frame+16,0x0800); frame[18]=6; frame[19]=4; be16(frame+20,1);
    kmemcpy(frame+22,peer_mac,6); frame[28]=10; frame[29]=0; frame[30]=2; frame[31]=2;
    kmemset(frame+32,0,6); frame[38]=10; frame[39]=0; frame[40]=2; frame[41]=15;
    net_receive_frame(frame,42);
    CHECK(sent_len==42); CHECK(sent[12]==0x08 && sent[13]==0x06);
    CHECK(sent[20]==0 && sent[21]==2); CHECK( kmemcmp(sent+32,peer_mac,6)==0);
    /* ICMP echo request addressed to our static QEMU guest address. */
    kmemset(frame,0,sizeof(frame)); kmemcpy(frame, test_mac,6); kmemcpy(frame+6,peer_mac,6); be16(frame+12,0x0800);
    u8 *ip=frame+14; ip[0]=0x45; be16(ip+2,28); ip[8]=64; ip[9]=1;
    ip[12]=10;ip[13]=0;ip[14]=2;ip[15]=2; ip[16]=10;ip[17]=0;ip[18]=2;ip[19]=15;
    be16(ip+10,sum(ip,20)); ip[20]=8; be16(ip+24,0x1234); be16(ip+26,1); be16(ip+22,sum(ip+20,8));
    net_receive_frame(frame,42);
    CHECK(sent_len==42); CHECK(sent[12]==0x08 && sent[13]==0x00);
    CHECK(sent[34]==0); CHECK(sum(sent+14,20)==0); CHECK(sum(sent+34,8)==0);
    puts("Network protocol tests passed: ARP response, IPv4 validation, ICMP echo reply, checksums.");
    return 0;
}
