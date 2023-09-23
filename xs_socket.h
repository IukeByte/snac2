/* copyright (c) 2022 - 2023 grunfink et al. / MIT license */

#ifndef _XS_SOCKET_H

#define _XS_SOCKET_H

int xs_socket_timeout(int s, double rto, double sto);
int xs_socket_server(const char *addr, const char *serv);
FILE *xs_socket_accept(int rs);
xs_str *xs_socket_peername(int s);
int xs_socket_connect(const char *addr, const char *serv);


#ifdef XS_IMPLEMENTATION

#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>


int xs_socket_timeout(int s, double rto, double sto)
/* sets the socket timeout in seconds */
{
    struct timeval tv;
    int ret = 0;

    if (rto > 0.0) {
        tv.tv_sec  = (int)rto;
        tv.tv_usec = (int)((rto - (double)(int)rto) * 1000000.0);

        ret = setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (char *)&tv, sizeof(tv));
    }

    if (sto > 0.0) {
        tv.tv_sec  = (int)sto;
        tv.tv_usec = (int)((sto - (double)(int)sto) * 1000000.0);

        ret = setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (char *)&tv, sizeof(tv));
    }

    return ret;
}


int xs_socket_server(const char *addr, const char *serv)
/* opens a server socket by service name (or port as string) */
{
    int rs = -1;
    struct sockaddr_in host;

    memset(&host, '\0', sizeof(host));

    if (addr != NULL) {
        struct hostent *he;

        if ((he = gethostbyname(addr)) != NULL)
            memcpy(&host.sin_addr, he->h_addr_list[0], he->h_length);
        else
            goto end;
    }

    struct servent *se;

    if ((se = getservbyname(serv, "tcp")) != NULL)
        host.sin_port = se->s_port;
    else
        host.sin_port = htons(atoi(serv));

    host.sin_family = AF_INET;

    if ((rs = socket(AF_INET, SOCK_STREAM, 0)) != -1) {
        /* reuse addr */
        int i = 1;
        setsockopt(rs, SOL_SOCKET, SO_REUSEADDR, (char *)&i, sizeof(i));

        if (bind(rs, (struct sockaddr *)&host, sizeof(host)) == -1) {
            close(rs);
            rs = -1;
        }
        else
            listen(rs, SOMAXCONN);
    }

end:
    return rs;
}


FILE *xs_socket_accept(int rs)
/* accepts an incoming connection */
{
    int cs = -1;
    struct sockaddr_storage addr;
    socklen_t l = sizeof(addr);

    cs = accept(rs, (struct sockaddr *)&addr, &l);

    return cs == -1 ? NULL : fdopen(cs, "r+");
}


xs_str *xs_socket_peername(int s)
/* returns the remote address as a string */
{
    xs_str *ip = NULL;
    struct sockaddr_storage addr;
    socklen_t slen = sizeof(addr);

    if (getpeername(s, (struct sockaddr *)&addr, &slen) != -1) {
        char buf[1024];
        const char *p = NULL;

        if (addr.ss_family == AF_INET) {
            struct sockaddr_in *sa = (struct sockaddr_in *)&addr;

            p = inet_ntop(AF_INET, &sa->sin_addr, buf, sizeof(buf));
        }
        else
        if (addr.ss_family == AF_INET6) {
            struct sockaddr_in6 *sa = (struct sockaddr_in6 *)&addr;

            p = inet_ntop(AF_INET6, &sa->sin6_addr, buf, sizeof(buf));
        }

        if (p != NULL)
            ip = xs_str_new(p);
    }

    return ip;
}


int xs_socket_connect(const char *addr, const char *serv)
/* creates a client connection socket */
{
    int d = -1;

#ifndef WITHOUT_GETADDRINFO
    struct addrinfo *res;
    struct addrinfo hints;

    memset(&hints, '\0', sizeof(hints));

    hints.ai_socktype   = SOCK_STREAM;
    hints.ai_flags      = AI_ADDRCONFIG;

    if (getaddrinfo(addr, serv, &hints, &res) == 0) {
        struct addrinfo *r;

        for (r = res; r != NULL; r = r->ai_next) {
            d = socket(r->ai_family, r->ai_socktype, r->ai_protocol);

            if (d != -1) {
                if (connect(d, r->ai_addr, r->ai_addrlen) == 0)
                    break;

                close(d);
                d = -1;
            }
        }

        freeaddrinfo(res);
    }

#else /* WITHOUT_GETADDRINFO */

    /* traditional socket interface */
    struct hostent *he;

    if ((he = gethostbyname(addr)) != NULL) {
        struct sockaddr_in host;

        memset(&host, '\0', sizeof(host));

        memcpy(&host.sin_addr, he->h_addr_list[0], he->h_length);
        host.sin_family = he->h_addrtype;

        struct servent *se;

        if ((se = getservbyname(serv, "tcp")) != NULL)
            host.sin_port = se->s_port;
        else
            host.sin_port = htons(atoi(serv));

        if ((d = socket(AF_INET, SOCK_STREAM, 0)) != -1) {
            if (connect(d, (struct sockaddr *)&host, sizeof(host)) == -1)
                d = -1;
        }
    }

#endif  /* WITHOUT_GETADDRINFO */

    return d;
}


#endif /* XS_IMPLEMENTATION */

#endif /* _XS_SOCKET_H */
