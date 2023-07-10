/* snac - A simple, minimalistic ActivityPub instance */
/* copyright (c) 2022 - 2023 grunfink / MIT license */

#include "xs.h"
#include "xs_json.h"
#include "xs_curl.h"
#include "xs_url.h"

#include "snac.h"

int webfinger_request_signed(snac *snac, const char *qs, char **actor, char **user)
/* queries the webfinger for qs and fills the required fields */
{
    int status;
    xs *payload = NULL;
    int p_size = 0;
    xs *headers = xs_dict_new();
    xs *l = NULL;
    d_char *host = NULL;
    xs *resource = NULL;

    const char* rest_of_url = NULL;
    if (xs_is_http_https(qs, &rest_of_url)) {
        /* actor query: pick the host */

        l = xs_split_n(rest_of_url, "/", 1);

        host     = xs_list_get(l, 0);
        resource = xs_dup(qs);
    }
    else {
        /* it's a user */
        xs *s = xs_strip_chars_i(xs_dup(qs), "@.");

        l = xs_split_n(s, "@", 1);

        if (xs_list_len(l) == 2) {
            host     = xs_list_get(l, 1);
            resource = xs_fmt("acct:%s", s);
        }
    }

    if (host == NULL || resource == NULL)
        return 400;

    headers = xs_dict_append(headers, "accept",     "application/json");
    headers = xs_dict_append(headers, "user-agent", USER_AGENT);

    /* is it a query about one of us? */
    if (strcmp(host, xs_dict_get(srv_config, "host")) == 0) {
        /* route internally */
        xs *req    = xs_dict_new();
        xs *q_vars = xs_dict_new();
        char *ctype;

        q_vars = xs_dict_append(q_vars, "resource", resource);
        req    = xs_dict_append(req, "q_vars", q_vars);

        status = webfinger_get_handler(req, "/.well-known/webfinger",
                                       &payload, &p_size, &ctype);
    }
    else {
        xs_val *scheme = xs_dict_get(srv_config, "scheme_webfinger");
        if (scheme == NULL) scheme = "https";
        xs *url = xs_cat(scheme, "://", host, "/.well-known/webfinger?resource=", resource);

        if (snac == NULL)
            xs_http_request("GET", url, headers, NULL, 0, &status, &payload, &p_size, 0);
        else
            http_signed_request(snac, "GET", url, headers, NULL, 0, &status, &payload, &p_size, 0);
    }

    if (valid_status(status)) {
        xs *obj = xs_json_loads(payload);

        if (user != NULL) {
            char *subject = xs_dict_get(obj, "subject");

            if (subject)
                *user = xs_replace_n(subject, "acct:", "", 1);
        }

        if (actor != NULL) {
            char *list = xs_dict_get(obj, "links");
            char *v;

            while (xs_list_iter(&list, &v)) {
                if (xs_type(v) == XSTYPE_DICT) {
                    char *type = xs_dict_get(v, "type");

                    if (type && (strcmp(type, "application/activity+json") == 0 ||
                                strcmp(type, "application/ld+json; profile=\"https://www.w3.org/ns/activitystreams\"") == 0)) {
                        *actor = xs_dup(xs_dict_get(v, "href"));
                        break;
                    }
                }
            }
        }
    }

    return status;
}


int webfinger_request(const char *qs, char **actor, char **user)
/* queries the webfinger for qs and fills the required fields */
{
    return webfinger_request_signed(NULL, qs, actor, user);
}


int webfinger_get_handler(const xs_dict *req, const char *q_path,
                          char **body, int *b_size, char **ctype)
/* serves webfinger queries */
{
    int status;

    (void)b_size;

    if (strcmp(q_path, "/.well-known/webfinger") != 0)
        return 0;

    const xs_dict *q_vars = xs_dict_get(req, "q_vars");
    const char *resource  = xs_dict_get(q_vars, "resource");

    if (resource == NULL)
        return 400;

    snac snac;
    int found = 0;

    if (xs_is_http_https(resource, NULL)) {
        /* actor search: find a user with this actor */
        xs *list = user_list();
        xs_list *p;
        char *uid;

        p = list;
        while (xs_list_iter(&p, &uid)) {
            if (user_open(&snac, uid)) {
                if (xs_compare_url(snac.actor, resource)) {
                    found = 1;
                    break;
                }

                user_free(&snac);
            }
        }
    }
    else
    if (xs_startswith(resource, "acct:")) {
        /* it's an account name */
        xs *an = xs_replace_n(resource, "acct:", "", 1);
        xs *l = NULL;

        /* strip a possible leading @ */
        if (xs_startswith(an, "@"))
            an = xs_crop_i(an, 1, 0);

        l = xs_split_n(an, "@", 1);

        if (xs_list_len(l) == 2) {
            const char *uid  = xs_list_get(l, 0);
            const char *host = xs_list_get(l, 1);

            if (strcmp(host, xs_dict_get(srv_config, "host")) == 0)
                found = user_open(&snac, uid);

            if (!found) {
                /* get the list of possible domain aliases */
                xs_list *domains = xs_dict_get(srv_config, "webfinger_domains");
                char *v;

                while (!found && xs_list_iter(&domains, &v)) {
                    if (strcmp(host, v) == 0)
                        found = user_open(&snac, uid);
                }
            }
        }
    }

    if (found) {
        /* build the object */
        xs *acct;
        xs *aaj   = xs_dict_new();
        xs *links = xs_list_new();
        xs *obj   = xs_dict_new();

        acct = xs_fmt("acct:%s@%s",
            xs_dict_get(snac.config, "uid"), xs_dict_get(srv_config, "host"));

        aaj = xs_dict_append(aaj, "rel",  "self");
        aaj = xs_dict_append(aaj, "type", "application/activity+json");
        aaj = xs_dict_append(aaj, "href", snac.actor);

        links = xs_list_append(links, aaj);

        obj = xs_dict_append(obj, "subject", acct);
        obj = xs_dict_append(obj, "links",   links);

        user_free(&snac);

        status = 200;
        *body  = xs_json_dumps_pp(obj, 4);
        *ctype = "application/json";
    }
    else
        status = 404;

    return status;
}
