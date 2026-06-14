/*
 * no_semihosting.c
 *
 * Complete bare-metal retarget layer for Arm Compiler 6 (ArmClang).
 *
 * Why this file exists:
 *   rlottie is a C++ library. Linking it pulls in the C/C++ standard library's
 *   stdio init path, which references the low-level _sys_* I/O primitives. By
 *   default armlink resolves those to the semihosting stubs in sys_io.o, which
 *   execute a BKPT (SVC) and HardFault on a board with no debugger servicing
 *   semihosting (observed as PC inside _sys_open, HFSR.DEBUGEVT / DFSR.BKPT).
 *
 *   Requesting __use_no_semihosting removes the semihosting stubs, but then the
 *   library REQUIRES the application to provide every referenced _sys_* symbol
 *   (otherwise armlink reports L6915E). This file provides the full set,
 *   unconditionally (no __ARMCC_VERSION gating), so it links cleanly on AC6.22.
 *
 *   Console output (stdout/stderr) is routed to SEGGER RTT so printf-style
 *   diagnostics still work; file/seek/read operations are stubbed out because
 *   the firmware does not use C stdio file access (SD card goes through FatFs).
 */

#if defined(__ARMCC_VERSION)

#include <stdio.h>
#include <string.h>
#include <rt_sys.h>
#include <rt_misc.h>

#include "SEGGER_RTT.h"

/* Tell the Arm C library not to use semihosting; we provide _sys_* ourselves.
 * AC6/ArmClang does not support the AC5 `#pragma import(__use_no_semihosting)`,
 * so reference the guard symbol directly via inline asm instead. */
__asm(".global __use_no_semihosting");

/* Standard stream handles. Must be < 0x8000 per the rt_sys.h contract is not
 * required, but keeping them distinct lets _sys_write route stdout/stderr. */
#define STDIN_HANDLE  0x8001
#define STDOUT_HANDLE 0x8002
#define STDERR_HANDLE 0x8003

/* Names the library passes to _sys_open for the standard streams. */
const char __stdin_name[]  = ":tt";
const char __stdout_name[] = ":tt";
const char __stderr_name[] = ":tt";

FILEHANDLE _sys_open(const char *name, int openmode)
{
    (void)openmode;

    if (name == NULL) {
        return STDOUT_HANDLE;
    }
    /* All three standard streams share the ":tt" name; disambiguate by mode is
     * not possible here, so map everything to STDOUT. The library opens stdin,
     * stdout and stderr in that order; routing all to RTT channel 0 is fine. */
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

    /* Route console streams to RTT; ignore everything else. */
    if (fh == STDOUT_HANDLE || fh == STDERR_HANDLE) {
        SEGGER_RTT_Write(0, (const char *)buf, len);
    }

    /* Return 0 == all bytes written. */
    return 0;
}

int _sys_read(FILEHANDLE fh, unsigned char *buf, unsigned len, int mode)
{
    (void)fh;
    (void)buf;
    (void)len;
    (void)mode;
    /* No console input. Signal EOF. */
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
        /* Halt: firmware should never return from main(). */
    }
}

#endif /* __ARMCC_VERSION */
