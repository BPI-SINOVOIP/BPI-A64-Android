#include <stdio.h>
#include <unistd.h>
#ifndef ANDROID
#include <linux/i2c.h>
#include <linux/i2c-dev.h>
#endif
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <signal.h>
#include <pthread.h>
#include <time.h>

#include "extract.h"
#include "gps_i2c.h"
#include "ql-log.h"
#define I2C_BUF_LEN 256

static pthread_t i2c_read_thread = 0;
static int sub_loop = 1;
static int main_loop = 1;

int i2c_pipe[2] = {-1, -1};
char *i2c_host = NULL;
int i2c_slave = 0;
static int i2c_read_stop = 1;

void start_i2c_read(void)
{
    D(LOG_INFO, "starting read from i2c!!!\n");
    i2c_read_stop = 1;
    Que_init_rx();
    i2c_read_stop = 0;
}

void stop_i2c_read(void)
{
    D(LOG_INFO, "stopping read from i2c!!!\n");
    i2c_read_stop = 1;
}

static void *i2c_read_func(void *param)
{
    int i2c_fd = -1;
    int ret = 0;
    struct i2c_rdwr_ioctl_data io;
    struct i2c_msg msg;
    __u8 buf[I2C_BUF_LEN] = {0};
    int i = 0;
    struct timespec req = {0, 5*1000000};
    unsigned int syncPkt = 0;

    D(LOG_INFO, "%s begin!!!\n", __func__);
    i2c_fd = open(i2c_host, O_RDWR | O_NONBLOCK);
    if (i2c_fd < 0) {
       D(LOG_ERR,"open i2c_host failed, errno = %d(%s)\n", errno, strerror(errno));
       goto out;
    }

    memset(&io, 0, sizeof(io));
    memset(&msg, 0, sizeof(msg));
    msg.addr = (__u16)i2c_slave;
    msg.flags = I2C_M_RD;
    msg.len = I2C_BUF_LEN - 1;
    msg.buf = buf;
    io.msgs = &msg;
    io.nmsgs = 1;

    while (sub_loop) {
        if (i2c_read_stop) {
            sleep(1);
            continue;
        }
        memset(buf, 0, sizeof(buf));
        ret = ioctl(i2c_fd, I2C_RDWR, &io);
        if (ret < 0) {
            D(LOG_ERR, "read i2c failed, errno = %d(%s)\n", errno, strerror(errno));
        } else {
            #if 0
            D(LOG_INFO, "read i2c successed!!!!\n");
            for (i = 0; i < (I2C_BUF_LEN - 1); i++) {
                if ((0 == (i%16)) && (0 != i)) {
                    D(LOG_INFO, "\n");
                }
                D(LOG_INFO, "%02x ", buf[i]);
            }
            D(LOG_INFO, "\n");
            #endif
        }

        i2c_lock();
        for (i = 0; i < (I2C_BUF_LEN - 1); i++) {
            Que_rx_nmea((unsigned char)buf[i]);
        }
        if (syncPkt != getSyncPkt()) { //got entire nmea pkt
            req.tv_sec = 0;
            req.tv_nsec = 100*1000000;
            syncPkt = getSyncPkt();
        } else {
            req.tv_sec = 0;
            req.tv_nsec = 5*1000000;
        }
        i2c_unlock();
        nanosleep(&req, NULL);
    }

out:
    if (i2c_fd > 0) {
        close(i2c_fd);
        i2c_fd = -1;
    }
    D(LOG_INFO, "%s end!!!\n", __func__);
    return NULL;
}

void *gps_i2c_main_func(void *param)
{
    short inst_id = 0;
    short dat_idx = -1;
    short dat_siz = 0;
    char nmea_buf[MAX_NMEA_STN_LEN + 2] = {0};
    int len = 0;
    int ret = 0;

    D(LOG_INFO, "%s begin!!!\n", __func__);
    if ((NULL == i2c_host) || (0 == i2c_slave)) {
        D(LOG_ERR, "invalid i2c_host or i2c_slave, i2c_host = %s, i2c_slave = 0x%02x\n", i2c_host, i2c_slave);
        goto out;
    } else {
        D(LOG_INFO, "i2c_host: %s, i2c_slave: 0x%02x\n", i2c_host, i2c_slave);
    }

    Que_init_rx();
    pthread_create(&i2c_read_thread, NULL, i2c_read_func, NULL);

    while (main_loop) {
        wait_nmea_ok();
        i2c_lock();
        while (!i2c_read_stop && Que_inst_avail(&inst_id, &dat_idx, &dat_siz)) {
            if ((1 == inst_id) && (-1 != dat_idx)) {
                memset(nmea_buf, 0, sizeof(nmea_buf));
                Que_get_inst(dat_idx, dat_siz, nmea_buf);
                nmea_buf[dat_siz] = '\n';
                for (len = 0; 
                    (len != (dat_siz + 1)) && 
                    ((ret = write(i2c_pipe[1], &(nmea_buf[len]), dat_siz + 1 -len) >= 0)); 
                    len += ((ret > 0) ? ret : 0));
            }
        }
        i2c_unlock();
    }

out:
    if (0 != i2c_read_thread) {
        pthread_join(i2c_read_thread, NULL);
    }
    D(LOG_DEBUG,"%s end!!!\n", __func__);
    return NULL;
}
