#include <string.h>
#include "config.h"
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "ql-log.h"

#include "gps_i2c.h"

#define SUPPORT_MODULE_TYPE_NUM 12
//support module list
static char *support_module_type[SUPPORT_MODULE_TYPE_NUM] = 
{
    //Qualcomm
    "UC20",
    //MTK
    //MTK platform GPS module,we can use PMTK to check module type
    "L70",
    "L76",
    "L80",
    "L86",
    "L26",
    "L76B",
    "L70-R",
    "L35",
    //SiRF
    "L20",
    "L30",
    "L50"
};

#define CFG_NUM 14
#define CFG_CONTENT_LENGTH 50
static char *QL_head[CFG_NUM] = 
{
    "MODULE_TYPE",
    "NMEA_PORT_PATH",
    "BAUD_RATE",
    "DATA_BITS",
    "STOP_BITS",
    "PARITY_TYPE",
    "FLOW_CONTROL",
    "LOG_LEVEL",
    "LOG_TYPE",
    "LOG_PATH",
    "FUNC_NMEA",
    "FUNC_XTRA",
    "FUNC_AGPS",
    "I2C_ADDR"
};

static char value[CFG_NUM][CFG_CONTENT_LENGTH];

#define TRUE    1
#define FALSE   0

#define MODULE_TYPE_MAX_LENGTH 10
char MODULE_TYPE[MODULE_TYPE_MAX_LENGTH];
//char NMEA_PORT_PATH[50];
char QL_GPS_CHANNEL[50];
int  BAUD_RATE;

int baud_rates[] = {
    0, 9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600};
speed_t baud_bits[] = {
    0, B9600, B19200, B38400, B57600, B115200, B230400, B460800, B921600};

int baud_rate_index(int baud_rate);
void get_config(void);
void read_config(char *config_file_path);

int is_supported_gps_module(char *module_typ);

char *memory_match(char *source, unsigned int source_len, char *search, unsigned int search_len)
{
    unsigned int index = 0,i;

    if(source == NULL || search == NULL)
        return NULL;

    while(index <= (source_len - search_len))
    {
        if(source[index] != search[0])
        {
            index++;
            continue;
        }

        for(i = 0; i < search_len; i++)
        {
            if(source[index + i] != search[i])
                break;
        }

        if(i == search_len)
        {
            break;
        }        
        index++;
    }

    if(index <= (source_len - search_len))
        return source + index;
    else
        return NULL;
}

int check_module_type(void)
{
    int fd;
    char buf[4096];
    int retry = 0;
    int n = 0;

    get_config();
    return 0; //donot check now!
    if(is_supported_gps_module(MODULE_TYPE) == FALSE)
    {
        printf("%s not in support list.\n",MODULE_TYPE);
        return -1;
    }

    if(!strncmp(MODULE_TYPE,"UC20",4) || !strncmp(MODULE_TYPE,"L20",3) ||
      !strncmp(MODULE_TYPE,"L30",3) || !strncmp(MODULE_TYPE,"L50",3))
    {
        return 0;
    }

    fd = open(QL_GPS_CHANNEL,O_RDWR);
    if(fd < 0)
    {
        printf("Open %s error!\n",QL_GPS_CHANNEL);
        return -1;
    }
    //configure serial port
    {
        struct termios ios;
        tcgetattr( fd, &ios );
        ios.c_lflag = 0;  /* disable ECHO, ICANON, etc... */
        ios.c_oflag &= (~ONLCR); /* Stop \n -> \r\n translation on output */
        ios.c_iflag &= (~(ICRNL | INLCR)); /* Stop \r -> \n & \n -> \r translation on input */
        ios.c_iflag |= (IGNCR | IXOFF);  /* Ignore \r & XON/XOFF on input */

        cfsetispeed(&ios, baud_bits[baud_rate_index(BAUD_RATE)]);
        cfsetospeed(&ios, baud_bits[baud_rate_index(BAUD_RATE)]);
        tcsetattr( fd, TCSANOW, &ios );
    }

#define RETRY_TIMES 5
    while(retry++ < RETRY_TIMES)
    {
        write(fd,"$PMTK605*31\r\n",13);
        do
        {
            n += read(fd, buf + n, 4095);
            printf("%s\n",buf);
            if(memory_match(buf,4096,MODULE_TYPE,strlen(MODULE_TYPE)) != NULL)
            {
                close(fd);
                return 0;
            }
        }while(n < 4000);
    }

    close(fd);
    return -1;
}

int is_supported_gps_module(char *module_typ)
{
    int i;

    if(module_typ == NULL || module_typ[0] == '\0')
        return FALSE;

    for(i = 0; i < SUPPORT_MODULE_TYPE_NUM; i++)
    {
        if(strstr(support_module_type[i],module_typ) != NULL)
            return TRUE;
    }

    return FALSE;
}


void read_config(char *config_file_path)
{
    FILE *file;
    char buf[128];;
    int i = 0;
    char *ph = NULL,*pt = NULL;

    memset(value,'*',CFG_NUM * CFG_CONTENT_LENGTH);

    file = fopen(config_file_path,"r");
    if (file == NULL) {
        D(LOG_ERR, "fail to read %s for errno: %d (%s)\n", config_file_path, errno, strerror(errno));
        return;
    }
       
    while(fgets(buf, sizeof(buf) - 1,file) != NULL)
    {
        if (buf[0] == '#' || buf[0] == ';' || buf[0] == '\0' || buf[0] == '\r' || buf[0] == '\n')
            continue;
        for(i = 0;i < CFG_NUM;i++)
        {
            if(strstr(buf,QL_head[i]) != NULL)
            {
                ph = strchr(buf,'=');
                if(ph == NULL)
                {
                    D(LOG_ERR, "No =\n");
                    continue;
                }
                ph++;

                pt = strchr(ph,' ');
                if(pt == NULL)
                {
                    pt =  &buf[strlen(buf)] - 1;
                    while (*pt == '\r' || *pt == '\n')
                        *pt-- = '\0';
                }
                
                if(pt == ph)
                {
                    D(LOG_ERR, "No content\n");
                    continue;
                }

                strcpy(value[i], ph);
            }
            }
        }
    fclose(file);
    }

//#define CONFIG_TEST

#ifdef CONFIG_TEST
#undef GPS_CONFIG_FILE_PATH
#define GPS_CONFIG_FILE_PATH "gps_cfg.inf"
int main()
{
    int ret;

    //get_config();

    //ret = is_supported_gps_module(MODULE_TYPE);
    ret = check_module_type();
    printf("ret:%d\n",ret);

    return 0;
}
#endif

int baud_rate_index(int baud_rate)
{
    unsigned int i;
    for (i = 0; i < sizeof(baud_rates) / sizeof(*baud_rates); ++i)
        if (baud_rates[i] == baud_rate)
            return i;
    return -1;
}

void get_config()
{
    int i = -1;
    
    read_config(GPS_CONFIG_FILE_PATH);
    
    for(i = 0; i < CFG_NUM; i++)
    {
        if(value[i][0] == '*')
            continue;
        switch(i)
        {
            case 0:
                strcpy(MODULE_TYPE,value[i]);
            break;
            case 1:
                strcpy(QL_GPS_CHANNEL,value[i]);
            break;
            case 2:
            {
                int j;
                for(j = 0;value[i][j] != '\0';j++)
                    if(!isdigit(value[i][j]))
                    {
                        BAUD_RATE = 115200;
                        break;
                    }
                if(value[i][j] == '\0')
                    BAUD_RATE = atoi(value[i]);
            }
            break;
            case 3:
            break;
            case 4:
            break;
            case 5:
            break;
            case 6:
            break;
            case 7:
            break;
            case 8:
            break;
            case 9:
            break;
            case 10:
            break;
            case 11:
            break;
            case 12:
            break;
            case 13:
            {
                int j;
                for(j = 0; value[i][j] != '\0'; j++) {
                    if (!isdigit(value[i][j])) {
                        i2c_slave = 16; //default i2c addr
                        break;
                    }
                }
                if (value[i][j] == '\0') {
                    i2c_slave = atoi(value[i]);
                }
            }
            break;
            default:
            break;
        }
    }
    
    D(LOG_NOTICE, "[%s] [%s] [%d]\n",MODULE_TYPE,QL_GPS_CHANNEL,BAUD_RATE);
}

