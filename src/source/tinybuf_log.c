#include "tinybuf_log.h"
#include <stdarg.h>
#ifdef _WIN32
#include <windows.h>
static int gettimeofday(struct timeval* tv, void* tz) {
    FILETIME ft;
    ULARGE_INTEGER uli;
    GetSystemTimeAsFileTime(&ft);
    uli.LowPart = ft.dwLowDateTime;
    uli.HighPart = ft.dwHighDateTime;
    unsigned long long t = (uli.QuadPart - 116444736000000000ULL) / 10ULL;
    tv->tv_sec = (long)(t / 1000000ULL);
    tv->tv_usec = (long)(t % 1000000ULL);
    return 0;
}
#else
#include <unistd.h>
#include <sys/time.h>
#endif
#include <time.h>

static e_log_lev s_log_leg = log_trace;

void set_log_level(e_log_lev lev){
    s_log_leg = lev;
}
e_log_lev get_log_level(){
    return s_log_leg;
}

const char *LOG_CONST_TABLE[][3] = {
        {"\033[44;37m", "\033[34m" , "T"},
        {"\033[42;37m", "\033[32m" , "D"},
        {"\033[46;37m", "\033[36m" , "I"},
        {"\033[43;37m", "\033[33m" , "W"},
        {"\033[41;37m", "\033[31m" , "E"}};

#ifdef ANDROID
android_LogPriority LogPriorityArr[] = {ANDROID_LOG_VERBOSE,ANDROID_LOG_DEBUG,ANDROID_LOG_INFO,ANDROID_LOG_WARN,ANDROID_LOG_ERROR};
#endif

static void print_time(const struct timeval *tv,char *buf,int buf_size) {
    time_t sec_tmp = tv->tv_sec;
    struct tm *tm = localtime(&sec_tmp);
    snprintf(buf,
             buf_size,
             "%d-%02d-%02d %02d:%02d:%02d.%03d",
             1900 + tm->tm_year,
             1 + tm->tm_mon,
             tm->tm_mday,
             tm->tm_hour,
             tm->tm_min,
             tm->tm_sec,
             (int) (tv->tv_usec / 1000));
}

void get_now_time_str(char *buf,int buf_size){
    struct timeval tv;
    gettimeofday(&tv, NULL);
    print_time(&tv,buf,buf_size);
}

static printf_ptr s_printf = NULL;
void set_printf_ptr(printf_ptr cb){
    s_printf = cb;
}
printf_ptr get_printf_ptr(){
    return s_printf ? s_printf : printf;
}

void log_print(e_log_lev lev, const char *file, int line, const char *func, const char *fmt, ...){
    if(lev < get_log_level()){
        return;
    }
    char time_str[64];
    get_now_time_str(time_str,sizeof(time_str));
    printf_ptr p = get_printf_ptr();
    p("%s %s | %s ", time_str, LOG_CONST_TABLE[lev][2], func);
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    p("\r\n");
}
