/*
 * Bare-metal Keil/Arm Compiler 6 retarget layer for rlottie/libc++.
 *
 * Linking rlottie pulls in parts of the C/C++ runtime. Without this file the
 * Arm C library can resolve low-level stdio calls to semihosting stubs, which
 * execute BKPT instructions on target hardware. This file requests no
 * semihosting and retargets stdout/stderr to SEGGER RTT.
 */

#if defined(__ARMCC_VERSION)

#include <stdio.h>
#include <string.h>
#include <rt_sys.h>
#include <rt_misc.h>

#include "SEGGER_RTT.h"

__asm(".global __use_no_semihosting");

#define STDIN_HANDLE  0x8001
#define STDOUT_HANDLE 0x8002
#define STDERR_HANDLE 0x8003

const char __stdin_name[]  = ":tt";
const char __stdout_name[] = ":tt";
const char __stderr_name[] = ":tt";

FILEHANDLE _sys_open(const char *name, int openmode)
{
    (void)openmode;

    if (name == NULL) {
        return STDOUT_HANDLE;
    }
    return STDOUT_HANDLE;
}

int _sys_close(FILEHANDLE fh)
{
    (void)fh;
    return 0;
}

int _sys_write(FILEHANDLE fh, const unsigned char *buf, unsigned len, int mode)
{
    (void)mode;

    if (buf == NULL) {
        return 0;
    }

    if (fh == STDOUT_HANDLE || fh == STDERR_HANDLE) {
        SEGGER_RTT_Write(0, (const char *)buf, len);
    }

    return 0;
}

int _sys_read(FILEHANDLE fh, unsigned char *buf, unsigned len, int mode)
{
    (void)fh;
    (void)buf;
    (void)len;
    (void)mode;
    return -1;
}

void _ttywrch(int ch)
{
    char c = (char)ch;
    SEGGER_RTT_Write(0, &c, 1U);
}

int _sys_istty(FILEHANDLE fh)
{
    if (fh == STDIN_HANDLE || fh == STDOUT_HANDLE || fh == STDERR_HANDLE) {
        return 1;
    }
    return 0;
}

int _sys_seek(FILEHANDLE fh, long pos)
{
    (void)fh;
    (void)pos;
    return -1;
}

int _sys_ensure(FILEHANDLE fh)
{
    (void)fh;
    return -1;
}

long _sys_flen(FILEHANDLE fh)
{
    (void)fh;
    return 0;
}

char *_sys_command_string(char *cmd, int len)
{
    (void)len;
    return cmd;
}

void _sys_exit(int return_code)
{
    (void)return_code;
    for (;;) {
    }
}

#endif
