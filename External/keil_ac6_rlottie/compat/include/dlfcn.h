#ifndef AC6_RLOTTIE_DLFCN_H
#define AC6_RLOTTIE_DLFCN_H

#ifdef __cplusplus
extern "C" {
#endif

#define RTLD_LAZY 1
#define RTLD_NOW 2

static inline void *dlopen(const char *filename, int flags)
{
    (void)filename;
    (void)flags;
    return 0;
}

static inline void *dlsym(void *handle, const char *symbol)
{
    (void)handle;
    (void)symbol;
    return 0;
}

static inline int dlclose(void *handle)
{
    (void)handle;
    return 0;
}

static inline char *dlerror(void)
{
    return "dynamic loading is not supported";
}

#ifdef __cplusplus
}
#endif

#endif

