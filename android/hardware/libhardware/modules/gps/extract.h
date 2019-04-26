#ifndef __EXTRACT_H__
#define __EXTRACT_H__

#define IOP_LF_DATA 0x0A // <LF>
#define IOP_CR_DATA 0x0D // <CR>
#define IOP_START_DBG 0x23 // MTK debug log start char '#'
#define IOP_START_NMEA 0x24 // NMEA start char '$'
#define IOP_START_HBD1 'H' // MTK HBD debug log start char 'H'
#define IOP_START_HBD2 'B'
#define IOP_START_HBD3 'D'
#define NMEA_ID_QUE_SIZE 0x0100
#define NMEA_RX_QUE_SIZE 0x8000
#define MAX_NMEA_STN_LEN 256

typedef enum {
    RXS_DAT_HBD, // receive HBD data
    RXS_PRM_HBD2, // receive HBD preamble 2
    RXS_PRM_HBD3, // receive HBD preamble 3
    RXS_DAT, // receive NMEA data
    RXS_ETX, // End-of-Packet
} RX_SYNC_STATE_T;

void Que_init_rx(void);
int Que_inst_avail(short *inst_id, short *dat_idx, short *dat_siz);
void Que_get_inst(short idx, short size, void *data);
void Que_rx_nmea(unsigned char data);
void i2c_lock(void);
void i2c_unlock(void);
void notify_nmea_ok(void);
void wait_nmea_ok(void);
unsigned int getSyncPkt(void);

#endif
