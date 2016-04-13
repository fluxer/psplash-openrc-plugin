/* psplash.c - psplash plugin for the OpenRC init system
 *
 * Copyright (C) 2011  Amadeusz Żołnowski <aidecoe@gentoo.org>
 * Copyright (C) 2016  Ivailo Monev <xakepa10@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include <assert.h>
#include <einfo.h>
#include <rc.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>


#ifdef DEBUG
#    define DBG(x) einfo("[psplash-plugin] " x)
#else
#    define DBG(x)
#endif

#define BUFFER_SIZE 300
#define RWFILE (R_OK | W_OK)
#define RWDIR (R_OK | W_OK | X_OK)

#ifndef RUN_DIR
#define RUN_DIR "/run"
#endif

#ifndef FIFO_FILE
#define FIFO_FILE RUN_DIR "/psplash_fifo"
#endif

int command(const char* cmd)
{
    int rv = system(cmd);

#if DEBUG
    if(rv != 0) {
        ewarn("[psplash-plugin] command(\"%s\"): rv=%d", cmd, rv);
    }
#endif

    return rv;
}


int commandf(const char* cmd, ...)
{
    static char buffer[BUFFER_SIZE];
    va_list ap;
    int rv;

    va_start(ap, cmd);
    rv = vsnprintf(buffer, BUFFER_SIZE, cmd, ap);
    va_end(ap);
    if(rv >= BUFFER_SIZE) {
        eerror("[psplash-plugin] command(\"%s\"): buffer overflow", buffer);
        return -1;
    }

    return command(buffer);
}


bool ply_message(const char* hook, const char* name)
{
    return (commandf("TMPDIR=\"%s\" /bin/psplash-write \"MSG %s %s\"", RUN_DIR, hook, name) == 0);
}


bool ply_ping()
{
    return (access(FIFO_FILE, RWFILE) == 0);
}


bool ply_quit()
{
    int rv = commandf("TMPDIR=\"%s\" /bin/psplash-write QUIT", RUN_DIR);

    return (rv == 0);
}


bool ply_start()
{
    

    if(!ply_ping()) {
        ebegin("Starting psplash");

        if(access(RUN_DIR, RWDIR) != 0) {
            if(mkdir(RUN_DIR, 0755) != 0) {
                eerror("[psplash-plugin] Couldn't create " RUN_DIR);
                return false;
            }
        }

        int rv = commandf("TMPDIR=\"%s\" /bin/psplash --no-progress &", RUN_DIR);
        eend(rv, "");

        if(rv != 0)
            return false;
    }

    return true;
}


int rc_plugin_hook(RC_HOOK hook, const char *name)
{
    int rv = 0;
    char* runlevel = rc_runlevel_get();
    const char* bootlevel = getenv("RC_BOOTLEVEL");
    const char* defaultlevel = getenv("RC_DEFAULTLEVEL");

#ifdef DEBUG
    einfo("hook=%d name=%s runlvl=%s plyd=%d", hook, name, runlevel,
            ply_ping());
#endif

    /* Don't do anything if we're not booting or shutting down. */
    if(!(rc_runlevel_starting() || rc_runlevel_stopping())) {
        switch(hook) {
            case RC_HOOK_RUNLEVEL_STOP_IN:
            case RC_HOOK_RUNLEVEL_STOP_OUT:
            case RC_HOOK_RUNLEVEL_START_IN:
            case RC_HOOK_RUNLEVEL_START_OUT:
                /* Switching runlevels, so we're booting or shutting down.*/
                break;
            default:
                DBG("Not booting or shutting down");
                goto exit;
        }
    }

    DBG("switch");

    switch(hook) {
    case RC_HOOK_RUNLEVEL_STOP_IN:
        /* Start the psplash daemon and show splash when system is being shut
         * down. */
        if(strcmp(name, RC_LEVEL_SHUTDOWN) == 0) {
            DBG("ply_start()");
            if(!ply_start())
                rv = 1;
        }
        break;

    case RC_HOOK_RUNLEVEL_START_IN:
        /* Start the psplash daemon and show splash when entering the boot
         * runlevel. Required /proc and /sys should already be mounted in
         * sysinit runlevel. */
        if(strcmp(name, bootlevel) == 0) {
            DBG("ply_start()");
            if(!ply_start())
                rv = 1;
        }
        break;

    case RC_HOOK_RUNLEVEL_START_OUT:
        /* Stop the psplash daemon right after default runlevel is started. */
        if(strcmp(name, defaultlevel) == 0) {
            DBG("ply_quit()");
            if(!ply_quit())
                rv = 1;
        }
        break;

    case RC_HOOK_SERVICE_STOP_IN:
        /* Quit psplash when we're going to lost write access to /var/... */
        if(strcmp(name, "localmount") == 0 &&
                strcmp(runlevel, RC_LEVEL_SHUTDOWN) == 0) {
            DBG("ply_quit()");
            if(!ply_quit())
                rv = 1;
        }
        break;

    case RC_HOOK_SERVICE_STOP_NOW:
        if(!ply_message("Stopping service", name))
            rv = 1;
        break;

    case RC_HOOK_SERVICE_START_NOW:
        if(!ply_message("Starting service", name))
            rv = 1;
        break;

    default:
        break;
    }

exit:
    free(runlevel);
    return rv;
}
