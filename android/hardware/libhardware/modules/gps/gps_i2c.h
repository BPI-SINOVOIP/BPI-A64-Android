#ifndef __GPS_I2C_H__
#define __GPS_I2C_H__

#ifdef ANDROID
#define I2C_RDWR 0x0707

struct i2c_msg {
    __u16 addr; /* slave address            */
    __u16 flags;
#define I2C_M_TEN       0x0010  /* this is a ten bit chip address */
#define I2C_M_RD        0x0001  /* read data, from slave to master */
#define I2C_M_NOSTART       0x4000  /* if I2C_FUNC_NOSTART */
#define I2C_M_REV_DIR_ADDR  0x2000  /* if I2C_FUNC_PROTOCOL_MANGLING */
#define I2C_M_IGNORE_NAK    0x1000  /* if I2C_FUNC_PROTOCOL_MANGLING */
#define I2C_M_NO_RD_ACK     0x0800  /* if I2C_FUNC_PROTOCOL_MANGLING */
#define I2C_M_RECV_LEN      0x0400  /* length will be first received byte */
    __u16 len;      /* msg length               */
    __u8 *buf;      /* pointer to msg data          */
};

/* This is the structure as used in the I2C_RDWR ioctl call */
struct i2c_rdwr_ioctl_data {
    struct i2c_msg __user *msgs;    /* pointers to i2c_msgs */
    __u32 nmsgs;            /* number of i2c_msgs */
};
#endif

extern int i2c_pipe[2];
extern char *i2c_host;
extern int i2c_slave;

void start_i2c_read(void);
void stop_i2c_read(void);
void *gps_i2c_main_func(void *);

#endif
