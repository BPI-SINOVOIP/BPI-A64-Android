/* 
 * QUECTEL GPS Driver
 * Author: Joe.Wang
 * this implements a GPS hardware library for the Android.
 * the following code should be built as a shared library that will be
 * placed into /system/lib/hw/gps.default.so
 *
 * it will be loaded by the code in hardware/libhardware/hardware.c
 * which is itself called from android_location_GpsLocationProvider.cpp
 */

#include <errno.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <math.h>
#include <time.h>
#include <termios.h>
#include <linux/socket.h>
typedef unsigned short sa_family_t;
#include <linux/un.h>

#include "config.h"

#define DRIVER_VERSION "Quectel_Android_GPS_Driver_V1.5.8"

#include "ql-log.h"

#ifdef USE_NDK
#include "hardware/gps.h"
#else
#include <cutils/sockets.h>
#include <hardware/gps.h>
#endif
//#include <hardware/qemud.h>
#define I2C_DEV_PREFIX "/dev/i2c"

#define GPS_CMD_RETRY_TIMES 1*100 //N*100 N:s 

#define GPS_STATE_UNKNOWN  0  //unknown
#define GPS_STATE_OPENED   1  //opened
#define GPS_STATE_CLOSED   2  //closed
#define GPS_STATE_ENABLED  3  //enabled
#define GPS_STATE_DISABLED 4  //disabled
#define GPS_STATE_ERROR    5

#include "gps_i2c.h"
pthread_t gps_i2c_main_thread = 0;

volatile int gps_state = GPS_STATE_UNKNOWN;

typedef struct {
    const char*  p;
    const char*  end;
} Token;

//NOTE:the default value is 16,but the GSV messages's length is longer than 16,modified 16 to 20
#define  MAX_NMEA_TOKENS  20

typedef struct {
    int     count;
    Token   tokens[ MAX_NMEA_TOKENS ];
} NmeaTokenizer;

typedef struct {
    int                     init;
    int                     fd;
    GpsCallbacks            callbacks;
    pthread_t               thread;
    int                     control[2];
    int gps_power_state;
    int debug_nema;
} GpsState;

static GpsState  _gps_state[1];

static GpsSvStatus gps_sv_status;
static GpsSvStatus bd_gps_sv_status;
static GpsSvStatus gnss_gps_sv_status;
unsigned char gps_sv_status_flag = 0;

typedef struct
{
    //used satellites total number
    unsigned int num;
    //used satellites's prn
    int NO[32];
}UsedSatellites;

static UsedSatellites used_satellites;
static UsedSatellites bd_used_satellites;

/*
void InitliazeGPSState()
{
    GpsState*  state = _gps_state;
    state->init       = 0;
    state->control[0] = -1;
    state->control[1] = -1;
    state->fd         = -1;
	state->thread     = 0;
}*/

static int
nmea_tokenizer_init( NmeaTokenizer*  t, const char*  p, const char*  end )
{
///{{{
    int    count = 0;

    // the initial '$' is optional
    if (p < end && p[0] == '$')
        p += 1;

    // remove trailing newline
    if (end > p && end[-1] == '\n') {
        end -= 1;
        if (end > p && end[-1] == '\r')
            end -= 1;
    }

    // get rid of checksum at the end of the sentecne
    if (end >= p+3 && end[-3] == '*') {
        end -= 3;
    }

    while (p < end) {
        const char*  q = p;

        q = memchr(p, ',', end-p);
        if (q == NULL)
            q = end;
        //NOTE:q > p => q >= p
        //this is for BUG: blank tok could not be analyzed correct 
        if (q >= p) {
            if (count < MAX_NMEA_TOKENS) {
                t->tokens[count].p   = p;
                t->tokens[count].end = q;
                count += 1;
            }
        }
        if (q < end)
            q += 1;

        p = q;
    }

    t->count = count;
    return count;
//}}}
}

static Token
nmea_tokenizer_get( NmeaTokenizer*  t, int  index )
{
//{{{
    Token  tok;
    static const char*  dummy = "";

    if (index < 0 || index >= t->count) {
        tok.p = tok.end = dummy;
    } else
        tok = t->tokens[index];

    return tok;
//}}}
}


static int
str2int( const char*  p, const char*  end )
{
///{{{
    int   result = 0;
    int   len    = end - p;

    for ( ; len > 0; len--, p++ )
    {
        int  c;

        if (p >= end)
            goto Fail;

        c = *p - '0';
        if ((unsigned)c >= 10)
            goto Fail;

        result = result*10 + c;
    }
    return  result;

Fail:
    return -1;
//}}}
}

static double
str2float( const char*  p, const char*  end )
{
///{{{
    int   len    = end - p;
    char  temp[16];

    if (len >= (int)sizeof(temp))
        return 0.;

    memcpy( temp, p, len );
    temp[len] = 0;
    return strtod( temp, NULL );
//}}}
}


#define  NMEA_MAX_SIZE  83

typedef struct {
    int     pos;
    int     overflow;
    int     utc_year;
    int     utc_mon;
    int     utc_day;
    int     utc_diff;
    GpsLocation  fix;
    gps_location_callback  callback;
    char    in[ NMEA_MAX_SIZE+1 ];
} NmeaReader;

static void
nmea_reader_update_utc_diff( NmeaReader*  r )
{
///{{{
    time_t         now = time(NULL);
    struct tm      tm_local;
    struct tm      tm_utc;
    long           time_local, time_utc;

    gmtime_r( &now, &tm_utc );
    localtime_r( &now, &tm_local );

    time_local = tm_local.tm_sec +
                 60*(tm_local.tm_min +
                 60*(tm_local.tm_hour +
                 24*(tm_local.tm_yday +
                 365*tm_local.tm_year)));

    time_utc = tm_utc.tm_sec +
               60*(tm_utc.tm_min +
               60*(tm_utc.tm_hour +
               24*(tm_utc.tm_yday +
               365*tm_utc.tm_year)));
	r->utc_diff = time_local - time_utc;
//}}}
}


static void
nmea_reader_init( NmeaReader*  r )
{
///{{{
    memset( r, 0, sizeof(*r) );

    r->pos      = 0;
    r->overflow = 0;
    r->utc_year = -1;
    r->utc_mon  = -1;
    r->utc_day  = -1;

    r->callback = NULL;
    r->fix.size = sizeof(r->fix);

    nmea_reader_update_utc_diff( r );
//}}}
}


static void
nmea_reader_set_callback( NmeaReader*  r, gps_location_callback  cb )
{
///{{{
    r->callback = cb;
    if (cb != NULL && r->fix.flags != 0) {
        D(LOG_INFO,"%s: sending latest fix to new callback", __FUNCTION__);
        r->callback( &r->fix );
        r->fix.flags = 0;
    }
//}}}
}


static int
nmea_reader_update_time( NmeaReader*  r, Token  tok )
{
///{{{
    int        hour, minute;
    double     seconds;
    struct tm  tm;
    time_t     fix_time;

    if (tok.p + 6 > tok.end)
        return -1;

    if (r->utc_year < 0) {
        // no date yet, get current one
        time_t  now = time(NULL);
        gmtime_r( &now, &tm );
        r->utc_year = tm.tm_year + 1900;
        r->utc_mon  = tm.tm_mon + 1;
        r->utc_day  = tm.tm_mday;
    }

    hour    = str2int(tok.p,   tok.p+2);
    minute  = str2int(tok.p+2, tok.p+4);
    seconds = str2float(tok.p+4, tok.end);

    tm.tm_hour  = hour;
    tm.tm_min   = minute;
    tm.tm_sec   = (int) seconds;
    tm.tm_year  = r->utc_year - 1900;
    tm.tm_mon   = r->utc_mon - 1;
    tm.tm_mday  = r->utc_day;
    tm.tm_isdst = -1;

    r->utc_diff = (r->fix.longitude / fabs(r->fix.longitude)) * ((int)(fabs(r->fix.longitude) - 7.5) / 15 + 1) * 3600;

    fix_time = mktime( &tm ) + r->utc_diff;
    r->fix.timestamp = (long long)fix_time * 1000;
    return 0;
//}}}
}

static int
nmea_reader_update_date( NmeaReader*  r, Token  date, Token  time )
{
///{{{
    Token  tok = date;
    int    day, mon, year;

    if (tok.p + 6 != tok.end) {
        D(LOG_INFO,"date not properly formatted: '%.*s'", (int)(tok.end-tok.p), tok.p);
        return -1;
    }
    day  = str2int(tok.p, tok.p+2);
    mon  = str2int(tok.p+2, tok.p+4);
    year = str2int(tok.p+4, tok.p+6) + 2000;

    if ((day|mon|year) < 0) {
        D(LOG_INFO,"date not properly formatted: '%.*s'", (int)(tok.end-tok.p), tok.p);
        return -1;
    }

    r->utc_year  = year;
    r->utc_mon   = mon;
    r->utc_day   = day;

    return nmea_reader_update_time( r, time );
//}}}
}


static double
convert_from_hhmm( Token  tok )
{
///{{{
    double  val     = str2float(tok.p, tok.end);
    int     degrees = (int)(floor(val) / 100);
    double  minutes = val - degrees*100.;
    double  dcoord  = degrees + minutes / 60.0;
    return dcoord;
//}}}
}


static int
nmea_reader_update_latlong( NmeaReader* r,Token latitude,char latitudeHemi,Token longitude,char longitudeHemi )
{
///{{{
    double   lat, lon;
    Token    tok;

    tok = latitude;
    if (tok.p + 6 > tok.end) {
        D(LOG_DEBUG,"latitude is too short: '%.*s'", (int)(tok.end-tok.p), tok.p);
        return -1;
    }
    lat = convert_from_hhmm(tok);
    if (latitudeHemi == 'S')
        lat = -lat;

    tok = longitude;
    if (tok.p + 6 > tok.end) {
        D(LOG_DEBUG,"longitude is too short: '%.*s'", (int)(tok.end-tok.p), tok.p);
        return -1;
    }
    lon = convert_from_hhmm(tok);
    if (longitudeHemi == 'W')
        lon = -lon;

    r->fix.flags    |= GPS_LOCATION_HAS_LAT_LONG;
    r->fix.latitude  = lat;
    r->fix.longitude = lon;
    return 0;
//}}}
}


static int
nmea_reader_update_altitude( NmeaReader*  r,
                             Token        altitude,
                             Token        units )
{
///{{{
    Token   tok = altitude;

    if (tok.p >= tok.end)
        return -1;

    r->fix.flags   |= GPS_LOCATION_HAS_ALTITUDE;
    r->fix.altitude = str2float(tok.p, tok.end);
    return 0;
//}}}
}


static int
nmea_reader_update_bearing( NmeaReader*  r,
                            Token        bearing )
{
///{{{
    Token   tok = bearing;
    float   value = 0.0f;
#if 0
    if (tok.p >= tok.end)
        return -1;
#endif

    if (tok.p < tok.end) {
        value = str2float(tok.p, tok.end);
    } else if ((tok.p >= tok.end) && (r->fix.flags & GPS_LOCATION_HAS_BEARING)) {
        return 0;
    }

    r->fix.flags   |= GPS_LOCATION_HAS_BEARING;
    r->fix.bearing  = value;
    return 0;
//}}}
}


static int
nmea_reader_update_speed( NmeaReader*  r,
                          Token        speed )
{
///{{{
    Token   tok = speed;

    if (tok.p >= tok.end)
        return -1;

    r->fix.flags   |= GPS_LOCATION_HAS_SPEED;
    r->fix.speed    = (1.852 / 3.6) * str2float(tok.p, tok.end); //1 knots -> 1.852km / hr -> 1.852 * 1000 / 3600 m/s
    return 0;
//}}}
}

/*
 * parse NMEA sentence
 */
static void
nmea_reader_parse( NmeaReader*  r )
{
///{{{
   /* we received a complete sentence, now parse it to generate
    * a new GPS fix...
    */

    GpsState*  state = _gps_state;

    NmeaTokenizer  tzer[1];
    Token tok;

    if (state->debug_nema > 0) {
        D(LOG_INFO,"Received: '%.*s'", r->pos-1, r->in);
        state->debug_nema--;
    }
    D(LOG_DEBUG,"Received: '%.*s'", r->pos-1, r->in);
    if (r->pos < 9) 
    {
        D(LOG_INFO,"Too short. discarded.");
        return;
    }

    nmea_tokenizer_init(tzer, r->in, r->in + r->pos);
#if 0
    {
        int  n;
        D(LOG_INFO,"Found %d tokens", tzer->count);
        for (n = 0; n < tzer->count; n++) {
            Token  tok = nmea_tokenizer_get(tzer,n);
            D(LOG_INFO,"%2d: '%.*s'", n, (int)(tok.end-tok.p), tok.p);
        }
    }
#endif

    tok = nmea_tokenizer_get(tzer, 0);
    if (tok.p + 5 > tok.end) 
    {
        D(LOG_INFO,"sentence id '%.*s' too short, ignored.", (int)(tok.end-tok.p), tok.p);
        return;
    }

    if (state->callbacks.nmea_cb) 
    {//update nmea sentence
        time_t         now = time(NULL);

        state->callbacks.nmea_cb(now,r->in,r->pos);
    }

    // ignore first two characters.
    int is_bd = (tok.p[0] == 'B' && tok.p[1] == 'D');
    int is_gnss = (tok.p[0] == 'G' && tok.p[1] == 'L');
    tok.p += 2;
    if ( !memcmp(tok.p, "GGA", 3) ) 
    {
        // GPS fix
        Token  tok_time          = nmea_tokenizer_get(tzer,1);
        Token  tok_latitude      = nmea_tokenizer_get(tzer,2);
        Token  tok_latitudeHemi  = nmea_tokenizer_get(tzer,3);
        Token  tok_longitude     = nmea_tokenizer_get(tzer,4);
        Token  tok_longitudeHemi = nmea_tokenizer_get(tzer,5);
        Token  tok_altitude      = nmea_tokenizer_get(tzer,9);
        Token  tok_altitudeUnits = nmea_tokenizer_get(tzer,10);

        nmea_reader_update_latlong(r, tok_latitude,
                                      tok_latitudeHemi.p[0],
                                      tok_longitude,
                                      tok_longitudeHemi.p[0]);
        nmea_reader_update_altitude(r, tok_altitude, tok_altitudeUnits);
        nmea_reader_update_time(r, tok_time);
    } 
    else if ( !memcmp(tok.p, "GSA", 3) ) 
    {
        Token  tok_accuracy = nmea_tokenizer_get(tzer,15);
        r->fix.accuracy = str2float(tok_accuracy.p,tok_accuracy.end);
        r->fix.flags |= GPS_LOCATION_HAS_ACCURACY;
        
        int i;
        int used_satellite_NO;
        for(i = 0;i < 12;i++)
        {
            Token tok_used_satellite_NO = nmea_tokenizer_get(tzer,3 + i);
            used_satellite_NO = str2int(tok_used_satellite_NO.p,tok_used_satellite_NO.end);
            if(used_satellite_NO == 0)
                break;
            if (is_bd) {
                bd_used_satellites.num++;
                bd_used_satellites.NO[i] = used_satellite_NO;
            } else {
                used_satellites.num++;
                used_satellites.NO[i] = used_satellite_NO;
            }
        }
        gps_sv_status_flag |= 0x01;
    } 
    else if ( !memcmp(tok.p, "GSV", 3) ) 
    {
        //Token tok_message_total_num = nmea_tokenizer_get(tzer,1);
        //volatile int messages_total_num = str2int(tok_message_total_num.p,tok_message_total_num.end);
        Token tok_message_NO = nmea_tokenizer_get(tzer,2);
        volatile int messages_NO = str2int(tok_message_NO.p,tok_message_NO.end);
        Token tok_satellites_in_view = nmea_tokenizer_get(tzer,3);
        volatile int satellites_in_view = str2int(tok_satellites_in_view.p,tok_satellites_in_view.end);
        GpsSvStatus *pGpsSvStatus = &gps_sv_status;
        if (is_bd)
            pGpsSvStatus = &bd_gps_sv_status;
        else if (is_gnss)
             pGpsSvStatus = &gnss_gps_sv_status;
        else
            gps_sv_status_flag |= (1 << messages_NO);

        pGpsSvStatus->size    = sizeof(GpsSvStatus);
        pGpsSvStatus->num_svs = satellites_in_view;

        volatile int tmp;
        if((satellites_in_view / 4) < messages_NO)
        {
            tmp = satellites_in_view % 4;
        }
        else
        {
            tmp = 4;
        }

        D(LOG_DEBUG,"satellites_in_view = %d messages_NO = %d tmp = %d\n",satellites_in_view,messages_NO,tmp);
        
        int i;
        for(i = 0; i < tmp;i++)
        {
            pGpsSvStatus->sv_list[i + 4 * (messages_NO - 1)].size = sizeof(GpsSvInfo);
            
            Token tok_prn = nmea_tokenizer_get(tzer,4 + i * 4);
            int prn = str2int(tok_prn.p,tok_prn.end);
                
            pGpsSvStatus->sv_list[i + 4 * (messages_NO - 1)].prn = prn;
            
            Token tok_elevation = nmea_tokenizer_get(tzer,5 + i * 4);
            float elevation = str2float(tok_elevation.p,tok_elevation.end);
            pGpsSvStatus->sv_list[i + 4 * (messages_NO - 1)].elevation = elevation;
            
            Token tok_azimuth = nmea_tokenizer_get(tzer,6 + i * 4);
            float azimuth = str2float(tok_azimuth.p,tok_azimuth.end);
            pGpsSvStatus->sv_list[i + 4 * (messages_NO - 1)].azimuth = azimuth;
            
            Token tok_snr = nmea_tokenizer_get(tzer,7 + i * 4);
            float snr = str2float(tok_snr.p,tok_snr.end);
            pGpsSvStatus->sv_list[ i + 4 * (messages_NO - 1)].snr = snr;

            D(LOG_DEBUG,"prn:%d elevation:%f azimuth:%f snr:%f\n",prn,elevation,azimuth,snr);
        }
    
        D(LOG_DEBUG,"gps_sv_status_flag = %d\n", gps_sv_status_flag);
    }
    else if ( !memcmp(tok.p, "VTG", 3))
	{
        Token  tok_bearing       = nmea_tokenizer_get(tzer,1);
        Token  tok_speed         = nmea_tokenizer_get(tzer,5);
        nmea_reader_update_bearing( r, tok_bearing );
        nmea_reader_update_speed( r, tok_speed );
	}
    else if ( !memcmp(tok.p, "RMC", 3) ) 
    {
        Token  tok_time          = nmea_tokenizer_get(tzer,1);
        Token  tok_fixStatus     = nmea_tokenizer_get(tzer,2);
        Token  tok_latitude      = nmea_tokenizer_get(tzer,3);
        Token  tok_latitudeHemi  = nmea_tokenizer_get(tzer,4);
        Token  tok_longitude     = nmea_tokenizer_get(tzer,5);
        Token  tok_longitudeHemi = nmea_tokenizer_get(tzer,6);
        Token  tok_speed         = nmea_tokenizer_get(tzer,7);
        Token  tok_bearing       = nmea_tokenizer_get(tzer,8);
        Token  tok_date          = nmea_tokenizer_get(tzer,9);

        D(LOG_DEBUG,"in RMC, fixStatus=%c", tok_fixStatus.p[0]);
        if (tok_fixStatus.p[0] == 'A')
        {

            nmea_reader_update_latlong( r, tok_latitude,
                                           tok_latitudeHemi.p[0],
                                           tok_longitude,
                                           tok_longitudeHemi.p[0] );
            nmea_reader_update_date( r, tok_date, tok_time );

            nmea_reader_update_bearing( r, tok_bearing );
            nmea_reader_update_speed  ( r, tok_speed );
        }

#if 1
        int bd_sv;
        for (bd_sv = 0; bd_sv < bd_gps_sv_status.num_svs; bd_sv++) {
            if (gps_sv_status.num_svs < GPS_MAX_SVS) {
                memcpy(&gps_sv_status.sv_list[gps_sv_status.num_svs], &bd_gps_sv_status.sv_list[bd_sv], sizeof(GpsSvInfo));
                if (gps_sv_status.sv_list[gps_sv_status.num_svs].prn <= 32)
                    gps_sv_status.sv_list[gps_sv_status.num_svs].prn += 160;
                //D(LOG_INFO, "add bd %d", gps_sv_status.sv_list[gps_sv_status.num_svs].prn);
                gps_sv_status.num_svs++;
            }
        }
        int gnss_sv;
        for (gnss_sv = 0; gnss_sv < gnss_gps_sv_status.num_svs; gnss_sv++) {
            if (gps_sv_status.num_svs < GPS_MAX_SVS) {
                memcpy(&gps_sv_status.sv_list[gps_sv_status.num_svs], &gnss_gps_sv_status.sv_list[gnss_sv], sizeof(GpsSvInfo));
                //D(LOG_INFO, "add gnss %d", gps_sv_status.sv_list[gps_sv_status.num_svs].prn);
                gps_sv_status.num_svs++;
            }
        }
#endif

        bzero(&bd_gps_sv_status,sizeof(bd_gps_sv_status));
        bzero(&bd_used_satellites,sizeof(bd_used_satellites));
        bzero(&gnss_gps_sv_status,sizeof(gnss_gps_sv_status));

//{{{
        {
            //set mask
            unsigned int i;
            gps_sv_status.used_in_fix_mask &= 0x00;
            
            for(i = 0;i < used_satellites.num;i++)
            {
                if (used_satellites.NO[i] <= 32)
                    gps_sv_status.used_in_fix_mask |= (0x01 << (used_satellites.NO[i] - 1)); 
            }
            
            //callbacks
            D(LOG_DEBUG,"CALL sv_status_cb\n");
            gps_sv_status_flag = 0;
            if (state->callbacks.sv_status_cb) {
#if 0
                int num_svs;
                D(LOG_INFO,"CALL sv_status_cb num_svs = %d\n", gps_sv_status.num_svs);
                for (num_svs = 0; num_svs < gps_sv_status.num_svs; num_svs++) {
                    D(LOG_INFO, "sv_list[%02d]: prn = %d, snr = %f\n", num_svs, gps_sv_status.sv_list[num_svs].prn, gps_sv_status.sv_list[num_svs].snr);
                }
#endif
                state->callbacks.sv_status_cb(&gps_sv_status);
            }
            bzero(&gps_sv_status,sizeof(gps_sv_status));
            bzero(&used_satellites,sizeof(used_satellites));       
    } 
//}}}
    } 
    else 
    {
        tok.p -= 2;
        D(LOG_DEBUG,"unknown sentence '%.*s", (int)(tok.end-tok.p), tok.p);
    }
    if (r->fix.flags == 0x1f)
    {
        char   temp[256];
        char*  p   = temp;
        char*  end = p + sizeof(temp);
        struct tm   utc;

        p += snprintf( p, end-p, "sending fix" );
        if (r->fix.flags & GPS_LOCATION_HAS_LAT_LONG) 
        {
            p += snprintf(p, end-p, " lat=%g lon=%g", r->fix.latitude, r->fix.longitude);
        }
        if (r->fix.flags & GPS_LOCATION_HAS_ALTITUDE) 
        {
            p += snprintf(p, end-p, " altitude=%g", r->fix.altitude);
        }
        if (r->fix.flags & GPS_LOCATION_HAS_SPEED) 
        {
            p += snprintf(p, end-p, " speed=%g", r->fix.speed);
        }
        if (r->fix.flags & GPS_LOCATION_HAS_BEARING) 
        {
            p += snprintf(p, end-p, " bearing=%g", r->fix.bearing);
        }
        if (r->fix.flags & GPS_LOCATION_HAS_ACCURACY) 
        {
            p += snprintf(p,end-p, " accuracy=%g", r->fix.accuracy);
        }
        gmtime_r( (time_t*) &r->fix.timestamp, &utc );
        p += snprintf(p, end-p, " time=%s", asctime( &utc ) );
        D(LOG_DEBUG,"%s",temp);
        
        if (r->callback) 
        {
            r->callback( &r->fix );
            r->fix.flags &= 0x0;
            r->fix.latitude = 0.0;
            r->fix.longitude = 0.0;
            r->fix.altitude = 0.0;
            r->fix.speed = 0.0;
            r->fix.bearing = 0.0;
            r->fix.accuracy = 0.0;

        }
        else 
        {
            D(LOG_DEBUG,"no callback, keeping data until needed !");
        }
    }
//}}}
}


static void
nmea_reader_addc( NmeaReader*  r, int  c )
{
///{{{
    if (r->overflow) {
        r->overflow = (c != '\n');
        return;
    }

    if (r->pos >= (int) sizeof(r->in)-1 ) {
        r->overflow = 1;
        r->pos      = 0;
        return;
    }

    if ((r->pos == 0) && (c != '$')) {
        D(LOG_DEBUG, "ignore 0x%x - %c", c, (char)c);
        return;
    }

    r->in[r->pos] = (char)c;
    r->pos       += 1;

    if (c == '\n') {
        nmea_reader_parse( r );
        r->pos = 0;
    }
//}}}
}

static void call_gps_state_callback(int state)
{
//{{{
    GpsState*  s = _gps_state;
    GpsStatus gps_status;
    
    gps_status.size = sizeof(GpsStatus);

    switch(state)
    {
        case GPS_STATE_OPENED:
            gps_status.status = GPS_STATUS_SESSION_BEGIN;
            D(LOG_DEBUG,"set gps status to GPS_STATUS_SESSION_BEGIN");
            break;
        case GPS_STATE_CLOSED:
            gps_status.status = GPS_STATUS_SESSION_END;
            D(LOG_DEBUG,"set gps status to GPS_STATUS_SESSION_END");
            break;
        case GPS_STATE_ENABLED:
            gps_status.status = GPS_STATUS_ENGINE_ON;
            D(LOG_DEBUG,"set gps status to GPS_STATUS_ENGINE_ON");
            break;
        case GPS_STATE_DISABLED:
            gps_status.status = GPS_STATUS_ENGINE_OFF;
            D(LOG_DEBUG,"set gps status to GPS_STATUS_ENGINE_OFF");
            break;
        default:
            gps_status.status = GPS_STATUS_NONE;
            D(LOG_DEBUG,"set gps status to GPS_STATUS_NONE");
            break;
    }
    s->callbacks.status_cb(&gps_status);
//}}}
}


/* commands sent to the gps thread */
enum {
    CMD_QUIT  = 0,
    CMD_START = 1,
    CMD_STOP  = 2
};

static int
epoll_register( int  epoll_fd, int  fd )
{
///{{{
    struct epoll_event  ev;
    int                 ret, flags;

    /* important: make the fd non-blocking */
    flags = fcntl(fd, F_GETFL);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    ev.events  = EPOLLIN;
    ev.data.fd = fd;
    do {
        ret = epoll_ctl( epoll_fd, EPOLL_CTL_ADD, fd, &ev );
    } while (ret < 0 && errno == EINTR);
    return ret;
//}}}
}

#if 0
static int
epoll_deregister( int  epoll_fd, int  fd )
{
///{{{
    int  ret;
    do {
        ret = epoll_ctl( epoll_fd, EPOLL_CTL_DEL, fd, NULL );
    } while (ret < 0 && errno == EINTR);
    return ret;
//}}}
}
#endif

typedef struct _GPS_TLV {
   int type;
   int length;
   unsigned char data[0];
} GPS_TLV;

static pthread_mutex_t s_commandmutex = PTHREAD_MUTEX_INITIALIZER;
void* send_command_to_module_thread(void *arg) {
    D(LOG_DEBUG,"%s[%d]--enter!",__FUNCTION__,__LINE__);
    int s = -1;
    int count = 0;

    if (((GPS_TLV *)arg)->type == 0) { //stop gps
        sleep(3); //delay 3 seconds to stop gps, to advoid user very frequently stop&start gps
        if (_gps_state[0].gps_power_state)
            return NULL;
    }

    pthread_mutex_lock(&s_commandmutex);

    while ((s < 0) && (count++ < 30))
    {
        struct sockaddr_un addr;
        struct sockaddr_un *p_addr = &addr;
        const char *name = "rild-gps";
            
        s = socket(AF_LOCAL, SOCK_STREAM, 0);
        if (s < 0) continue;

        memset (p_addr, 0, sizeof (*p_addr));
        p_addr->sun_family = AF_LOCAL;
        p_addr->sun_path[0] = 0;
        memcpy(p_addr->sun_path + 1, name, strlen(name) );

        if(connect(s, (struct sockaddr *) &addr, strlen(name) + offsetof(struct sockaddr_un, sun_path) + 1) < 0) {
            D(LOG_ERR, "Error connecting rild-gps (%s)\n", strerror(errno));
            close(s); s = -1;
            sleep(1);
        }
    }

    if (s > 0)
    {
        write(s, arg, sizeof(GPS_TLV)+((GPS_TLV *)arg)->length);
        sleep(1);
        close(s);
    }
    free(arg);

    pthread_mutex_unlock(&s_commandmutex);

    D(LOG_DEBUG,"%s[%d] exit!",__FUNCTION__,__LINE__);
    return ((void *)0);
}

#if 0
static int check_channel(void)
{
//{{{
    if (access(QL_GPS_CHANNEL,R_OK) == 0)
    {
        return 0;
    }
    else 
    {
        return -1;
    }
//}}}
}
#endif

void deal_cmd(int gps_fd, const int cmd)
{
///{{{
    D(LOG_DEBUG,"%s[%d]--enter and cmd is %d!",__FUNCTION__,__LINE__,cmd);

#if 0
    if(check_channel() != 0)
    {
        return;
    }
#endif

    if (strncmp(MODULE_TYPE,"UC20",4) == 0 || strncmp(MODULE_TYPE,"EC20",4) == 0
            || strncmp(MODULE_TYPE,"EC21",4) == 0 || strncmp(MODULE_TYPE,"EC25",4) == 0
            || (!strcmp(QL_GPS_CHANNEL, "rild-nmea"))) {
        GPS_TLV *tlv = ( GPS_TLV *)malloc(sizeof(GPS_TLV));
        pthread_t deal_cmd_thread;
        pthread_attr_t attr;
        pthread_attr_init (&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        tlv->type = (cmd == CMD_START);
        tlv->length = 0;
        if (cmd == CMD_START && _gps_state[0].debug_nema == 0)
            _gps_state[0].debug_nema = 30;
        pthread_create (&deal_cmd_thread, &attr, &send_command_to_module_thread, (void *)tlv);
    }
    _gps_state[0].gps_power_state = (cmd == CMD_START);

    if (gps_fd > 0) //Fatal signal 11 (SIGSEGV) at 0x00000000 (code=1), thread 646 (LocationManager)
        call_gps_state_callback(gps_state);

    D(LOG_DEBUG,"%s[%d]--exit!",__FUNCTION__,__LINE__);
    //}}}
}


//{{{
static void
gps_state_done( GpsState*  s )
{
///{{{
    D(LOG_INFO,"%s[%d]--enter!",__FUNCTION__,__LINE__);
    // tell the thread to quit, and wait for it
    deal_cmd(s->fd,CMD_QUIT);

#if 0
    write( s->control[0], &cmd, 1 );
#endif
    D(LOG_DEBUG,"%s[%d]--SEND CMD_QUIT!",__FUNCTION__,__LINE__);
    // KENT-CHANGE 2014-07-03{
    //pthread_join(s->thread, &dummy);
    //pthread_join(s->thread, &dummy);
	//}

#if 0
    while(gps_state != GPS_STATE_DISABLED)
    {
        D(LOG_DEBUG,"switch state ...!");
        usleep(100);
    }
    D(LOG_DEBUG,"switch state OK!");
#endif
    if (!strcmp(QL_GPS_CHANNEL, "rild-nmea") && s->fd > 0) {
        write(s->fd, "close rild-nema", strlen("close rild-nema"));
        D(LOG_INFO, "close rild-nema");
    }
    sleep(1);
    
    // close connection to the QEMU GPS daemon
    if (s->fd  > 0) {close( s->fd ); s->fd = -1;}
    s->init = 0;

    // close the control socket pair
    if (s->control[0] > 0) {close( s->control[0] ); s->control[0] = -1;}
    if (s->control[1] > 0) {close( s->control[1] ); s->control[1] = -1;}
//}}}
}


static void
gps_state_start( GpsState*  s )
{
///{{{
    D(LOG_INFO,"%s[%d]--enter!",__FUNCTION__,__LINE__);
    
    deal_cmd(s->fd,CMD_START);
#if 0
    do { ret=write( s->control[0], &cmd, 1 ); }
    while (ret < 0 && errno == EINTR);

    if (ret != 1)
        D(LOG_ERR,"%s: could not send CMD_START command: ret=%d: %s",
          __FUNCTION__, ret, strerror(errno));
#endif
//}}}
}


static void
gps_state_stop( GpsState*  s )
{
///{{{
    D(LOG_INFO,"%s[%d]--enter!",__FUNCTION__,__LINE__);

    deal_cmd(s->fd,CMD_STOP);
#if 0
    do { ret=write( s->control[0], &cmd, 1 ); }
    while (ret < 0 && errno == EINTR);

    if (ret != 1)
        D(LOG_ERR,"%s: could not send CMD_STOP command: ret=%d: %s",
          __FUNCTION__, ret, strerror(errno));
#endif
//}}}
}
//}}}
/* this is the main thread, it waits for commands from gps_state_start/stop and,
 * when started, messages from the GPS daemon. these are simple NMEA sentences
 * that must be parsed to be converted into GPS fixes sent to the framework
 */
static void
gps_state_thread(void* arg)
{
///{{{
    D(LOG_DEBUG,"%s[%d]--enter!",__FUNCTION__,__LINE__);
    GpsState*   state = (GpsState*) arg;
    NmeaReader  reader[1];
    int         epoll_fd   = epoll_create(2);
    int         started    = 0;
    int         gps_fd     = state->fd;
    int         control_fd = state->control[1];

    nmea_reader_init( reader );

    // register control file descriptors for polling
    epoll_register( epoll_fd, control_fd );
    epoll_register( epoll_fd, gps_fd );

    D(LOG_INFO,"gps thread running");
    //joe for test
    nmea_reader_set_callback( reader, state->callbacks.location_cb );
    // now loop
    for (;;) {
        struct epoll_event   events[2];
        int                  ne, nevents;

        nevents = epoll_wait( epoll_fd, events, 2, -1 );
        if (nevents < 0) {
            if (errno != EINTR)
                D(LOG_ERR,"epoll_wait() unexpected error: %s", strerror(errno));
            continue;
        }
        D(LOG_DEBUG,"gps thread received %d events", nevents);
        for (ne = 0; ne < nevents; ne++) {
            if ((events[ne].events & (EPOLLERR|EPOLLHUP)) != 0) {
                D(LOG_ERR,"EPOLLERR or EPOLLHUP after epoll_wait() !?");
                //Joe
                //2014-4-4
                gps_state = GPS_STATE_ERROR;

				// KENT-CHANGE{//
                //close(gps_fd);
				// close connection to the QEMU GPS daemon
                if (!strcmp(QL_GPS_CHANNEL, "rild-nmea") && state->fd > 0) {
                    write(state->fd, "close rild-nema", strlen("close rild-nema"));
                    D(LOG_INFO, "close rild-nema");
                }
                if (state->fd > 0) {close(state->fd); state->fd = -1;}
                state->init = 0;
                // close the control socket pair
                if (state->control[0] > 0) {close( state->control[0] ); state->control[0] = -1;}
                if (state->control[1] > 0) {close( state->control[1] ); state->control[1] = -1;}
                return;
            }
            if ((events[ne].events & EPOLLIN) != 0) {
                int  fd = events[ne].data.fd;

                if (fd == control_fd)
                {
                    char  cmd = 255;
                    int   ret;
                    D(LOG_INFO,"gps control fd event");
                    do {
                        ret = read( fd, &cmd, 1 );
                    } while (ret < 0 && errno == EINTR);

                    if (cmd == CMD_QUIT) {
                        deal_cmd(gps_fd,CMD_QUIT);
                        
                        D(LOG_INFO,"gps thread quitting on demand");
                        return;
                    }
                    else if (cmd == CMD_START) {
                        deal_cmd(gps_fd,CMD_START);
                        
                        if (!started) {
                            D(LOG_INFO,"gps thread starting  location_cb=%p", state->callbacks.location_cb);
                            started = 1;
                            nmea_reader_set_callback( reader, state->callbacks.location_cb );
                        }
                    }
                    else if (cmd == CMD_STOP) {
                        deal_cmd(gps_fd,CMD_STOP);
                        
                        if (started) {
                            D(LOG_INFO,"gps thread stopping");
                            started = 0;
                            nmea_reader_set_callback( reader, NULL );
                        }
                    }
                }
                else if (fd == gps_fd)
                {
                    char  buff[NMEA_MAX_SIZE];
                    D(LOG_DEBUG,"gps fd event ");

                    //set callback
                    //nmea_reader_set_callback( reader, state->callbacks.location_cb );

                    for (;;) {
                        int  nn, ret;

                        ret = read( fd, buff, sizeof(buff) );
                        if (ret < 0) {
                            if (errno == EINTR)
                                continue;
                            if (errno != EWOULDBLOCK)
                                D(LOG_ERR,"error while reading from gps daemon socket: %s:", strerror(errno));
                            break;
                        }

                        if(ret > 0)
                        {
                            D(LOG_DEBUG,"received %d bytes: %.*s", ret, ret, buff);
                        }
                        
                        for (nn = 0; nn < ret; nn++)
                            nmea_reader_addc( reader, buff[nn] );
                    }
                    D(LOG_DEBUG,"gps fd event end");
                }
                else
                {
                    D(LOG_ERR,"epoll_wait() returned unkown fd %d ?", fd);
                }
            }
        }
    }
//}}}
}

static pthread_t ql_gps_nmea_thread;
static int ql_gps_nnme_fd[2];
static void * ql_gps_nmea_reader_thread(void *param) {
    while (1) {
        int gps_fd = -1;
     
       if (QL_GPS_CHANNEL[0] == '/' ) {
            gps_fd = open(QL_GPS_CHANNEL,O_RDWR);
            if (gps_fd > 0) {
                /* disable echo on serial ports */
                struct termios  ios;
                memset(&ios, 0, sizeof(ios));
                tcgetattr( gps_fd, &ios );
                cfmakeraw(&ios);
                ios.c_lflag = 0;  /* disable ECHO, ICANON, etc... */
                cfsetispeed(&ios, baud_bits[baud_rate_index(BAUD_RATE)]);
                cfsetospeed(&ios, baud_bits[baud_rate_index(BAUD_RATE)]);
                tcsetattr( gps_fd, TCSANOW, &ios );
            } else {
                D(LOG_ERR,"GPS channel <%s> not exist. errno: %d (%s)\n",QL_GPS_CHANNEL, errno, strerror(errno));
            }
        } else if (!strcmp(QL_GPS_CHANNEL, "rild-nmea")) {
            struct sockaddr_un addr;
            struct sockaddr_un *p_addr = &addr;
            const char *name = "rild-nmea";
                
            memset (p_addr, 0, sizeof (*p_addr));
            p_addr->sun_family = AF_LOCAL;
            p_addr->sun_path[0] = 0;
            memcpy(p_addr->sun_path + 1, name, strlen(name) );

            gps_fd = socket(AF_LOCAL, SOCK_STREAM, 0);
            if (gps_fd > 0) {
                if (connect(gps_fd, (struct sockaddr *) &addr, strlen(name) + offsetof(struct sockaddr_un, sun_path) + 1) < 0) {
                    D(LOG_ERR, "Error connecting rild-nmea (%s)\n", strerror(errno));
                    close(gps_fd);
                    gps_fd = -1;
                }
            }
            if (gps_fd > 0) {
                D(LOG_INFO, "connect rild-nmea socket = %d\n", gps_fd);
                write(gps_fd, "start rild-nema", strlen("start rild-nema"));
            }                
        }
        else {
            D(LOG_ERR,"Unknow and ERROR GPS channel <%s>\n",QL_GPS_CHANNEL);
        }
        
        if (gps_fd <= 0) {
            D(LOG_INFO, "fail to open GPS channel <%s>!\n", QL_GPS_CHANNEL);
            sleep(1);
            continue;
        }

        D(LOG_INFO, "open GPS channel <%s> successful!\n", QL_GPS_CHANNEL);

        if (gps_state == GPS_STATE_ENABLED && _gps_state[0].gps_power_state) {
            deal_cmd(-1, CMD_START); ////Fatal signal 11 (SIGSEGV) at 0x00000000 (code=1), thread 646 (LocationManager)
        }

        while(1) {
            unsigned char tmp[128];
            ssize_t nreads = read(gps_fd, tmp, sizeof(tmp));
            if (nreads > 0) {
                if (gps_state == GPS_STATE_ENABLED && _gps_state[0].gps_power_state)
                    write(ql_gps_nnme_fd[1], tmp, nreads);
            } else {
                D(LOG_INFO, "fail to read GPS channel <%s>! errno: %d (%s)\n", QL_GPS_CHANNEL, errno, strerror(errno));
                break;
            }
        }

        close(gps_fd);
        sleep(1);
    }

    return NULL;
}

static int
gps_state_init( GpsState*  state, GpsCallbacks* callbacks )
{
///{{{
    D(LOG_DEBUG,"%s[%d]--enter!",__FUNCTION__,__LINE__);
    state->init       = 1;
    state->control[0] = -1;
    state->control[1] = -1;
    state->fd         = -1;
    int flags = 0;
    int i = 0;
   
    D(LOG_INFO, "gps_state_init start");

    //open QL_GPS_CHANNEL
    if (!strncmp(QL_GPS_CHANNEL, I2C_DEV_PREFIX, strlen(I2C_DEV_PREFIX))) {
		i2c_host = QL_GPS_CHANNEL;
		socketpair( AF_LOCAL, SOCK_STREAM, 0, i2c_pipe);
        for (i = 0; i < 2; i++) {
            flags = fcntl(i2c_pipe[i], F_GETFL);
            fcntl(i2c_pipe[i], F_SETFL, flags | O_NONBLOCK);
        }
        state->fd = i2c_pipe[0];
        pthread_create(&gps_i2c_main_thread, NULL, gps_i2c_main_func, NULL);
    } else {
        if (ql_gps_nnme_fd[0] == 0) {
            socketpair( AF_LOCAL, SOCK_STREAM, 0, ql_gps_nnme_fd);
            pthread_create(&ql_gps_nmea_thread, NULL, ql_gps_nmea_reader_thread, NULL);     
        }
        state->fd = dup(ql_gps_nnme_fd[0]);
    }

    if (state->fd < 0) {
        D(LOG_ERR,"no gps module detected:[%d:%s]",errno,strerror(errno));
        gps_state = GPS_STATE_ERROR;
		call_gps_state_callback(gps_state);
        state->init = 0;
        return -1;
    }

    D(LOG_INFO,"Android gps will read from '%s' channel", QL_GPS_CHANNEL);

    if ( socketpair( AF_LOCAL, SOCK_STREAM, 0, state->control ) < 0 ) {
        D(LOG_ERR,"could not create thread control socket pair: %s", strerror(errno));
        goto Fail;
    }
    
    state->thread = callbacks->create_thread_cb( "gps_state_thread", gps_state_thread, state );

    if ( !state->thread ) {
        D(LOG_ERR,"could not create gps thread: %s", strerror(errno));
        goto Fail;
    }

    //deal_cmd(s->fd,CMD_START);                  

    D(LOG_INFO,"gps state initialized");
    return 0;

Fail:
    gps_state_done( state );
    return -1;
//}}}
}

static GpsCallbacks *callback_p = NULL;
static GpsXtraCallbacks* gpsXtraCallbacks = NULL;

static int ql_gps_init(GpsCallbacks* callbacks) {
    int ret;
    D(LOG_INFO,"GPS DRIVER VERSION: %s",DRIVER_VERSION);

    D(LOG_INFO,"%s(callbacks=%p)",__FUNCTION__, callbacks);

    //get_config();
    ret = check_module_type();
    if(ret != 0)
    {
        D(LOG_INFO,"NOT SUPPORT %s\n",MODULE_TYPE);
        return -1; 
    }
    D(LOG_INFO,"MODULE_TYPE:%s\nQL_GPS_CHANNEL:%s\nBAUD_RATE:%d\n",MODULE_TYPE,QL_GPS_CHANNEL,BAUD_RATE);

    callback_p = callbacks;
    GpsState*  s = _gps_state;
    s->callbacks = *callbacks;

    if (!s->init)
        gps_state_init(s, callbacks);

    if (s->fd < 0)
        return -1;
    
    
    gps_state = GPS_STATE_ENABLED;
    call_gps_state_callback(gps_state);

    return 0;
}

static void ql_gps_cleanup(void) {
    D(LOG_INFO,"%s()",__FUNCTION__);
    if(gps_state == GPS_STATE_ERROR)
    {
        return;
    }
    GpsState*  s = _gps_state;

    if (s->init)
        gps_state_done(s);
}


static int ql_gps_start(void) {
    D(LOG_INFO,"%s()",__FUNCTION__);
    GpsState*  s = _gps_state;
    D(LOG_INFO,"%s: gps_state is %d!", __FUNCTION__,gps_state);
    if(gps_state == GPS_STATE_ERROR ||
		gps_state == GPS_STATE_DISABLED)
    {
        int ret;
		
        D(LOG_INFO,"%s: Initialize GPS again!", __FUNCTION__);

        ret = ql_gps_init(callback_p);
        if(ret != 0)
        {
            return -1;
        }
    }

    if (!s->init) {
        D(LOG_ERR,"%s[%d]: called with uninitialized state !!", __FUNCTION__,__LINE__);
        return -1;
    }

    D(LOG_INFO,"%s: called", __FUNCTION__);
    gps_state_start(s);

#if 0
    D(LOG_INFO,"%s: gpsXtraCallbacks=%p", __FUNCTION__, gpsXtraCallbacks);
    if (strncmp(MODULE_TYPE,"UC20",4) == 0 || strncmp(MODULE_TYPE,"EC20",4) == 0
            || strncmp(MODULE_TYPE,"EC21",4) == 0 || strncmp(MODULE_TYPE,"EC25",4) == 0)
    {
        if (gpsXtraCallbacks != NULL)
            gpsXtraCallbacks->download_request_cb();
    }
#endif
    if (!strncmp(QL_GPS_CHANNEL, I2C_DEV_PREFIX, strlen(I2C_DEV_PREFIX)))
        start_i2c_read();
    
    return 0;
}

static int ql_gps_stop(void) {
    D(LOG_INFO,"%s()",__FUNCTION__);
    if(gps_state == GPS_STATE_ERROR)
    {
        return -1;
    }
    GpsState*  s = _gps_state;

    if (!s->init) {
        D(LOG_INFO,"%s: called with uninitialized state !!", __FUNCTION__);
        return -1;
    }

    D(LOG_INFO,"%s: called", __FUNCTION__);
    gps_state_stop(s);
    if (!strncmp(QL_GPS_CHANNEL, I2C_DEV_PREFIX, strlen(I2C_DEV_PREFIX)))
        stop_i2c_read();
    return 0;
}

static int ql_gps_inject_time(GpsUtcTime time, int64_t timeReference, int uncertainty) {
    D(LOG_INFO,"%s(time=%lld, timeReference=%lld, uncertainty=%d)",__FUNCTION__,
        *((int64_t *)&time), timeReference, uncertainty);
    if (strncmp(MODULE_TYPE,"UC20",4) == 0 || strncmp(MODULE_TYPE,"EC20",4) == 0
            || strncmp(MODULE_TYPE,"EC21",4) == 0 || strncmp(MODULE_TYPE,"EC25",4) == 0) {
        GPS_TLV *tlv = ( GPS_TLV *)malloc(sizeof(GPS_TLV) + sizeof(time) + sizeof(timeReference) + sizeof(uncertainty));
        pthread_t deal_cmd_thread;
        pthread_attr_t attr;
        pthread_attr_init (&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        tlv->type = 23;
        tlv->length = sizeof(time) + sizeof(timeReference) + sizeof(uncertainty);
        memcpy(tlv->data, &time, sizeof(time));
        memcpy(tlv->data + sizeof(time), &timeReference, sizeof(timeReference));
        memcpy(tlv->data + sizeof(time) + sizeof(uncertainty), &uncertainty, sizeof(uncertainty));
        pthread_create (&deal_cmd_thread, &attr, &send_command_to_module_thread, (void *)tlv);
    }
    return 0;
}

static int ql_gps_inject_location(double latitude, double longitude, float accuracy) {
    D(LOG_INFO,"%s(latitude=%f, longitude=%f, accuracy=%f)",__FUNCTION__,
        latitude, longitude, accuracy);
    return 0;
}

static void ql_gps_delete_aiding_data(GpsAidingData flags) {
    D(LOG_INFO,"%s(flags=%d)",__FUNCTION__, flags);
}

static int ql_gps_set_position_mode(GpsPositionMode mode, GpsPositionRecurrence recurrence,
            uint32_t min_interval, uint32_t preferred_accuracy, uint32_t preferred_time)
{
    D(LOG_INFO,"%s(mode=%d, recurrence=%d, min_interval=%d, preferred_accuracy=%d, preferred_time=%d)",__FUNCTION__,
        mode, recurrence, min_interval, preferred_accuracy, preferred_time);
    return 0;
}

static int ql_gps_xtra_init( GpsXtraCallbacks* callbacks ) {
    D(LOG_INFO,"%s(callbacks=%p)",__FUNCTION__, callbacks);
    gpsXtraCallbacks = callbacks;
    return 0;
}

static int ql_gps_xtra_inject_xtra_data( char* data, int length ) {
    D(LOG_INFO,"%s(data=%p, length=%d)",__FUNCTION__, data, length);
    if (strncmp(MODULE_TYPE,"UC20",4) == 0 || strncmp(MODULE_TYPE,"EC20",4) == 0
            || strncmp(MODULE_TYPE,"EC21",4) == 0 || strncmp(MODULE_TYPE,"EC25",4) == 0) {
        GPS_TLV *tlv = ( GPS_TLV *)malloc(sizeof(GPS_TLV) + length);
        pthread_t deal_cmd_thread;
        pthread_attr_t attr;
        pthread_attr_init (&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        tlv->type = 34;
        tlv->length = length;
        memcpy(tlv->data, data, length);
        pthread_create (&deal_cmd_thread, &attr, &send_command_to_module_thread, (void *)tlv);
    }
    return 0;
}

static const GpsXtraInterface qlGpsXtraInterface = {
    sizeof(GpsXtraInterface),
    ql_gps_xtra_init,
    ql_gps_xtra_inject_xtra_data
};

static const void* ql_gps_get_extension(const char* name) {
    D(LOG_INFO,"%s(name=%s)",__FUNCTION__, name);
    if (!strcmp(name, GPS_XTRA_INTERFACE))
        return &qlGpsXtraInterface;
    return NULL;
}

static const GpsInterface  qlGpsInterface = {
    sizeof(GpsInterface),
    //when system enable gps , it will be called
    /* 
     * Opens the interface and provides the callback routines 
     * to the implemenation of this interface. 
     */
    ql_gps_init,
    //when app use gps , this will be called second
    /*
     * Starts navigating.
     */  
    ql_gps_start,
    //when app exit , and does not use  gps , this will be called first
    /*
     * Stop navigating.
     */ 
    ql_gps_stop,
    //when system disable gps , it will be called
    /*
     * Close the interface.
     */ 
    ql_gps_cleanup,
    //
    /*
     * Injects the current time.
     */  
    ql_gps_inject_time,
    //
    /*
     * Injects current location from another location provider
     * (typically cell ID). 
     * latitude and longitude are measured in degrees 
     * expected accuracy is measured in meters 
     */  
    ql_gps_inject_location,
    //for Performance test
    /* 
     * Specifies that the next call to start will not use the
     * information defined in the flags. GPS_DELETE_ALL is passed for 
     * a cold start.
     */
    ql_gps_delete_aiding_data,
    //when app use gps , this will be called first
    /* 
     * min_interval represents the time between fixes in milliseconds. 
     * preferred_accuracy represents the requested fix accuracy in meters. 
     * preferred_time represents the requested time to first fix in milliseconds. 
     */ 
    ql_gps_set_position_mode,
    //
    /*
     * Get a pointer to extension information.
     */  
    ql_gps_get_extension,
};

const GpsInterface* gps__get_gps_interface(struct gps_device_t* dev)
{
///{{{
    D(LOG_DEBUG,"%s[%d]--enter!",__FUNCTION__,__LINE__);
    return &qlGpsInterface;
//}}}
}

static int open_gps(const struct hw_module_t* module, char const* name,
        struct hw_device_t** device)
{
///{{{
    D(LOG_DEBUG,"%s[%d]--enter!",__FUNCTION__,__LINE__);
    struct gps_device_t *dev = malloc(sizeof(struct gps_device_t));
    memset(dev, 0, sizeof(*dev));

    dev->common.tag = HARDWARE_DEVICE_TAG;
    dev->common.version = 0;
    dev->common.module = (struct hw_module_t*)module;
    //dev->common.close = (int (*)(struct hw_device_t*))close_lights;
    dev->get_gps_interface = gps__get_gps_interface;

    *device = (struct hw_device_t*)dev;
    return 0;
//}}}
}


static struct hw_module_methods_t gps_module_methods = {
    .open = open_gps
};

struct hw_module_t HAL_MODULE_INFO_SYM = {
    .tag = HARDWARE_MODULE_TAG,
    .version_major = 1,
    .version_minor = 0,
    .id = GPS_HARDWARE_MODULE_ID,
    .name = "QUECTEL GPS Module",
    .author = "Joe.Wang",
    .methods = &gps_module_methods,
};

#ifdef MAIN_TEST
static void location_cb(GpsLocation* location) {
    D(LOG_INFO, "%s", __func__);
}

static void status_cb(GpsStatus* status) {
    //D(LOG_INFO, "%s", __func__);
}

pthread_t create_thread(const char* name, void (*start)(void *), void* arg) {
    pthread_t pthread_id;
    D(LOG_INFO, "%s", __func__);
    pthread_create(&pthread_id, NULL, start, arg);
    return pthread_id;
}

static void nmea_cb(GpsUtcTime timestamp, const char* nmea, int length) {
    //D(LOG_INFO, "%s", __func__);
}


static void sv_status_cb(GpsSvStatus* sv_info) {
    int i;
    D(LOG_INFO, "%s", __func__);
    D(LOG_INFO, "num_svs\t%d", sv_info->num_svs);
    //D(LOG_INFO, "ephemeris_mask\t%08x", sv_info->ephemeris_mask);
    //D(LOG_INFO, "almanac_mask\t%08x", sv_info->almanac_mask);
    D(LOG_INFO, "used_in_fix_mask\t%08x", sv_info->used_in_fix_mask);
    for (i = 0; i < sv_info->num_svs; i++) {
        //D(LOG_INFO, "sv_list[%d]: prn %d,  snr %f, elevation %f azimuth %f ", i,
            //sv_info->sv_list[i].prn, sv_info->sv_list[i].snr, sv_info->sv_list[i].elevation, sv_info->sv_list[i].azimuth);
    }
}

static GpsCallbacks callbacks;
int main(int argc, char *argv[]) {
    callbacks.size = sizeof(callbacks);
    callbacks.location_cb = location_cb;
    callbacks.status_cb = status_cb;
    callbacks.sv_status_cb = sv_status_cb;
    callbacks.nmea_cb = nmea_cb;
    callbacks.create_thread_cb = create_thread;
    ql_gps_init(&callbacks);
    ql_gps_start();
    getchar();getchar();
    ql_gps_stop();

    return 0;
}
#endif
