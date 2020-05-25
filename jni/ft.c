#include <jni.h>
#include <errno.h>
#include <dlfcn.h>
#include <ctype.h>
#include <sys/stat.h>
#include <pthread.h>
#ifdef HANDLE_UNICODE
#include <wchar.h>
#endif
#include <android_native_app_glue.h>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <elf.h>

#include <ft2build.h>
#include FT_FREETYPE_H

#include "main.h"

struct bitmap {
#ifdef HANDLE_UNICODE
   FT_ULong c;
#else
   char c;
#endif
   int width, rows, pitch;
   int left, top, advance;
   uint8_t *buffer;	
   struct bitmap *next;
};

struct ft_ctx {
    void *ftlib;		/* handle to libft2.so */
    FT_Library  library;	/* ft2 initialised */
    FT_Face face;		/* current face for output */
    int	 fsize;			/* its point size */	
    int  height;		/* baseline-to-baseline distance in pixels */	
    int  maxwd;			/* width of widest character in face */
    struct bitmap *bmp_cache;
#define ADD_FUNC(A) typeof(&A) A
#include "ft_functions.inc"
};

int ft_init(struct ctx *c) 
{
    struct ft_ctx *ctx;

    if(c->fctx) {	
	log_error("initialised already");
	return -1;
    }	
    ctx = calloc(1, sizeof(struct ft_ctx));
    if(!ctx) {
	log_error("no memory");
	return -1;
    }	
    c->fctx = ctx;

#ifdef __arm__
    ctx->ftlib = fake_dlopen("/system/lib/libft2.so", RTLD_NOW);
    /* NB: /system/vendor/lib and /vendor/lib to be checked for libs like libssl */	    	
#else
    ctx->ftlib = fake_dlopen("/system/lib64/libft2.so", RTLD_NOW);
    /* NB: /system/vendor/lib64 and /vendor/lib64 to be checked for libs like libssl */
    if(!ctx->ftlib) ctx->ftlib = fake_dlopen("/apex/com.android.conscrypt/lib64/libft2.so", RTLD_NOW);		    	
#endif
    log_info("fake_dlopen for libft2.so returned %p", ctx->ftlib);

    if(!ctx->ftlib) return -1;

#define ADD_FUNC(A) do { \
   ctx->A = (typeof(&A)) fake_dlsym(ctx->ftlib, #A); 			\
   if(!ctx->A) { 							\
	log_error("no " #A " in libft2.so");				\
	return -1; 							\
   }									\
} while(0)
#include "ft_functions.inc"

    if(ctx->FT_Init_FreeType(&ctx->library) != 0) {
	log_error("failed to init libft2.so");
	return -1;	
    }
    if(ft_set_face(c, DEFAULT_FACE, DEFAULT_FSIZE) != 0) {
	log_error("failed to set default face " DEFAULT_FACE);
	return -1;
    }	
    log_info("ft_init: success");
    return 0;
}

void ft_quit(struct ctx *c)
{
    struct ft_ctx *ctx = c->fctx;
    if(!ctx) return;
    if(ctx->bmp_cache) {
	struct bitmap *bmp, *next_bmp;
	for(bmp = ctx->bmp_cache; bmp; bmp = next_bmp) {
	    next_bmp = bmp->next;
	    free(bmp->buffer);
	    free(bmp);
	}
    }
    if(ctx->FT_Done_Face && ctx->face) ctx->FT_Done_Face(ctx->face);	
    if(ctx->FT_Done_FreeType && ctx->library) ctx->FT_Done_FreeType(ctx->library);
    if(ctx->ftlib) fake_dlclose(ctx->ftlib);
    c->fctx = 0;	
    free(ctx);
}

/* convert glyph dimension to screen pixels */
#define glyph2screen(A) ((A) * ctx->fsize * c->dpi)/(ctx->face->units_per_EM * 72)

int ft_set_face(struct ctx *c, const char *new_face, int new_size)
{
    struct  ft_ctx *ctx = c->fctx;
    if(ctx->face) ctx->FT_Done_Face(ctx->face);
    if(ctx->FT_New_Face(ctx->library, new_face, 0, &ctx->face) != 0) {
	log_error("failed to open face %s", new_face);
	return -1;
    }	
    ctx->fsize = new_size;
    if(ctx->FT_Set_Char_Size(ctx->face, ctx->fsize * 64, 0, c->dpi, c->dpi) != 0) {
	log_error("failed to set face size %d for %s", ctx->fsize, new_face);
	return -1;
    }
#ifdef HANDLE_UNICODE
    ctx->FT_Select_Charmap(ctx->face, FT_ENCODING_UNICODE);	
#endif
    log_debug("%08lX/%08lX u_p_EM %d, bbox %ld-%ld x %ld-%ld, asc=%d desc=%d, ht=%d",
	ctx->face->face_flags,
	ctx->face->style_flags,
	ctx->face->units_per_EM,
	ctx->face->bbox.xMin, ctx->face->bbox.xMax, 
	ctx->face->bbox.yMin, ctx->face->bbox.yMax, 
	ctx->face->ascender, ctx->face->descender, ctx->face->height);	

    ctx->height = glyph2screen(ctx->face->height);
    ctx->maxwd = glyph2screen(ctx->face->max_advance_width);

    return 0;
}

/* Baseline-to-baseline distance in pixels for a given font and given screen dpi */
int ft_get_line_height(struct ctx *c)
{ 
    return c && c->fctx ? c->fctx->height : 0;	
}

/* Return the bitmap cached for "c". If not in cache, first render and cache it. */

static struct bitmap *get_char_bitmap(struct ft_ctx *ctx, FT_ULong c)
{
    FT_GlyphSlot slot = ctx->face->glyph;
    FT_Bitmap *bitmap = &slot->bitmap;
    struct bitmap *bmp = ctx->bmp_cache, *last = 0;	

	for(bmp = ctx->bmp_cache; bmp; bmp = bmp->next) {
	    if(bmp->c == c) return bmp; /* cache hit */
	    last = bmp;
	}
	if(ctx->FT_Load_Char(ctx->face, c, FT_LOAD_RENDER) != 0) {
	    log_error("error rendering bitmap for char %ld", c);	
	    return 0;
	}
	bmp = (struct bitmap *) calloc(1, sizeof(struct bitmap));
	if(!bmp) {
	    log_error("no memory for bitmap");	
	    return 0;
	}
	bmp->buffer = (uint8_t *) malloc(bitmap->pitch * bitmap->rows);
	if(!bmp->buffer) {
	    if(bitmap->pitch < 0) log_error("fonts with negative pitch not supported");
	    else log_error("no memory for bitmap buffer");	
	    free(bmp);
	    return 0;
	}
	memcpy(bmp->buffer, bitmap->buffer, bitmap->pitch * bitmap->rows);
	bmp->c = c;
	bmp->width = bitmap->width;
	bmp->rows = bitmap->rows;
	bmp->pitch = bitmap->pitch;
	bmp->left = slot->bitmap_left;
	bmp->top = slot->bitmap_top;
	bmp->advance = (slot->advance.x >> 6);

	if(last) last->next = bmp;
	else ctx->bmp_cache = bmp;

    return bmp;
}

/* Max string width in pixels for word wrapping calculations (not implemented in this example) */
int ft_max_string_width(struct ctx *c, const char *str)
{
    struct ft_ctx *ctx = c->fctx;
#ifdef HANDLE_UNICODE
    wchar_t s[strlen(str)+1];
	if(mbstowcs(s, str, strlen(str)+1) <= 0) {
	    log_error("widechar conversion error");
	    return -1;
	}
    return glyph2screen(ctx->maxwd * wcslen(s));
#else
    return glyph2screen(ctx->maxwd * strlen(str));
#endif
}


/* Updates target_lines, returns error if the string does not fit in target_width. 
   If target_lines is null, no line breaks are allowed. */

int ft_get_string_metrics(struct ctx *c, const char *str, int target_width, int *target_lines)
{
    struct  ft_ctx *ctx = c->fctx;
    int wd = 0, lines = 1;  	
    struct bitmap *bmp; 
#ifdef HANDLE_UNICODE	
    wchar_t ss[strlen(str)+1], *s = ss;
	if(mbstowcs(s, str, strlen(str)+1) <= 0) {
	    log_error("widechar conversion error");
	    return -1;
	}
#else
    const char *s = str;	
#endif
	while(*s) {
	    if(*s == '\n') {	
		if(!target_lines) return -1;
		lines++;		
		wd = 0;
		s++;
		continue;	
	    }
	    bmp = get_char_bitmap(ctx, *s);
	    if(!bmp) return -1;
	    if(wd + bmp->advance > target_width) {	/* line break required? */
		if(!target_lines) return -1;
		if(!wd) return -1;		/* first char on line, no chance */
		lines++;
		wd = 0;				/* retry with this char starting the next line */
	    } else {
		wd += bmp->advance;
		s++;
	    }
	}
	*target_lines = lines;

    return 0;
}

/* Render string using current face assuming that the string will fit 
   as per the previous function */

int ft_render_string(struct ctx *c, const char *str, int start_x, int start_y, int width)
{
    int pen_x, pen_y, x, y;
    struct  ft_ctx *ctx = c->fctx;
    struct bitmap *bmp;
#ifdef HANDLE_UNICODE
    wchar_t ss[strlen(str)+1], *s = ss;
	if(mbstowcs(s, str, strlen(str)+1) <= 0) {
	    log_error("widechar conversion error");	
	    return -1;	
	}
#else
    const char *s = str;	
#endif
	log_info("%s: %d %d wd=%d", __func__, start_x, start_y, width);
	pen_x = start_x;
	pen_y = start_y + ctx->height;

	while(*s) {
	    if(*s == '\n') {
		pen_x = start_x;
		pen_y += ctx->height;
		s++;
		continue;
	    }
	    bmp = get_char_bitmap(ctx, *s);
	    if(!bmp) return -1;
	    if(width && pen_x + bmp->advance > width) {
	        pen_x = start_x;
	        pen_y += ctx->height;
	    }
	    if(c->fmt == WINDOW_FORMAT_RGBA_8888) {
		uint32_t *p32 = (uint32_t *) c->buffer + pen_x + bmp->left + (pen_y - bmp->top) * c->stride;
		for(y = 0; y < bmp->rows; y++, p32 += c->stride - bmp->width)
		    for(x = 0; x < bmp->width; x++) {
			uint8_t val = bmp->buffer[x + y * bmp->pitch];
			*p32++ = (val | (val << 8) | (val << 16));
		    }
	    } else if(c->fmt == WINDOW_FORMAT_RGB_565) {	
		uint16_t *p16 = (uint16_t *) c->buffer + pen_x + bmp->left + (pen_y - bmp->top) * c->stride;
		for(y = 0; y < bmp->rows; y++, p16 += c->stride - bmp->width)
		    for(x = 0; x < bmp->width; x++) {
			uint8_t val = (bmp->buffer[x + y * bmp->pitch] >> 3);
			*p16++ = (val | (val << 6) | (val << 11));
		    }	
	    }	
	    pen_x += bmp->advance;
	    s++;
	}
    return 0;
}


