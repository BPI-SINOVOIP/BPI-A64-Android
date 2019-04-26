#include <pthread.h>
#include "extract.h"

static pthread_mutex_t extract_rw_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t nmea_ok_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t nmea_ok_cond = PTHREAD_COND_INITIALIZER;

struct {
    short inst_id; // 1-NMEA, 2-DBG, 3-HBD
    short dat_idx;
    short dat_siz;
} id_que[NMEA_ID_QUE_SIZE];

static char rx_que[NMEA_RX_QUE_SIZE];
static unsigned short id_que_head;
static unsigned short id_que_tail;
static unsigned short rx_que_head;
static RX_SYNC_STATE_T rx_state;
static unsigned int u4SyncPkt;
static unsigned int u4OverflowPkt;
static unsigned int u4PktInQueue;

//Queue Functions

void Que_init_rx(void)
{
    /*-----------------------------
	variables
	-------------------------------*/
    short i = 0;

    /*-----------------------------
	initialize queue indexes
	-------------------------------*/
    id_que_head = 0;
    id_que_tail = 0;
    rx_que_head = 0;

    /*-----------------------------
    intialize identification queue
	-------------------------------*/
    for (i = 0; i < NMEA_ID_QUE_SIZE; i++) {
        id_que[i].inst_id = -1;
        id_que[i].dat_idx = 0;
    }

    /*-----------------------------
	intialize receive state
	-------------------------------*/
    rx_state = RXS_ETX;

    /*-----------------------------
    intialize statistic information
	-------------------------------*/
    u4SyncPkt = 0;
    u4OverflowPkt = 0;
    u4PktInQueue = 0;
}

/*************************************************************
 * PROCEDURE NAME:
 *   Que_inst_avail - Get available NMEA sentence information
 *
 * DESCRIPTION:
 *  inst_id - NMEA sentence type
 *  dat_idx - start data index in queue
 *  dat_siz - NMEA sentence size
 **************************************************************/
int Que_inst_avail(short *inst_id, short *dat_idx, short *dat_siz)
{
    /*---------------------------------------------
    variables
	-----------------------------------------------*/
    int inst_avail = 0;

    /*---------------------------------------------
    if packet is available then return id and index
	-----------------------------------------------*/
    if (id_que_tail != id_que_head) {
        *inst_id = id_que[id_que_tail].inst_id;
        *dat_idx = id_que[id_que_tail].dat_idx;
        *dat_siz = id_que[id_que_tail].dat_siz;
        id_que[id_que_tail].inst_id = -1;
        id_que_tail = (id_que_tail + 1) & (unsigned short)(NMEA_ID_QUE_SIZE - 1);
        inst_avail = 1;
        if (u4PktInQueue > 0) {
            u4PktInQueue--;
        }
    } else {
        inst_avail = 0;
    }
    return inst_avail;
} /* Que_inst_avail() end */

/********************************************************
 * PROCEDURE NAME:
 *  Que_get_inst - Get available NMEA sentence from queue
 *
 * DESCRIPTION:
 *  idx - start data index in queue
 *  size - NMEA sentence size
 *  data - data buffer used to save NMEA sentence
 ********************************************************/
void Que_get_inst(short idx, short size, void *data)
{
    /*-------------------------------------------------
    variables
	---------------------------------------------------*/
    short i;
    unsigned char *ptr;

    /*-------------------------------------------------
    copy data from the receive queue to the data buffer
	---------------------------------------------------*/
    ptr = (unsigned char *)data;
    for (i = 0; i < size; i++) {
        *ptr = rx_que[idx];
        ptr++;
        idx = (idx + 1) & (unsigned short)(NMEA_RX_QUE_SIZE - 1);
    }
} /* Que_get_inst() end */

/*************************************************************************
 * PROCEDURE NAME:
 *   Que_rx_nmea - Receive NMEA code
 *
 * DESCRIPTION:
 *  The procedure fetch the characters between/includes '$' and <CR>
 *  That is, character <CR><LF> is skipped.
 *  and the maximum size of the sentence fetched by this procedure is 256
 *  $XXXXXX*AA
 *************************************************************************/
void Que_rx_nmea(unsigned char data)
{
    /*-----------------------------------
    determine the receive state
    -------------------------------------*/
    if (data == IOP_LF_DATA) {
        return;
    }
    switch (rx_state) {
    case RXS_DAT:
        switch (data) {
        case IOP_CR_DATA:
            // count total number of sync packets
            u4SyncPkt += 1;

            id_que_head = (id_que_head + 1) & (unsigned short)(NMEA_ID_QUE_SIZE - 1);
            if (id_que_tail == id_que_head) {
                // count total number of overflow packets
                u4OverflowPkt += 1;
                id_que_tail = (id_que_tail + 1) & (unsigned short)(NMEA_ID_QUE_SIZE - 1);
            } else {
                u4PktInQueue++;
            }
            rx_state = RXS_ETX;
            /*--------------------------------------------
            set RxEvent signaled
			----------------------------------------------*/
            //TODO send event
            notify_nmea_ok();
            break;
        case IOP_START_NMEA:
            // Restart NMEA sentence collection
            rx_state = RXS_DAT;
            id_que[id_que_head].inst_id = 1;
            id_que[id_que_head].dat_idx = rx_que_head;
            id_que[id_que_head].dat_siz = 0;
            rx_que[rx_que_head] = data;
            rx_que_head = (rx_que_head + 1) & (unsigned short)(NMEA_RX_QUE_SIZE - 1);
            id_que[id_que_head].dat_siz++;
            break;
        default:
            rx_que[rx_que_head] = data;
            rx_que_head = (rx_que_head + 1) & (unsigned short)(NMEA_RX_QUE_SIZE - 1);
            id_que[id_que_head].dat_siz++;

            // if NMEA sentence length > 256, stop NMEA sentence collection
            if (id_que[id_que_head].dat_siz == MAX_NMEA_STN_LEN) {
                id_que[id_que_head].inst_id = -1;
                rx_state = RXS_ETX;
            }
            break;
        } /* switch (data) end */
        break;
    case RXS_ETX:
        if (data == IOP_START_NMEA) {
            rx_state = RXS_DAT;
            id_que[id_que_head].inst_id = 1;
            id_que[id_que_head].dat_idx = rx_que_head;
            id_que[id_que_head].dat_siz = 0;
            rx_que[rx_que_head] = data;
            rx_que_head = (rx_que_head + 1) & (unsigned short)(NMEA_RX_QUE_SIZE - 1);
            id_que[id_que_head].dat_siz++;
        }
        break;
    default:
        rx_state = RXS_ETX;
        break;
    } /* switch (rx_state) end */
} /* Que_rx_nmea() end */

void i2c_lock(void)
{
    pthread_mutex_lock(&extract_rw_mutex);
}

void i2c_unlock(void)
{
    pthread_mutex_unlock(&extract_rw_mutex);
}

void notify_nmea_ok(void)
{
    pthread_mutex_lock(&nmea_ok_mutex);
    pthread_cond_broadcast(&nmea_ok_cond);
    pthread_mutex_unlock(&nmea_ok_mutex);
}

void wait_nmea_ok(void)
{
    pthread_mutex_lock(&nmea_ok_mutex);
    pthread_cond_wait(&nmea_ok_cond, &nmea_ok_mutex);
    pthread_mutex_unlock(&nmea_ok_mutex);
}

unsigned int getSyncPkt(void)
{
    return u4SyncPkt;
}
