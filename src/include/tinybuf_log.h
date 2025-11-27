#ifndef TINYBUF_LOG_H
#define TINYBUF_LOG_H

#include <stdio.h>
#ifdef ANDROID
#include <android/log.h>
#ifdef ANDROID
extern android_LogPriority LogPriorityArr[];
#endif
#endif //ANDROID

#define CLEAR_COLOR "\033[0m"

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

/**
 * 日志等级
 */
typedef enum {
    log_trace = 0,
    log_debug ,
    log_info ,
    log_warn ,
    log_error ,
} e_log_lev;

extern const char *LOG_CONST_TABLE[][3];


/**
 * 获取当前时间字符串
 * @param buf 存放地址
 * @param buf_size 存放地址长度
 */
void get_now_time_str(char *buf,int buf_size);

/**
 * 设置日志等级
 * @param lev 日志等级
 */
void set_log_level(e_log_lev lev);

/**
 * 获取日志等级
 * @return 日志等级
 */
e_log_lev get_log_level(void);

/**
 * printf回调函数
 */
typedef int	(*printf_ptr)(const char * fmt, ...) ;

/**
 * 设置printf回调函数
 * @param cb
 */
void set_printf_ptr(printf_ptr cb);

/**
 * 获取printf函数
 * @return
 */
printf_ptr get_printf_ptr(void);


void log_print(e_log_lev lev, const char *file, int line, const char *func, const char *fmt, ...);
#define LOGT(...) log_print(log_trace,__FILE__,__LINE__,__FUNCTION__,__VA_ARGS__)
#define LOGD(...) log_print(log_debug,__FILE__,__LINE__,__FUNCTION__,__VA_ARGS__)
#define LOGI(...) log_print(log_info,__FILE__,__LINE__,__FUNCTION__,__VA_ARGS__)
#define LOGW(...) log_print(log_warn,__FILE__,__LINE__,__FUNCTION__,__VA_ARGS__)
#define LOGE(...) log_print(log_error,__FILE__,__LINE__,__FUNCTION__,__VA_ARGS__)

#ifdef __cplusplus
} // extern "C"
#endif // __cplusplus
#endif //TINYBUF_LOG_H
