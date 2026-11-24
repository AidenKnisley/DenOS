#include "e1000.h"
#include "pci.h"
#include "kstring.h"

#define E1000_VENDOR 0x8086u
#define E1000_DEVICE 0x100Eu
#define REG_CTRL   0x0000u
#define REG_STATUS 0x0008u
#define REG_EERD   0x0014u
#define REG_ICR    0x00C0u
#define REG_IMC    0x00D8u
#define REG_RCTL   0x0100u
#define REG_TCTL   0x0400u
#define REG_TIPG   0x0410u
#define REG_RDBAL  0x2800u
#define REG_RDBAH  0x2804u
#define REG_RDLEN  0x2808u
#define REG_RDH    0x2810u
#define REG_RDT    0x2818u
#define REG_TDBAL  0x3800u
#define REG_TDBAH  0x3804u
#define REG_TDLEN  0x3808u
#define REG_TDH    0x3810u
#define REG_TDT    0x3818u
#define REG_RAL    0x5400u
#define REG_RAH    0x5404u
#define RX_COUNT 16u
#define TX_COUNT 8u
#define BUFFER_SIZE 2048u

typedef struct __attribute__((packed)) {
    u64 address; u16 length; u16 checksum; u8 status; u8 errors; u16 special;
} e1000_rx_desc_t;
typedef struct __attribute__((packed)) {
    u64 address; u16 length; u8 cso; u8 command; u8 status; u8 css; u16 special;
} e1000_tx_desc_t;

static volatile u32 *mmio;
static e1000_rx_desc_t rx_desc[RX_COUNT] __attribute__((aligned(16)));
static e1000_tx_desc_t tx_desc[TX_COUNT] __attribute__((aligned(16)));
static u8 rx_buffers[RX_COUNT][BUFFER_SIZE] __attribute__((aligned(16)));
static u8 tx_buffers[TX_COUNT][BUFFER_SIZE] __attribute__((aligned(16)));
static u8 mac[6];
static u32 rx_index, tx_index, rx_frames, tx_frames;
static int initialized;
extern void net_receive_frame(const u8 *frame, u16 length);

static u32 reg_read(u32 offset) { return mmio[offset >> 2]; }
static void reg_write(u32 offset,u32 value) { mmio[offset >> 2]=value; (void)mmio[REG_STATUS >> 2]; }
static void pause_io(void) { for (volatile u32 i=0;i<1000;i++) __asm__ volatile("pause"); }

int e1000_init(void) {
    pci_device_t dev;
    u32 bar, base, command;
    if (initialized) return 0;
    if (pci_find_device(E1000_VENDOR,E1000_DEVICE,&dev)<0) return -1;
    bar=dev.bar[0];
    if (!bar || (bar & 1u) || (bar & 6u)==4u) return -2; /* MMIO, 32-bit BAR only */
    base=bar & ~0xFu;
    if (!base) return -3;
    if (pci_enable_bus_master(&dev)<0) return -4;
    mmio=(volatile u32 *)(usize)base;
    reg_write(REG_IMC,0xFFFFFFFFu);
    reg_write(REG_CTRL,reg_read(REG_CTRL)|(1u<<26));
    for (u32 i=0;i<100000u;i++) { if (!(reg_read(REG_CTRL)&(1u<<26))) break; pause_io(); }
    if (reg_read(REG_CTRL)&(1u<<26)) return -6;
    /* Read the factory MAC from the receive-address registers. */
    u32 ral=reg_read(REG_RAL), rah=reg_read(REG_RAH);
    mac[0]=(u8)ral; mac[1]=(u8)(ral>>8); mac[2]=(u8)(ral>>16); mac[3]=(u8)(ral>>24);
    mac[4]=(u8)rah; mac[5]=(u8)(rah>>8);
    if ((mac[0]|mac[1]|mac[2]|mac[3]|mac[4]|mac[5])==0 || (mac[0]&1u)) return -5;
    for (u32 i=0;i<RX_COUNT;i++) {
        rx_desc[i].address=(u64)(usize)&rx_buffers[i][0];
        rx_desc[i].length=0; rx_desc[i].checksum=0; rx_desc[i].status=0; rx_desc[i].errors=0; rx_desc[i].special=0;
    }
    for (u32 i=0;i<TX_COUNT;i++) {
        tx_desc[i].address=(u64)(usize)&tx_buffers[i][0];
        tx_desc[i].length=0; tx_desc[i].cso=0; tx_desc[i].command=0; tx_desc[i].status=1; tx_desc[i].css=0; tx_desc[i].special=0;
    }
    reg_write(REG_RDBAL,(u32)(usize)&rx_desc[0]); reg_write(REG_RDBAH,0);
    reg_write(REG_RDLEN,sizeof(rx_desc)); reg_write(REG_RDH,0); reg_write(REG_RDT,RX_COUNT-1);
    reg_write(REG_TDBAL,(u32)(usize)&tx_desc[0]); reg_write(REG_TDBAH,0);
    reg_write(REG_TDLEN,sizeof(tx_desc)); reg_write(REG_TDH,0); reg_write(REG_TDT,0);
    reg_write(REG_TIPG,0x0060200Au);
    reg_write(REG_RCTL,(1u<<1)|(1u<<15)); /* receiver + broadcast; 2 KiB buffers */
    reg_write(REG_TCTL,(1u<<1)|(1u<<3)|(0x10u<<4)|(0x40u<<12));
    command=reg_read(REG_CTRL); (void)command;
    initialized=1; rx_index=tx_index=rx_frames=tx_frames=0;
    return 0;
}

int e1000_send(const void *frame,u16 length) {
    e1000_tx_desc_t *d;
    u32 spins=100000u;
    if (!initialized || !frame || length<14 || length>BUFFER_SIZE) return -1;
    d=&tx_desc[tx_index];
    while (!(d->status&1u) && spins--) pause_io();
    if (!(d->status&1u)) return -2;
    kmemcpy(tx_buffers[tx_index],frame,length);
    d->length=length; d->cso=0; d->css=0; d->special=0;
    d->command=0x0Bu; /* EOP | IFCS | RS */ d->status=0;
    tx_index=(tx_index+1u)%TX_COUNT;
    reg_write(REG_TDT,tx_index); tx_frames++;
    return 0;
}

void e1000_poll(void) {
    if (!initialized) return;
    for (u32 count=0;count<RX_COUNT;count++) {
        e1000_rx_desc_t *d=&rx_desc[rx_index];
        if (!(d->status&1u)) break;
        if (!(d->errors) && d->length>=14 && d->length<=BUFFER_SIZE) {
            net_receive_frame(rx_buffers[rx_index],d->length); rx_frames++;
        }
        d->length=0; d->checksum=0; d->errors=0; d->status=0; d->special=0;
        reg_write(REG_RDT,rx_index);
        rx_index=(rx_index+1u)%RX_COUNT;
    }
}
int e1000_link_up(void) { return initialized && (reg_read(REG_STATUS)&2u)!=0; }
const u8 *e1000_mac_address(void) { return mac; }
u32 e1000_received_frames(void) { return rx_frames; }
u32 e1000_transmitted_frames(void) { return tx_frames; }
