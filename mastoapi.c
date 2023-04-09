/* snac - A simple, minimalistic ActivityPub instance */
/* copyright (c) 2022 - 2023 grunfink / MIT license */

#include "xs.h"
#include "xs_encdec.h"
#include "xs_json.h"
#include "xs_io.h"
#include "xs_time.h"

#include "snac.h"

static xs_str *random_str(void)
/* just what is says in the tin */
{
    unsigned int data[4] = {0};
    FILE *f;

    if ((f = fopen("/dev/random", "r")) != NULL) {
        fread(data, sizeof(data), 1, f);
        fclose(f);
    }
    else {
        data[0] = random() % 0xffffffff;
        data[1] = random() % 0xffffffff;
        data[2] = random() % 0xffffffff;
        data[3] = random() % 0xffffffff;
    }

    return xs_hex_enc((char *)data, sizeof(data));
}


int app_add(const char *id, const xs_dict *app)
/* stores an app */
{
    int status = 201;
    xs *fn     = xs_fmt("%s/app/", srv_basedir);
    FILE *f;

    mkdirx(fn);
    fn = xs_str_cat(fn, id);
    fn = xs_str_cat(fn, ".json");

    if ((f = fopen(fn, "w")) != NULL) {
        xs *j = xs_json_dumps_pp(app, 4);
        fwrite(j, strlen(j), 1, f);
        fclose(f);
    }
    else
        status = 500;

    return status;
}


xs_dict *app_get(const char *id)
/* gets an app */
{
    xs *fn       = xs_fmt("%s/app/%s.json", srv_basedir, id);
    xs_dict *app = NULL;
    FILE *f;

    if ((f = fopen(fn, "r")) != NULL) {
        xs *j = xs_readall(f);
        fclose(f);

        app = xs_json_loads(j);
    }

    return app;
}


const char *login_page = ""
"<!DOCTYPE html>\n"
"<body><h1>%s OAuth identify</h1>\n"
"<div style=\"background-color: red; color: white\">%s</div>\n"
"<form method=\"post\" action=\"https:/" "/%s/oauth/x-snac-login\">\n"
"<p>Login: <input type=\"text\" name=\"login\"></p>\n"
"<p>Password: <input type=\"password\" name=\"passwd\"></p>\n"
"<input type=\"hidden\" name=\"redir\" value=\"%s\">\n"
"<input type=\"hidden\" name=\"cid\" value=\"%s\">\n"
"<input type=\"submit\" value=\"OK\">\n"
"</form><p>%s</p></body>\n"
"";

int oauth_get_handler(const xs_dict *req, const char *q_path,
                      char **body, int *b_size, char **ctype)
{
    if (!xs_startswith(q_path, "/oauth/"))
        return 0;

    {
        xs *j = xs_json_dumps_pp(req, 4);
        printf("oauth get:\n%s\n", j);
    }

    int status   = 404;
    xs_dict *msg = xs_dict_get(req, "q_vars");
    xs *cmd      = xs_replace(q_path, "/oauth", "");

    srv_debug(0, xs_fmt("oauth_get_handler %s", q_path));

    if (strcmp(cmd, "/authorize") == 0) {
        const char *cid   = xs_dict_get(msg, "client_id");
        const char *ruri  = xs_dict_get(msg, "redirect_uri");
        const char *rtype = xs_dict_get(msg, "response_type");

        status = 400;

        if (cid && ruri && rtype && strcmp(rtype, "code") == 0) {
            xs *app = app_get(cid);

            if (app != NULL) {
                const char *host = xs_dict_get(srv_config, "host");

                *body  = xs_fmt(login_page, host, "", host, ruri, cid, USER_AGENT);
                *ctype = "text/html";
                status = 200;

                srv_debug(0, xs_fmt("oauth authorize: generating login page"));
            }
            else
                srv_debug(0, xs_fmt("oauth authorize: bad client_id %s", cid));
        }
        else
            srv_debug(0, xs_fmt("oauth authorize: invalid or unset arguments"));
    }

    return status;
}


int oauth_post_handler(const xs_dict *req, const char *q_path,
                       const char *payload, int p_size,
                       char **body, int *b_size, char **ctype)
{
    if (!xs_startswith(q_path, "/oauth/"))
        return 0;

    {
        xs *j = xs_json_dumps_pp(req, 4);
        printf("oauth post:\n%s\n", j);
    }

    int status   = 404;
    xs_dict *msg = xs_dict_get(req, "p_vars");
    xs *cmd      = xs_replace(q_path, "/oauth", "");

    srv_debug(0, xs_fmt("oauth_post_handler %s", q_path));

    if (strcmp(cmd, "/x-snac-login") == 0) {
        const char *login  = xs_dict_get(msg, "login");
        const char *passwd = xs_dict_get(msg, "passwd");
        const char *redir  = xs_dict_get(msg, "redir");
        const char *cid    = xs_dict_get(msg, "cid");

        const char *host = xs_dict_get(srv_config, "host");

        /* by default, generate another login form with an error */
        *body  = xs_fmt(login_page, host, "LOGIN INCORRECT", host, redir, cid, USER_AGENT);
        *ctype = "text/html";
        status = 200;

        if (login && passwd && redir && cid) {
            snac snac;

            if (user_open(&snac, login)) {
                /* check the login + password */
                if (check_password(login, passwd,
                    xs_dict_get(snac.config, "passwd"))) {
                    /* success! redirect to the desired uri */
                    xs *code = random_str();

                    xs_free(*body);
                    *body = xs_fmt("%s?code=%s", redir, code);
                    status = 303;

                    srv_debug(0, xs_fmt("oauth x-snac-login: redirect to %s", *body));
                }
                else
                    srv_debug(0, xs_fmt("oauth x-snac-login: login '%s' incorrect", login));

                user_free(&snac);
            }
            else
                srv_debug(0, xs_fmt("oauth x-snac-login: bad user '%s'", login));
        }
        else
            srv_debug(0, xs_fmt("oauth x-snac-login: invalid or unset arguments"));
    }
    else
    if (strcmp(cmd, "/token") == 0) {
        const char *gtype = xs_dict_get(msg, "grant_type");
        const char *code  = xs_dict_get(msg, "code");
        const char *cid   = xs_dict_get(msg, "client_id");
        const char *csec  = xs_dict_get(msg, "client_secret");
        const char *ruri  = xs_dict_get(msg, "redirect_uri");

        if (gtype && code && cid && csec && ruri) {
            xs *rsp   = xs_dict_new();
            xs *cat   = xs_number_new(time(NULL));
            xs *token = random_str();

            rsp = xs_dict_append(rsp, "access_token", token);
            rsp = xs_dict_append(rsp, "token_type",   "Bearer");
            rsp = xs_dict_append(rsp, "created_at",   cat);

            *body  = xs_json_dumps_pp(rsp, 4);
            *ctype = "application/json";
            status = 200;

            srv_debug(0, xs_fmt("oauth token: successful login, token %s", token));
        }
        else {
            srv_debug(0, xs_fmt("oauth token: invalid or unset arguments"));
            status = 400;
        }
    }
    else
    if (strcmp(cmd, "/revoke") == 0) {
        const char *cid   = xs_dict_get(msg, "client_id");
        const char *csec  = xs_dict_get(msg, "client_secret");
        const char *token = xs_dict_get(msg, "token");

        if (cid && csec && token) {
            *body  = xs_str_new("{}");
            *ctype = "application/json";
            status = 200;
        }
        else
            status = 400;
    }

    return status;
}


int mastoapi_get_handler(const xs_dict *req, const char *q_path,
                         char **body, int *b_size, char **ctype)
{
    if (!xs_startswith(q_path, "/api/v1/"))
        return 0;

    {
        xs *j = xs_json_dumps_pp(req, 4);
        printf("mastoapi get:\n%s\n", j);
    }

    int status   = 404;
    xs_dict *msg = xs_dict_get(req, "q_vars");
    xs *cmd      = xs_replace(q_path, "/api/v1", "");

    srv_debug(0, xs_fmt("mastoapi_get_handler %s", q_path));

    if (strcmp(cmd, "/accounts/verify_credentials") == 0) {
    }

    return status;
}


int mastoapi_post_handler(const xs_dict *req, const char *q_path,
                          const char *payload, int p_size,
                          char **body, int *b_size, char **ctype)
{
    if (!xs_startswith(q_path, "/api/v1/"))
        return 0;

    {
        xs *j = xs_json_dumps_pp(req, 4);
        printf("mastoapi post:\n%s\n", j);
    }

    int status    = 404;
    xs *msg       = NULL;
    char *i_ctype = xs_dict_get(req, "content-type");

    if (xs_startswith(i_ctype, "application/json"))
        msg = xs_json_loads(payload);
    else
        msg = xs_dup(xs_dict_get(req, "p_vars"));

    if (msg == NULL)
        return 400;

    {
        xs *j = xs_json_dumps_pp(req, 4);
        printf("%s\n", j);
    }
    {
        xs *j = xs_json_dumps_pp(msg, 4);
        printf("%s\n", j);
    }

    xs *cmd = xs_replace(q_path, "/api/v1", "");

    if (strcmp(cmd, "/apps") == 0) {
        const char *name = xs_dict_get(msg, "client_name");
        const char *ruri = xs_dict_get(msg, "redirect_uris");

        if (name && ruri) {
            xs *app  = xs_dict_new();
            xs *id   = xs_replace_i(tid(0), ".", "");
            xs *cid  = random_str();
            xs *csec = random_str();
            xs *vkey = random_str();

            app = xs_dict_append(app, "name",          name);
            app = xs_dict_append(app, "redirect_uri",  ruri);
            app = xs_dict_append(app, "client_id",     cid);
            app = xs_dict_append(app, "client_secret", csec);
            app = xs_dict_append(app, "vapid_key",     vkey);
            app = xs_dict_append(app, "id",            id);

            *body  = xs_json_dumps_pp(app, 4);
            *ctype = "application/json";
            status = 200;

            app = xs_dict_append(app, "code", "");
            app_add(cid, app);
        }
    }

    return status;
}