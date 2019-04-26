#ifdef QL_GPS_LOG

//add by joe
//write gps log to file
#define QL_GPS_LOG_PATH "/mnt/ql_gps_log"
#define QL_LOG_PATH QL_GPS_LOG_PATH
//FILE* ql_log_fd;

//void ql_open_log(void);
void ql_write_log(const char *fmt,...);

//#define  QL_GPS_LOG_WRITE(fmt,...) ql_write_log("%s:%s[%d]:" fmt "",__FILE__,__FUNCTION__,__LINE__,##__VA_ARGS__)
#define  QL_GPS_LOG_WRITE(fmt,...) ql_write_log("%s[%d]:" fmt "",__FUNCTION__,__LINE__,##__VA_ARGS__)

#define QLOG(fmt,...) QL_GPS_LOG_WRITE(fmt,##__VA_ARGS__)

#define LOGD(fmt,...) QLOG(fmt,##__VA_ARGS__)
#define LOGE(fmt,...) QLOG(fmt,##__VA_ARGS__)
#define LOGI(fmt,...) QLOG(fmt,##__VA_ARGS__)

#else
//use system log , you can use logcat to get the log 
#define  LOG_TAG  "gps_ql"

#ifdef USE_NDK
#include <android/log.h>
#define LOGD(fmt, arg...) ((void)__android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, fmt, ##arg))

#else
#include <cutils/log.h>
#ifndef LOGD
#define LOGD ALOGD
#endif
#endif //ifdef USE_NDK
#endif

//just dummy defines since were not including syslog.h.
#define LOG_EMERG   0
#define LOG_ALERT   1
#define LOG_CRIT    2
#define LOG_ERR     3
#define LOG_WARNING 4
#define LOG_NOTICE  5
#define LOG_INFO    6
#define LOG_DEBUG   7

extern int LOG_LVL;

#define D(lvl,fmt,...) \
    do\
    {\
        if(lvl <= LOG_LVL)\
        {\
            LOGD(fmt,##__VA_ARGS__);\
        }\
        else{}\
    }while(0);
