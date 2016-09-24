#include <unistd.h>
#include <dlfcn.h>
#include <android/log.h>

#define TAG_NAME        "test2:test_cplusplus"
#define log_error(fmt,args...) __android_log_print(ANDROID_LOG_ERROR, TAG_NAME, (const char *) fmt, ##args)
#define log_info(fmt,args...) __android_log_print(ANDROID_LOG_INFO, TAG_NAME, (const char *) fmt, ##args)

extern "C" {
extern void *fake_dlopen(const char *filename, int flags);
extern void *fake_dlsym(void *handle, const char *symbol);
extern void fake_dlclose(void *handle);
}

#if 0

The function below loads Android "libutils.so" to run the following c++ code:

    const char *str = "Hello, world!";
    android::String8 *str8 = new android::String8(str);
    size_t len = str8->getUtf32Length();
    log_info("%s: length = %d", str, (int) len);
    delete str8;

#endif

extern "C" int test_cplusplus()
{
    log_info("testing libutils.so");
#ifdef __arm__
    void *handle = fake_dlopen("/system/lib/libutils.so", RTLD_NOW);
#elif defined(__aarch64__)
    void *handle = fake_dlopen("/system/lib64/libutils.so", RTLD_NOW);
#else
#error "Arch unknown, please port me" 
#endif

    if(!handle) return log_error("cannot load libutils.so\n");   

    // Constructor:  android::String8::String8(char const*)
    // The first argument is a pointer where "this" of a new object is to be stored.
    		
    void (*create_string) (void **, const char *) = 
	(typeof(create_string)) fake_dlsym(handle, "_ZN7android7String8C1EPKc");

    // Member function:  size_t android::String8::getUtf32Length() const
    // The argument is a pointer to "this" of the object

    size_t (*get_len) (void **) = 
	(typeof(get_len)) fake_dlsym(handle, "_ZNK7android7String814getUtf32LengthEv");

    // Destructor:  android::String8::~String8()

    void (*delete_string) (void **) = 	
	(typeof(delete_string)) fake_dlsym(handle, "_ZN7android7String8D1Ev");

    // All required library addresses known now, so don't need its handle anymore

    fake_dlclose(handle);

    if(!create_string || !get_len || !delete_string) 
	return log_error("required functions missing in libutils.so\n");

    // Fire up now.

    void *str8 = 0;
    const char *str = "Hello, world!";

    create_string(&str8, str);
    if(!str8) return log_error("failed to create string\n");

    size_t len = get_len(&str8);
    log_info("%s: length = %d", str, (int) len);

    delete_string(&str8);

    return 0;

}



