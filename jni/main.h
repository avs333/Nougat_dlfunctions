#ifndef __MAIN_H_INCLUDED
#define __MAIN_H_INCLUDED

#include <android/log.h>

#define APP_TAG	"test2"
#define log_error(...) ((void)__android_log_print(ANDROID_LOG_ERROR, APP_TAG, __VA_ARGS__))
#define log_info(...) ((void)__android_log_print(ANDROID_LOG_INFO, APP_TAG, __VA_ARGS__))
#if 1
#define log_debug(...)
#else
#define log_debug log_info
#endif


#define DEFAULT_FACE	"/system/fonts/Roboto-Regular.ttf" 
#define DEFAULT_FSIZE	8

struct android_app;
struct ft_ctx;

struct ctx {
    struct  android_app* app;
    int	dpi;				/* screen pixel density */
    int fmt, height, width, stride;	/* window params */
    void *buffer;			/* window buffer */	    
    struct  ft_ctx *fctx;
    pthread_mutex_t mutex;
    pthread_t app_thread;
    int should_run;
};

extern int ft_init(struct ctx *ctx);
extern void ft_quit(struct ctx *ctx);

/* "file" is full path to ttf font file, "size" is its size in points */
extern int ft_set_face(struct ctx *ctx, const char *file, int size);

extern int ft_get_line_height(struct ctx *c);
extern int ft_get_string_metrics(struct ctx *c, const char *str, int target_width, int *target_lines);
extern int ft_render_string(struct ctx *ctx, const char *str, int start_x, int start_y, int width);


extern void *fake_dlopen(const char *filename, int flags);
extern int fake_dlclose(void *handle);
extern void *fake_dlsym(void *handle, const char *symbol);


#endif

