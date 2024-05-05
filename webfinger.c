/* snac - A simple, minimalistic ActivityPub instance */
/* copyright (c) 2022 - 2024 grunfink et al. / MIT license */

#include "xs.h"
#include "xs_json.h"
#include "xs_curl.h"
#include "xs_mime.h"

#include "snac.h"

int webfinger_request_signed(snac *snac, const char *qs, char **actor, char **user)
/* queries the webfinger for qs and fills the required fields */
{
    int status;
    xs *payload = NULL;
    int p_size = 0;
    xs *headers = xs_dict_new();
    xs *l = NULL;
    xs_str *host = NULL;
    xs *resource = NULL;

    if (xs_startswith(qs, "https:/") || xs_startswith(qs, "http:/")) {
        /* actor query: pick the host */
        xs *s1 = xs_replace_n(qs, "http:/" "/", "", 1);
        xs *s = xs_replace_n(s1, "https:/" "/", "", 1);

        l = xs_split_n(s, "/", 1);

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

    xs *obj = NULL;

    xs *cached_qs = xs_fmt("webfinger:%s", qs);

    /* is it cached? */
    if (valid_status(status = object_get(cached_qs, &obj))) {
        /* nothing more to do */
    }
    else
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
        const char *proto = xs_dict_get_def(srv_config, "protocol", "https");

        xs *url = xs_fmt("%s:/" "/%s/.well-known/webfinger?resource=%s", proto, host, resource);

        if (snac == NULL)
            xs_http_request("GET", url, headers, NULL, 0, &status, &payload, &p_size, 0);
        else
            http_signed_request(snac, "GET", url, headers, NULL, 0, &status, &payload, &p_size, 0);
    }

    if (obj == NULL && valid_status(status) && payload) {
        obj = xs_json_loads(payload);
        object_add(cached_qs, obj);
    }

    if (obj) {
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


int webfinger_get_handler(xs_dict *req, char *q_path,
                           char **body, int *b_size, char **ctype)
/* serves webfinger queries */
{
    int status;

    (void)b_size;

    if (strcmp(q_path, "/.well-known/webfinger") != 0)
        return 0;

    char *q_vars   = xs_dict_get(req, "q_vars");
    char *resource = xs_dict_get(q_vars, "resource");

    if (resource == NULL)
        return 400;

    snac snac;
    int found = 0;

    if (xs_startswith(resource, "https:/") || xs_startswith(resource, "http:/")) {
        /* actor search: find a user with this actor */
        xs *l = xs_split(resource, "/");
        char *uid = xs_list_get(l, -1);

        if (uid)
            found = user_open(&snac, uid);
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
            char *uid  = xs_list_get(l, 0);
            char *host = xs_list_get(l, 1);

            if (strcmp(host, xs_dict_get(srv_config, "host")) == 0)
                found = user_open(&snac, uid);
        }
    }

    if (found) {
        /* build the object */
        xs *acct;
        xs *aaj   = xs_dict_new();
        xs *prof  = xs_dict_new();
        xs *links = xs_list_new();
        xs *obj   = xs_dict_new();

        acct = xs_fmt("acct:%s@%s",
            xs_dict_get(snac.config, "uid"), xs_dict_get(srv_config, "host"));

        aaj = xs_dict_append(aaj, "rel",  "self");
        aaj = xs_dict_append(aaj, "type", "application/activity+json");
        aaj = xs_dict_append(aaj, "href", snac.actor);

        links = xs_list_append(links, aaj);

        prof = xs_dict_append(prof, "rel", "http://webfinger.net/rel/profile-page");
        prof = xs_dict_append(prof, "type", "text/html");
        prof = xs_dict_append(prof, "href", snac.actor);

        links = xs_list_append(links, prof);

        char *avatar = xs_dict_get(snac.config, "avatar");
        if (!xs_is_null(avatar) && *avatar) {
            xs *d = xs_dict_new();

            d = xs_dict_append(d, "rel",  "http:/" "/webfinger.net/rel/avatar");
            d = xs_dict_append(d, "type", xs_mime_by_ext(avatar));
            d = xs_dict_append(d, "href", avatar);

            links = xs_list_append(links, d);
        }

        obj = xs_dict_append(obj, "subject", acct);
        obj = xs_dict_append(obj, "links",   links);

        xs_str *j = xs_json_dumps(obj, 4);

        user_free(&snac);

        status = 200;
        *body  = j;
        *ctype = "application/json";
    }
    else
        status = 404;

    srv_debug(1, xs_fmt("webfinger_get_handler: resource=%s", resource));

    return status;
}
