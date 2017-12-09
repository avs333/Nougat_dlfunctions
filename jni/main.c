#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <jni.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <limits.h>
#include <pthread.h>

#include <android_native_app_glue.h>
#include <sys/system_properties.h>

#include "main.h"

static void *app_thread(void *arg);

static void stop_app_thread(struct ctx *ctx) 
{
    ctx->should_run = 0;
    pthread_join(ctx->app_thread, 0);
    ctx->app_thread = 0;	
}

static void done(struct ctx *ctx) 
{
    ANativeActivity* activity;

    if(!ctx) return;
    activity = ctx->app->activity;
    pthread_mutex_lock(&ctx->mutex);
    if(ctx->app_thread) log_error("internal error: thread not stopped in %s", __func__);
    ft_quit(ctx);
    pthread_mutex_unlock(&ctx->mutex);	
    pthread_mutex_destroy(&ctx->mutex);
    if(ctx->buffer) free(ctx->buffer);
    free(ctx);
    ANativeActivity_finish(activity);
}

static void draw_frame(struct ctx* ctx) 
{
    int k, width, stride;
    void *src, *dst; 
    ANativeWindow_Buffer buffer;

    if(!ctx->app->window) {
	log_error("%s called with null window", __func__);
	return;
    }
    log_info("%s: %d x %d", __func__, ctx->width, ctx->height);
    pthread_mutex_lock(&ctx->mutex); 
    if(ANativeWindow_setBuffersGeometry(ctx->app->window, 0, 0, ctx->fmt) != 0) {
	log_error("Failed to set buffer format");
        pthread_mutex_unlock(&ctx->mutex); 
	return;
    }
    if(ANativeWindow_lock(ctx->app->window, &buffer, 0) < 0) {
	log_error("Unable to lock window buffer");
        pthread_mutex_unlock(&ctx->mutex); 
	return;
    }
    src = ctx->buffer;
    dst = buffer.bits;

    if(ctx->fmt == WINDOW_FORMAT_RGBA_8888) {
	width = ctx->width * 4;
	stride = ctx->stride * 4;	
    } else if(ctx->fmt == WINDOW_FORMAT_RGB_565) {
	width = ctx->width * 2;
	stride = ctx->stride * 2;	
    } else {
	log_error("%s called for unsupported window format %d", __func__, ctx->fmt);
	ANativeWindow_unlockAndPost(ctx->app->window);	
	pthread_mutex_unlock(&ctx->mutex); 
	return;
    }

    for(k = 0; k < ctx->height; k++, src += stride, dst += stride) memcpy(dst, src, width);
   		
    ANativeWindow_unlockAndPost(ctx->app->window);	
    pthread_mutex_unlock(&ctx->mutex); 
}

static void init_window(struct ctx *ctx)
{
    ANativeWindow_Buffer buffer;

    if(ctx->buffer) return;

    log_info(__func__);
    if(ANativeWindow_setBuffersGeometry(ctx->app->window, 0, 0, ctx->fmt) != 0) {
	log_error("Failed to set buffer format");
	done(ctx);
	return;
    }
    if(ANativeWindow_lock(ctx->app->window, &buffer, 0) < 0) {
	log_error("Unable to lock window buffer");
	done(ctx);
	return;
    }
    ctx->height = buffer.height;
    ctx->width = buffer.width;
    ctx->stride = buffer.stride;
    ctx->buffer = calloc(1, ctx->height * ctx->stride * (ctx->fmt == WINDOW_FORMAT_RGBA_8888 ? 4 : 2)); 
    ANativeWindow_unlockAndPost(ctx->app->window);	
    if(!ctx->buffer) {
	log_error("No memory for window buffer");
	done(ctx);
    }		
    log_info("window %d x %d stride %d allocated", ctx->width, ctx->height, ctx->stride);
    ctx->should_run = 1;	
    if(pthread_create(&ctx->app_thread, 0, app_thread, ctx) != 0) {
	log_error("failed to create app thread");
	done(ctx);
    }
}

static int32_t handle_input(struct android_app* app, AInputEvent* event) 
{
    struct ctx* ctx = (struct ctx*) app->userData;
    if(AInputEvent_getType(event) == AINPUT_EVENT_TYPE_MOTION) {
	int x = AMotionEvent_getX(event, 0);
	int y = AMotionEvent_getY(event, 0);
	log_info("AINPUT_EVENT_TYPE_MOTION %d %d", x, y);	
	pthread_mutex_lock(&ctx->mutex);
	if(ft_render_string(ctx, "BRAVO, Mr. T.W.Lewis\nЭто по-русски\n", x, y, 1000) != 0) 
		log_error("rendering failed");
	pthread_mutex_unlock(&ctx->mutex);
	draw_frame(ctx);	
	stop_app_thread(ctx);
	return 1;
    }
    return 0;
}

static void handle_cmd(struct android_app* app, int32_t cmd)
{
    struct ctx* ctx = (struct ctx*) app->userData;

    switch(cmd) {
	case APP_CMD_INIT_WINDOW:
	    log_info("APP_CMD_INIT_WINDOW");	
	    pthread_mutex_lock(&ctx->mutex);
	    init_window(ctx);
	    pthread_mutex_unlock(&ctx->mutex);
	    draw_frame(ctx);
	    break;
	case APP_CMD_WINDOW_REDRAW_NEEDED:
	case APP_CMD_WINDOW_RESIZED:
	    draw_frame(ctx);
	    log_info("APP_CMD_WINDOW_RESIZED/REDRAW_NEEDED");	
	    break;
	case APP_CMD_TERM_WINDOW:
	    log_info("APP_CMD_TERM_WINDOW");	
	    break;
	default:
	    log_info("APP_CMD_%d", cmd);
	    break;
    }
}

/* Terrible bug corrected: https://groups.google.com/d/msg/alt.usage.english/4t0PAe9-QGc/H2Jb2SJQBgAJ */

static const char *low_nums[] = { 0, "one", "two", "three", "four", "five", "six", "seven", "eight", "nine", "ten",
	"eleven", "twelve", "thirteen", "fourteen", "fifteen", "sixteen", "seventeen", "eighteen", "nineteen" };
static const char *ten_multiples[] = { 0, 0, "twenty", "thirty", "forty", "fifty", "sixty", "seventy", "eighty", "ninety" };
static const char *large_nums[] = { "billion", "million", "thousand", 0 };
static const int large_vals[] = { 1000000000, 1000000, 1000, 0 };

/* Numerals in the English language are spoken in triples each consisting 
   of the number of hundreds (if any) possibly followed by "and" and the lesser number, e.g.:
   909,168,442 is "nine hundred and nine million one hundered and sixty-eight thousand four hundred and forty-two" */

static char *triple(char *out, int num)
{
    int hi, rest;
    char *c = out;	

	if(num <= 0 || num >= 1000) return 0;	/* can't happen */
	hi = num/100;
	rest = num % 100;
	if(hi) {
	    sprintf(c, "%s hundred%s", low_nums[hi], rest ? " and " : "");
	    c += strlen(c);
	}
	if(!rest) return out;
	if(rest <= 19) {
	    sprintf(c, "%s", low_nums[rest]);
	    return out;
	}
	hi = rest/10;
	rest = rest % 10;
	if(rest) sprintf(c, "%s-%s", ten_multiples[hi], low_nums[rest]);
	else sprintf(c, "%s", ten_multiples[hi]);

    return out;
}

static char *num2engl(unsigned int num)
{
    uint32_t i, hi, rest;
    char tmp[512], *c;

	if(!num) return strdup("zero");

	for(i = 0, rest = num, c = tmp; large_vals[i]; i++) {
	    hi = rest/large_vals[i];
	    rest = rest % large_vals[i];
	    if(hi) {
		sprintf(c, "%s %s%s", triple(c, hi), large_nums[i], rest ? " " : "");
		c += strlen(c);
	    }
	    if(!rest) return strdup(tmp);
	}
	triple(c, rest);

    return strdup(tmp);
}

static void *app_thread(void *arg) 
{
    struct ctx *ctx = (struct ctx *) arg;
    int  k = 0, stride_bytes, bytes_per_font_height, pixels_per_font_height, 
	 lines, cur_lines, max_lines, target_width, start_x, start_y;   

	pixels_per_font_height = ft_get_line_height(ctx) + 10;
	if(!pixels_per_font_height || !ctx->height) {
	    log_error("dimensions unknown");
	    return 0;	    
	}
	start_x = 10;
	start_y = 250;
	cur_lines = 0;
	max_lines = (ctx->height - 2 * start_y)/pixels_per_font_height;
	target_width = ctx->width - 2 * start_x;

	log_info("%s: width = %d height %d max_lines %d", __func__, target_width, ctx->height, max_lines);

	if(ctx->fmt == WINDOW_FORMAT_RGBA_8888) stride_bytes = ctx->stride * 4;	
	else stride_bytes = ctx->stride * 2;	
	
	bytes_per_font_height = stride_bytes * pixels_per_font_height;

	while(1) {

	    char *c = num2engl(k++);

	    pthread_mutex_lock(&ctx->mutex);
	    if(!ctx->should_run) {
            	pthread_mutex_unlock(&ctx->mutex);	
		break;
	    }	 
	    if(ft_get_string_metrics(ctx, c, target_width, &lines) != 0) {
		log_error("ft_get_string_metrics failed");
            	pthread_mutex_unlock(&ctx->mutex);	
		break;	
	    }	
	    if(lines + cur_lines >= max_lines) {	/* scroll up window buffer by "lines" */
		void *ptr = ctx->buffer + start_y * stride_bytes;
		log_info("scrolling: %d + %d > %d", lines, cur_lines, max_lines);
		if(lines > max_lines) {
		    log_error("logcat string won't fit on this screen: lines %d max_lines %d", lines, max_lines);
		    pthread_mutex_unlock(&ctx->mutex);
		    continue;		
		}
		cur_lines -= lines;
    		memmove(ptr, ptr + lines * bytes_per_font_height, cur_lines * bytes_per_font_height);
		memset(ptr + cur_lines * bytes_per_font_height, 0, (max_lines - cur_lines) * bytes_per_font_height);
	    } 

	    if(ft_render_string(ctx, c, start_x, start_y + cur_lines * pixels_per_font_height, target_width) != 0) {
		pthread_mutex_unlock(&ctx->mutex);
		break;
	    }	
	    cur_lines += lines;
            pthread_mutex_unlock(&ctx->mutex);	

	    draw_frame(ctx);
	    free(c);	
	    sleep(1);
	}

    return 0;		
}

#ifdef TESTCPP
extern int test_cplusplus();
#endif


void android_main(struct android_app* app) 
{
    struct ctx *ctx = 0;
    int ident, events;
    struct android_poll_source* source;
    char prop_density[PROP_VALUE_MAX];

    app_dummy();

#ifdef __arm__
    log_info("starting arm code");
#elif defined(__aarch64__)
    log_info("starting aarch64 code");
#else
#error "Arch unknown, please port me" 
#endif

#ifdef TESTCPP
    test_cplusplus();
#endif

    ctx = (struct ctx *) calloc(1, sizeof(struct ctx));
    if(!ctx) {
	log_error("no memory");
    	ANativeActivity_finish(app->activity);
	return;
    }	

    app->onAppCmd = handle_cmd;
    app->onInputEvent = handle_input;
    app->userData = ctx;

    pthread_mutex_init(&ctx->mutex, 0);
    ctx->app = app;

    if(__system_property_get("ro.sf.lcd_density", prop_density) < 0 
	|| sscanf(prop_density, "%d", &ctx->dpi) != 1) {
	log_info("failed to find screen density, assuming 480");
	ctx->dpi = 480;
    }

    ctx->fmt = WINDOW_FORMAT_RGB_565; // WINDOW_FORMAT_RGBA_8888;

    if(ft_init(ctx) != 0) {
	done(ctx);
	return;
    }

    while(1) {	
	while((ident = ALooper_pollAll(-1, NULL, &events, (void**)&source)) >= 0) {
	    if(source) source->process(app, source);
	    if(app->destroyRequested != 0) {
		stop_app_thread(ctx);
		log_info("destroyed.");	
		done(ctx);	
		return;
	    }
	}
    }
}


