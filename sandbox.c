#include "xs.h"

#include "snac.h"

#include <unistd.h>

#if defined (__linux__)

#define LL_PRINTERR(fmt, ...) srv_debug(0, xs_fmt(fmt, __VA_ARGS__))
#include "landloc.h"

static
LL_BEGIN(sbox_enter_linux_, const char* basedir, const char *address, int smail) {

    const unsigned long long
        r = LANDLOCK_ACCESS_FS_READ_DIR    |
            LANDLOCK_ACCESS_FS_READ_FILE,
        w = LANDLOCK_ACCESS_FS_WRITE_FILE  |
            LANDLOCK_ACCESS_FS_TRUNCATE,
        c = LANDLOCK_ACCESS_FS_MAKE_DIR    |
            LANDLOCK_ACCESS_FS_MAKE_REG    |
            LANDLOCK_ACCESS_FS_TRUNCATE    |
            LANDLOCK_ACCESS_FS_MAKE_SYM    |
            LANDLOCK_ACCESS_FS_REMOVE_DIR  |
            LANDLOCK_ACCESS_FS_REMOVE_FILE |
            LANDLOCK_ACCESS_FS_REFER,
        s = LANDLOCK_ACCESS_FS_MAKE_SOCK,
        x = LANDLOCK_ACCESS_FS_EXECUTE;

    LL_PATH(basedir,                r|w|c);
    LL_PATH("/tmp",                 r|w|c);
#ifndef WITHOUT_SHM
    LL_PATH("/dev/shm",             r|w|c);
#endif
    LL_PATH("/etc/resolv.conf",     r    );
    LL_PATH("/etc/hosts",           r    );
    LL_PATH("/etc/ssl/openssl.cnf", r    );
    LL_PATH("/etc/ssl/cert.pem",    r    );
    LL_PATH("/usr/share/zoneinfo",  r    );

    if (*address == '/')
        LL_PATH(address, s);

    if (smail)
        LL_PATH("/usr/sbin/sendmail", x);

    if (*address != '/') {
        unsigned short listen_port = xs_number_get(xs_dict_get(srv_config, "port"));
        LL_PORT(listen_port, LANDLOCK_ACCESS_NET_BIND_TCP);
    }

    LL_PORT(80,  LANDLOCK_ACCESS_NET_CONNECT_TCP);
    LL_PORT(443, LANDLOCK_ACCESS_NET_CONNECT_TCP);

} LL_END

#endif

void sbox_enter(const char *basedir)
{
    if (xs_is_true(xs_dict_get(srv_config, "disable_openbsd_security"))) {
        srv_log(xs_dup("disable_openbsd_security is deprecated. Use disable_sandbox instead."));
        return;
    }
    if (xs_is_true(xs_dict_get(srv_config, "disable_sandbox"))) {
        srv_debug(0, xs_dup("Sandbox disabled by admin"));
        return;
    }

    const char *address = xs_dict_get(srv_config, "address");

    int smail = !xs_is_true(xs_dict_get(srv_config, "disable_email_notifications"));

#if defined (__OpenBSD__)
    srv_debug(1, xs_fmt("Calling unveil()"));
    unveil(basedir,                "rwc");
    unveil("/tmp",                 "rwc");
    unveil("/etc/resolv.conf",     "r");
    unveil("/etc/hosts",           "r");
    unveil("/etc/ssl/openssl.cnf", "r");
    unveil("/etc/ssl/cert.pem",    "r");
    unveil("/usr/share/zoneinfo",  "r");

    if (smail)
        unveil("/usr/sbin/sendmail",   "x");

    if (*address == '/')
        unveil(address, "rwc");

    unveil(NULL,                   NULL);

    srv_debug(1, xs_fmt("Calling pledge()"));

    xs *p = xs_str_new("stdio rpath wpath cpath flock inet proc dns fattr");

    if (smail)
        p = xs_str_cat(p, " exec");

    if (*address == '/')
        p = xs_str_cat(p, " unix");

    pledge(p, NULL);

    xs_free(p);
#elif defined (__linux__)
    
    sbox_enter_linux_(basedir, address, smail);

    srv_log(xs_dup("landlocked"));

#endif
}
