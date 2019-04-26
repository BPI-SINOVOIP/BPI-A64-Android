#ifndef QL_GPS_CONFIG_H
#define QL_GPS_CONFIG_H

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <termios.h>
#include <stdlib.h>
#include <errno.h>

#define GPS_CONFIG_FILE_PATH "/etc/gps_cfg.inf"

extern char MODULE_TYPE[10];
//extern char NMEA_PORT_PATH[50];
extern char QL_GPS_CHANNEL[50];
extern int  BAUD_RATE;

extern int baud_rates[];
extern speed_t baud_bits[];
extern int baud_rate_index(int baud_rate);
//extern void get_config(void);
//extern void read_config(char *config_file_path);

extern int check_module_type(void);

#endif

