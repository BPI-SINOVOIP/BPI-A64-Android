//#include<unistd.h>
//#include<sys/types.h>
//#include<sys/stat.h>
//#include<fcntl.h>
#include <stdarg.h>
#include "ql-log.h"
#include <stdio.h>

int LOG_LVL = LOG_INFO;

#ifdef QL_GPS_LOG

void ql_write_log(const char *fmt, ...)
{
    FILE* ql_log_fd;
    ql_log_fd = fopen(QL_LOG_PATH,"a+");
    if(ql_log_fd == NULL)
    {
        return;
    }

    va_list vl;
    va_start(vl, fmt);
    
//    do
//    {
        vfprintf(ql_log_fd,fmt,vl);
        fprintf(ql_log_fd,"\n");
//    }while(fmt!='\0');

    va_end(vl);
    fclose(ql_log_fd);

}
#endif
