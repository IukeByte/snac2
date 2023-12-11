/* snac - A simple, minimalistic ActivityPub instance */
/* copyright (c) 2022 - 2023 grunfink et al. / MIT license */

#include "xs.h"
#include "xs_json.h"
#include "xs_curl.h"
#include "xs_mime.h"
#include "xs_openssl.h"
#include "xs_regex.h"
#include "xs_time.h"
#include "xs_set.h"
#include "xs_match.h"

#include "snac.h"

#include <sys/wait.h>

const char *public_address = "https:/" "/www.w3.org/ns/activitystreams#Public";

/* susie.png */

const char *susie =
    "iVBORw0KGgoAAAANSUhEUgAAAEAAAABAAQAAAAC"
    "CEkxzAAAAUUlEQVQoz43R0QkAMQwCUDdw/y3dwE"
    "vsvzlL4X1IoQkAisKmwfAFT3RgJHbQezpSRoXEq"
    "eqCL9BJBf7h3QbOCCxV5EVWMEMwG7K1/WODtlvx"
    "AYTtEsDU9F34AAAAAElFTkSuQmCC";

const char *susie_cool =
    "iVBORw0KGgoAAAANSUhEUgAAAEAAAABAAQAAAAC"
    "CEkxzAAAAV0lEQVQoz43RwQ3AMAwCQDZg/y3ZgN"
    "qo3+JaedwDOUQBQFHYaTB8wTM6sGl2cMPu+DFzn"
    "+ZcgN7wF7ZVihXkfSlWIVzIA6dbQzaygllpNuTX"
    "ZmmFNlvxADX1+o0cUPMbAAAAAElFTkSuQmCC";

const char *susie_muertos = 
    "iVBORw0KGgoAAAANSUhEUgAAAEAAAABAAQAAAAC"
    "CEkxzAAAAV0lEQVQoz4XQsQ0AMQxCUW/A/lv+DT"
    "ic6zGRolekIMyMELNp8PiCEw6Q4w4NoAt53IH5m"
    "xXksrZYgZwJrIox+Z8vJAfe2lCxG6AK7eKkWcEb"
    "QHbF617xAQatAAD7jJHUAAAAAElFTkSuQmCC";


const char *default_avatar_base64(void)
/* returns the default avatar in base64 */
{
    time_t t = time(NULL);
    struct tm tm;
    const char *p = susie;

    gmtime_r(&t, &tm);

    if (tm.tm_mon == 10 && tm.tm_mday == 2)
        p = susie_muertos;
    else
    if (tm.tm_wday == 0 || tm.tm_wday == 6)
        p = susie_cool;

    return p;
}


int activitypub_request(snac *user, const char *url, xs_dict **data)
/* request an object */
{
    int status = 0;
    xs *response = NULL;
    xs *payload = NULL;
    int p_size;
    char *ctype;

    *data = NULL;

    if (user != NULL) {
        /* get from the net */
        response = http_signed_request(user, "GET", url,
            NULL, NULL, 0, &status, &payload, &p_size, 0);
    }

    if (status == 0 || (status >= 500 && status <= 599)) {
        /* I found an instance running Misskey that returned
           500 on signed messages but returned the object
           perfectly without signing (?), so why not try */
        xs_free(response);

        xs *hdrs = xs_dict_new();
        hdrs = xs_dict_append(hdrs, "accept",     "application/activity+json");
        hdrs = xs_dict_append(hdrs, "user-agent", USER_AGENT);

        response = xs_http_request("GET", url, hdrs,
            NULL, 0, &status, &payload, &p_size, 0);
    }

    if (valid_status(status)) {
        /* ensure it's ActivityPub data */
        ctype = xs_dict_get(response, "content-type");

        if (xs_is_null(ctype))
            status = 400;
        else
        if (xs_str_in(ctype, "application/activity+json") != -1 ||
            xs_str_in(ctype, "application/ld+json") != -1) {

            /* if there is no payload, fail */
            if (xs_is_null(payload))
                status = 400;
            else
                *data = xs_json_loads(payload);
        }
        else
            status = 500;
    }

    return status;
}


int actor_request(const char *actor, xs_dict **data)
/* request an actor */
{
    int status;
    xs *payload = NULL;

    if (data)
        *data = NULL;

    /* get from disk first */
    status = actor_get(actor, data);

    if (status != 200) {
        /* actor data non-existent or stale: get from the net */
        status = activitypub_request(NULL, actor, &payload);

        if (valid_status(status)) {
            /* renew data */
            status = actor_add(actor, payload);

            if (data != NULL) {
                *data   = payload;
                payload = NULL;
            }
        }
        else
            srv_debug(0, xs_fmt("actor_request error %s %d", actor, status));
    }

    /* collect the (presumed) shared inbox in this actor */
    if (xs_type(xs_dict_get(srv_config, "disable_inbox_collection")) != XSTYPE_TRUE) {
        if (valid_status(status) && data && *data)
            inbox_add_by_actor(*data);
    }
    else
        srv_debug(2, xs_fmt("NOT collected"));

    return status;
}


int timeline_request(snac *snac, char **id, xs_str **wrk, int level)
/* ensures that an entry and its ancestors are in the timeline */
{
    int status = 0;

    if (level < 256 && !xs_is_null(*id)) {
        xs *msg = NULL;

        /* is the object already there? */
        if (!valid_status(object_get(*id, &msg))) {
            /* no; download it */
            status = activitypub_request(snac, *id, &msg);

            if (valid_status(status)) {
                xs_dict *object  = msg;
                const char *type = xs_dict_get(object, "type");

                /* get the id again from the object, as it may be different */
                const char *nid = xs_dict_get(object, "id");

                if (xs_type(nid) != XSTYPE_STRING)
                    return 0;

                if (wrk && strcmp(nid, *id) != 0) {
                    snac_debug(snac, 1,
                        xs_fmt("timeline_request canonical id for %s is %s", *id, nid));

                    *wrk = xs_dup(nid);
                    *id  = *wrk;
                }

                if (xs_is_null(type))
                    type = "(null)";

                srv_debug(1, xs_fmt("timeline_request type %s '%s'", nid, type));

                if (strcmp(type, "Create") == 0) {
                    /* some software like lemmy nest Announce + Create + Note */
                    if (!xs_is_null(object = xs_dict_get(object, "object"))) {
                        type = xs_dict_get(object, "type");
                        nid  = xs_dict_get(object, "id");
                    }
                    else
                        type = "(null)";
                }

                if (xs_match(type, "Note|Page|Article")) {
                    const char *actor = xs_dict_get(object, "attributedTo");

                    /* request (and drop) the actor for this entry */
                    if (!xs_is_null(actor))
                        actor_request(actor, NULL);

                    /* does it have an ancestor? */
                    char *in_reply_to = xs_dict_get(object, "inReplyTo");

                    /* store */
                    timeline_add(snac, nid, object);

                    /* recurse! */
                    timeline_request(snac, &in_reply_to, NULL, level + 1);
                }
            }
        }

        enqueue_request_replies(snac, *id);
    }

    return status;
}


void timeline_request_replies(snac *user, const char *id)
/* requests all replies of a message */
/* FIXME: experimental -- needs more testing */
{
    /* FIXME: TEMPORARILY DISABLED */
    /* Reason: I've found that many of the posts in the 'replies' Collection
       do not have an inReplyTo field (why??? aren't they 'replies'???).
       For this reason, these requested objects are not stored as children
       of the original post and they are shown as out-of-context, top level posts.
       This process is disabled until I find an elegant way of providing a parent
       for these 'stray' children. */
    return;

    xs *msg = NULL;

    if (!valid_status(object_get(id, &msg)))
        return;

    /* does it have a replies collection? */
    const xs_dict *replies = xs_dict_get(msg, "replies");

    if (!xs_is_null(replies)) {
        const char *type  = xs_dict_get(replies, "type");
        const char *first = xs_dict_get(replies, "first");

        if (!xs_is_null(type) && !xs_is_null(first) && strcmp(type, "Collection") == 0) {
            const char *next = xs_dict_get(first, "next");

            if (!xs_is_null(next)) {
                xs *rpls = NULL;
                int status = activitypub_request(user, next, &rpls);

                /* request the Collection of replies */
                if (valid_status(status)) {
                    xs_list *items = xs_dict_get(rpls, "items");

                    if (xs_type(items) == XSTYPE_LIST) {
                        xs_val *v;

                        /* request them all */
                        while (xs_list_iter(&items, &v)) {
                            if (xs_type(v) == XSTYPE_DICT) {
                                /* not an id, but the object itself (!) */
                                const char *c_id = xs_dict_get(v, "id");

                                if (!xs_is_null(id)) {
                                    snac_debug(user, 0, xs_fmt("embedded reply %s", c_id));

                                    object_add(c_id, v);

                                    /* get its own children */
                                    timeline_request_replies(user, v);
                                }
                            }
                            else {
                                snac_debug(user, 0, xs_fmt("request reply %s", v));
                                timeline_request(user, &v, NULL, 0);
                            }
                        }
                    }
                }
                else
                    snac_debug(user, 0, xs_fmt("replies request error %s %d", next, status));
            }
        }
    }
}


int send_to_inbox_raw(const char *keyid, const char *seckey,
                  const xs_str *inbox, const xs_dict *msg,
                  xs_val **payload, int *p_size, int timeout)
/* sends a message to an Inbox */
{
    int status;
    xs_dict *response;
    xs *j_msg = xs_json_dumps((xs_dict *)msg, 4);

    response = http_signed_request_raw(keyid, seckey, "POST", inbox,
        NULL, j_msg, strlen(j_msg), &status, payload, p_size, timeout);

    xs_free(response);

    return status;
}


int send_to_inbox(snac *snac, const xs_str *inbox, const xs_dict *msg,
                  xs_val **payload, int *p_size, int timeout)
/* sends a message to an Inbox */
{
    char *seckey = xs_dict_get(snac->key, "secret");

    return send_to_inbox_raw(snac->actor, seckey, inbox, msg, payload, p_size, timeout);
}


xs_str *get_actor_inbox(const char *actor)
/* gets an actor's inbox */
{
    xs *data = NULL;
    char *v = NULL;

    if (valid_status(actor_request(actor, &data))) {
        /* try first endpoints/sharedInbox */
        if ((v = xs_dict_get(data, "endpoints")))
            v = xs_dict_get(v, "sharedInbox");

        /* try then the regular inbox */
        if (xs_is_null(v))
            v = xs_dict_get(data, "inbox");
    }

    return xs_is_null(v) ? NULL : xs_dup(v);
}


int send_to_actor(snac *snac, const char *actor, const xs_dict *msg,
                  xs_val **payload, int *p_size, int timeout)
/* sends a message to an actor */
{
    int status = 400;
    xs *inbox = get_actor_inbox(actor);

    if (!xs_is_null(inbox))
        status = send_to_inbox(snac, inbox, msg, payload, p_size, timeout);

    return status;
}


void post_message(snac *snac, const char *actor, const xs_dict *msg)
/* posts a message immediately (bypassing the output queues) */
{
    xs *payload = NULL;
    int p_size;

    int status = send_to_actor(snac, actor, msg, &payload, &p_size, 3);

    srv_log(xs_fmt("post_message to actor %s %d", actor, status));

    if (!valid_status(status))
        /* cannot send right now, enqueue */
        enqueue_message(snac, msg);
}


xs_list *recipient_list(snac *snac, const xs_dict *msg, int expand_public)
/* returns the list of recipients for a message */
{
    char *to = xs_dict_get(msg, "to");
    char *cc = xs_dict_get(msg, "cc");
    xs_set rcpts;
    int n;

    xs_set_init(&rcpts);

    char *lists[] = { to, cc, NULL };
    for (n = 0; lists[n]; n++) {
        char *l = lists[n];
        char *v;
        xs *tl = NULL;

        /* if it's a string, create a list with only one element */
        if (xs_type(l) == XSTYPE_STRING) {
            tl = xs_list_new();
            tl = xs_list_append(tl, l);

            l = tl;
        }

        while (xs_list_iter(&l, &v)) {
            if (expand_public && strcmp(v, public_address) == 0) {
                /* iterate the followers and add them */
                xs *fwers = follower_list(snac);
                char *actor;

                char *p = fwers;
                while (xs_list_iter(&p, &actor))
                    xs_set_add(&rcpts, actor);
            }
            else
                xs_set_add(&rcpts, v);
        }
    }

    return xs_set_result(&rcpts);
}


int is_msg_public(const xs_dict *msg)
/* checks if a message is public */
{
    const char *to = xs_dict_get(msg, "to");
    const char *cc = xs_dict_get(msg, "cc");
    int n;

    const char *lists[] = { to, cc, NULL };
    for (n = 0; lists[n]; n++) {
        const xs_val *l = lists[n];

        if (xs_type(l) == XSTYPE_STRING) {
            if (strcmp(l, public_address) == 0)
                return 1;
        }
        else
        if (xs_type(l) == XSTYPE_LIST) {
            if (xs_list_in(l, public_address) != -1)
                return 1;
        }
    }

    return 0;
}


int is_msg_for_me(snac *snac, const xs_dict *c_msg)
/* checks if this message is for me */
{
    const char *type = xs_dict_get(c_msg, "type");

    if (xs_match(type, "Like|Announce")) {
        const char *object = xs_dict_get(c_msg, "object");

        if (xs_type(object) == XSTYPE_DICT)
            object = xs_dict_get(object, "id");

        /* bad object id? reject */
        if (xs_type(object) != XSTYPE_STRING)
            return 0;

        /* if it's about one of our posts, accept it */
        if (xs_startswith(object, snac->actor))
            return 2;

        /* if it's by someone we don't follow, reject */
        if (!following_check(snac, xs_dict_get(c_msg, "actor")))
            return 0;
    }

    /* if it's not a Create or Update, allow */
    if (!xs_match(type, "Create|Update"))
        return 1;

    xs_dict *msg = xs_dict_get(c_msg, "object");
    xs *rcpts = recipient_list(snac, msg, 0);
    xs_list *p = rcpts;
    xs_str *v;

    while(xs_list_iter(&p, &v)) {
        /* explicitly for me? accept */
        if (strcmp(v, snac->actor) == 0)
            return 2;

        /* for someone we follow? (probably cc'ed) accept */
        if (following_check(snac, v))
            return 5;
    }

    /* accept if it's by someone we follow */
    char *atto = xs_dict_get(msg, "attributedTo");

    if (!xs_is_null(atto) && following_check(snac, atto))
        return 3;

    /* is this message a reply to another? */
    char *irt = xs_dict_get(msg, "inReplyTo");
    if (!xs_is_null(irt)) {
        xs *r_msg = NULL;

        /* try to get the replied message */
        if (valid_status(object_get(irt, &r_msg))) {
            atto = xs_dict_get(r_msg, "attributedTo");

            /* accept if the replied message is from someone we follow */
            if (!xs_is_null(atto) && following_check(snac, atto))
                return 4;
        }
    }

    return 0;
}


xs_str *process_tags(snac *snac, const char *content, xs_list **tag)
/* parses mentions and tags from content */
{
    xs_str *nc  = xs_str_new(NULL);
    xs_list *tl = *tag;
    xs *split;
    xs_list *p;
    xs_val *v;
    int n = 0;

    /* create a default server for incomplete mentions */
    xs *def_srv = NULL;

    if (xs_list_len(tl)) {
        /* if there are any mentions, get the server from
           the first one, which is the inReplyTo author */
        p = tl;
        while (xs_list_iter(&p, &v)) {
            const char *type = xs_dict_get(v, "type");
            const char *name = xs_dict_get(v, "name");

            if (type && name && strcmp(type, "Mention") == 0) {
                xs *l = xs_split(name, "@");

                def_srv = xs_dup(xs_list_get(l, -1));

                break;
            }
        }
    }

    if (xs_is_null(def_srv))
        /* use this same server */
        def_srv = xs_dup(xs_dict_get(srv_config, "host"));

    split = xs_regex_split(content, "(@[A-Za-z0-9_]+(@[A-Za-z0-9\\.-]+)?|&#[0-9]+;|#[^ ,\\.:;<]+)");

    p = split;
    while (xs_list_iter(&p, &v)) {
        if ((n & 0x1)) {
            if (*v == '@') {
                xs *link = NULL;
                xs *wuid = NULL;

                if (strchr(v + 1, '@') == NULL) {
                    /* only one @? it's a dumb Mastodon-like mention
                       without server; add the default one */
                    wuid = xs_fmt("%s@%s", v, def_srv);

                    snac_debug(snac, 2, xs_fmt("mention without server '%s' '%s'", v, wuid));
                }
                else
                    wuid = xs_dup(v);

                /* query the webfinger about this fellow */
                xs *actor = NULL;
                xs *uid   = NULL;
                int status;

                status = webfinger_request(wuid, &actor, &uid);

                if (valid_status(status)) {
                    xs *d = xs_dict_new();
                    xs *n = xs_fmt("@%s", uid);

                    d = xs_dict_append(d, "type",   "Mention");
                    d = xs_dict_append(d, "href",   actor);
                    d = xs_dict_append(d, "name",   n);

                    tl = xs_list_append(tl, d);

                    link = xs_fmt("<a href=\"%s\" class=\"u-url mention\">%s</a>", actor, n);
                }

                if (!xs_is_null(link))
                    nc = xs_str_cat(nc, link);
                else
                    nc = xs_str_cat(nc, v);
            }
            else
            if (*v == '#') {
                /* hashtag */
                xs *d = xs_dict_new();
                xs *n = xs_tolower_i(xs_dup(v));
                xs *h = xs_fmt("%s?t=%s", srv_baseurl, n + 1);
                xs *l = xs_fmt("<a href=\"%s\" class=\"mention hashtag\" rel=\"tag\">%s</a>", h, v);

                d = xs_dict_append(d, "type",   "Hashtag");
                d = xs_dict_append(d, "href",   h);
                d = xs_dict_append(d, "name",   n);

                tl = xs_list_append(tl, d);

                /* add the code */
                nc = xs_str_cat(nc, l);
            }
            else
            if (*v == '&') {
                /* HTML Unicode entity, probably part of an emoji */

                /* write as is */
                nc = xs_str_cat(nc, v);
            }
        }
        else
            nc = xs_str_cat(nc, v);

        n++;
    }

    *tag = tl;

    return nc;
}


void notify(snac *snac, const char *type, const char *utype, const char *actor, const xs_dict *msg)
/* notifies the user of relevant events */
{
    const char *id = xs_dict_get(msg, "id");

    if (strcmp(type, "Create") == 0) {
        /* only notify of notes specifically for us */
        xs *rcpts = recipient_list(snac, msg, 0);

        if (xs_list_in(rcpts, snac->actor) == -1)
            return;

        /* discard votes */
        const xs_dict *note = xs_dict_get(msg, "object");

        if (note && !xs_is_null(xs_dict_get(note, "name")))
            return;
    }

    if (strcmp(type, "Undo") == 0 && strcmp(utype, "Follow") != 0)
        return;

    /* get the object id */
    const char *objid = xs_dict_get(msg, "object");

    if (xs_type(objid) == XSTYPE_DICT)
        objid = xs_dict_get(objid, "id");

    if (strcmp(type, "Like") == 0 || strcmp(type, "Announce") == 0) {
        /* if it's not an admiration about something by us, done */
        if (xs_is_null(objid) || !xs_startswith(objid, snac->actor))
            return;
    }

    /* updated poll? */
    if (strcmp(type, "Update") == 0 && strcmp(utype, "Question") == 0) {
        const xs_dict *poll;
        const char *poll_id;

        if ((poll = xs_dict_get(msg, "object")) == NULL)
            return;

        /* if it's not closed, discard */
        if (xs_is_null(xs_dict_get(poll, "closed")))
            return;

        if ((poll_id = xs_dict_get(poll, "id")) == NULL)
            return;

        /* if it's not ours and we didn't vote, discard */
        if (!xs_startswith(poll_id, snac->actor) && !was_question_voted(snac, poll_id))
            return;
    }

    /* user will love to know about this! */

    /* prepare message body */
    xs *body = xs_fmt("User  : @%s@%s\n",
        xs_dict_get(snac->config, "uid"),
        xs_dict_get(srv_config,   "host")
    );

    if (strcmp(utype, "(null)") != 0) {
        xs *s1 = xs_fmt("Type  : %s + %s\n", type, utype);
        body = xs_str_cat(body, s1);
    }
    else {
        xs *s1 = xs_fmt("Type  : %s\n", type);
        body = xs_str_cat(body, s1);
    }

    {
        xs *s1 = xs_fmt("Actor : %s\n", actor);
        body = xs_str_cat(body, s1);
    }

    if (objid != NULL) {
        xs *s1 = xs_fmt("Object: %s\n", objid);
        body = xs_str_cat(body, s1);
    }

    /* email */

    const char *email = "[disabled by admin]";

    if (xs_type(xs_dict_get(srv_config, "disable_email_notifications")) != XSTYPE_TRUE) {
        email = xs_dict_get(snac->config_o, "email");
        if (xs_is_null(email)) {
            email = xs_dict_get(snac->config, "email");

            if (xs_is_null(email))
                email = "[empty]";
        }
    }

    if (*email != '\0' && *email != '[') {
        snac_debug(snac, 1, xs_fmt("email notify %s %s %s", type, utype, actor));

        xs *subject = xs_fmt("snac notify for @%s@%s",
                    xs_dict_get(snac->config, "uid"), xs_dict_get(srv_config, "host"));
        xs *from    = xs_fmt("snac-daemon <snac-daemon@%s>", xs_dict_get(srv_config, "host"));
        xs *header  = xs_fmt(
                    "From: %s\n"
                    "To: %s\n"
                    "Subject: %s\n"
                    "\n",
                    from, email, subject);

        xs *email_body = xs_fmt("%s%s", header, body);

        enqueue_email(email_body, 0);
    }

    /* telegram */

    char *bot     = xs_dict_get(snac->config, "telegram_bot");
    char *chat_id = xs_dict_get(snac->config, "telegram_chat_id");

    if (!xs_is_null(bot) && !xs_is_null(chat_id) && *bot && *chat_id)
        enqueue_telegram(body, bot, chat_id);

    /* finally, store it in the notification folder */
    if (strcmp(type, "Follow") == 0)
        objid = id;
    else
    if (strcmp(utype, "Follow") == 0)
        objid = actor;

    notify_add(snac, type, utype, actor, objid != NULL ? objid : id);
}


/** messages **/

xs_dict *msg_base(snac *snac, const char *type, const char *id,
                  const char *actor, const char *date, const char *object)
/* creates a base ActivityPub message */
{
    xs *did       = NULL;
    xs *published = NULL;
    xs *ntid      = tid(0);
    const char *obj_id;

    if (xs_type(object) == XSTYPE_DICT)
        obj_id = xs_dict_get(object, "id");
    else
        obj_id = object;

    /* generated values */
    if (date && strcmp(date, "@now") == 0) {
        published = xs_str_utctime(0, ISO_DATE_SPEC);
        date = published;
    }

    if (id != NULL) {
        if (strcmp(id, "@dummy") == 0) {
            did = xs_fmt("%s/d/%s/%s", snac->actor, ntid, type);

            id = did;
        }
        else
        if (strcmp(id, "@object") == 0) {
            if (obj_id != NULL) {
                did = xs_fmt("%s/%s_%s", obj_id, type, ntid);
                id = did;
            }
            else
                id = NULL;
        }
        else
        if (strcmp(id, "@wrapper") == 0) {
            /* like @object, but always generate the same id */
            if (object != NULL) {
                date = xs_dict_get(object, "published");
                did = xs_fmt("%s/%s", obj_id, type);
                id = did;
            }
            else
                id = NULL;
        }
    }

    xs_dict *msg = xs_dict_new();

    msg = xs_dict_append(msg, "@context", "https:/" "/www.w3.org/ns/activitystreams");
    msg = xs_dict_append(msg, "type",     type);

    if (id != NULL)
        msg = xs_dict_append(msg, "id", id);

    if (actor != NULL)
        msg = xs_dict_append(msg, "actor", actor);

    if (date != NULL)
        msg = xs_dict_append(msg, "published", date);

    if (object != NULL)
        msg = xs_dict_append(msg, "object", object);

    return msg;
}


xs_dict *msg_collection(snac *snac, char *id)
/* creates an empty OrderedCollection message */
{
    xs_dict *msg = msg_base(snac, "OrderedCollection", id, NULL, NULL, NULL);
    xs *ol = xs_list_new();

    msg = xs_dict_append(msg, "attributedTo", snac->actor);
    msg = xs_dict_append(msg, "orderedItems", ol);
    msg = xs_dict_append(msg, "totalItems",   xs_stock_0);

    return msg;
}


xs_dict *msg_accept(snac *snac, char *object, char *to)
/* creates an Accept message (as a response to a Follow) */
{
    xs_dict *msg = msg_base(snac, "Accept", "@dummy", snac->actor, NULL, object);

    msg = xs_dict_append(msg, "to", to);

    return msg;
}


xs_dict *msg_update(snac *snac, xs_dict *object)
/* creates an Update message */
{
    xs_dict *msg = msg_base(snac, "Update", "@object", snac->actor, "@now", object);

    char *type = xs_dict_get(object, "type");

    if (strcmp(type, "Note") == 0) {
        msg = xs_dict_append(msg, "to", xs_dict_get(object, "to"));
        msg = xs_dict_append(msg, "cc", xs_dict_get(object, "cc"));
    }
    else
    if (strcmp(type, "Person") == 0) {
        msg = xs_dict_append(msg, "to", public_address);

        /* also spam the people being followed, so that
           they have the newest information about who we are */
        xs *cc = following_list(snac);

        msg = xs_dict_append(msg, "cc", cc);
    }
    else
        msg = xs_dict_append(msg, "to", public_address);

    return msg;
}


xs_dict *msg_admiration(snac *snac, char *object, char *type)
/* creates a Like or Announce message */
{
    xs *a_msg    = NULL;
    xs_dict *msg = NULL;
    xs *wrk      = NULL;

    /* call the object */
    timeline_request(snac, &object, &wrk, 0);

    if (valid_status(object_get(object, &a_msg))) {
        xs *rcpts = xs_list_new();

        msg = msg_base(snac, type, "@dummy", snac->actor, "@now", object);

        if (is_msg_public(a_msg))
            rcpts = xs_list_append(rcpts, public_address);

        rcpts = xs_list_append(rcpts, xs_dict_get(a_msg, "attributedTo"));

        msg = xs_dict_append(msg, "to", rcpts);
    }
    else
        snac_log(snac, xs_fmt("msg_admiration cannot retrieve object %s", object));

    return msg;
}


xs_dict *msg_actor(snac *snac)
/* create a Person message for this actor */
{
    xs *ctxt     = xs_list_new();
    xs *icon     = xs_dict_new();
    xs *keys     = xs_dict_new();
    xs *tags     = xs_list_new();
    xs *avtr     = NULL;
    xs *kid      = NULL;
    xs *f_bio    = NULL;
    xs_dict *msg = msg_base(snac, "Person", snac->actor, NULL, NULL, NULL);
    char *p;
    int n;

    /* change the @context (is this really necessary?) */
    ctxt = xs_list_append(ctxt, "https:/" "/www.w3.org/ns/activitystreams");
    ctxt = xs_list_append(ctxt, "https:/" "/w3id.org/security/v1");
    msg = xs_dict_set(msg, "@context",          ctxt);

    msg = xs_dict_set(msg, "url",               snac->actor);
    msg = xs_dict_set(msg, "name",              xs_dict_get(snac->config, "name"));
    msg = xs_dict_set(msg, "preferredUsername", snac->uid);
    msg = xs_dict_set(msg, "published",         xs_dict_get(snac->config, "published"));

    xs *f_bio_2 = not_really_markdown(xs_dict_get(snac->config, "bio"), NULL);
    f_bio = process_tags(snac, f_bio_2, &tags);
    msg = xs_dict_set(msg, "summary", f_bio);
    msg = xs_dict_set(msg, "tag", tags);

    char *folders[] = { "inbox", "outbox", "followers", "following", NULL };
    for (n = 0; folders[n]; n++) {
        xs *f = xs_fmt("%s/%s", snac->actor, folders[n]);
        msg = xs_dict_set(msg, folders[n], f);
    }

    p = xs_dict_get(snac->config, "avatar");

    if (*p == '\0')
        avtr = xs_fmt("%s/susie.png", srv_baseurl);
    else
        avtr = xs_dup(p);

    icon = xs_dict_append(icon, "type",         "Image");
    icon = xs_dict_append(icon, "mediaType",    xs_mime_by_ext(avtr));
    icon = xs_dict_append(icon, "url",          avtr);
    msg = xs_dict_set(msg, "icon", icon);

    kid = xs_fmt("%s#main-key", snac->actor);

    keys = xs_dict_append(keys, "id",           kid);
    keys = xs_dict_append(keys, "owner",        snac->actor);
    keys = xs_dict_append(keys, "publicKeyPem", xs_dict_get(snac->key, "public"));
    msg = xs_dict_set(msg, "publicKey", keys);

    /* if the "bot" config field is set to true, change type to "Service" */
    if (xs_type(xs_dict_get(snac->config, "bot")) == XSTYPE_TRUE)
        msg = xs_dict_set(msg, "type", "Service");

    /* add the header image, if there is one defined */
    const char *header = xs_dict_get(snac->config, "header");
    if (!xs_is_null(header)) {
        xs *d = xs_dict_new();
        d = xs_dict_append(d, "type",       "Image");
        d = xs_dict_append(d, "mediaType",  xs_mime_by_ext(header));
        d = xs_dict_append(d, "url",        header);
        msg = xs_dict_set(msg, "image", d);
    }

    /* add the metadata as attachments of PropertyValue */
    xs_dict *metadata = xs_dict_get(snac->config, "metadata");
    if (xs_type(metadata) == XSTYPE_DICT) {
        xs *attach = xs_list_new();
        xs_str *k;
        xs_str *v;

        while (xs_dict_iter(&metadata, &k, &v)) {
            xs *d = xs_dict_new();

            xs *k2 = encode_html(k);
            xs *v2 = NULL;

            if (xs_startswith(v, "https:")) {
                xs *t = encode_html(v);
                v2 = xs_fmt("<a href=\"%s\">%s</a>", t, t);
            }
            else
                v2 = encode_html(v);

            d = xs_dict_append(d, "type",  "PropertyValue");
            d = xs_dict_append(d, "name",  k2);
            d = xs_dict_append(d, "value", v2);

            attach = xs_list_append(attach, d);
        }

        msg = xs_dict_set(msg, "attachment", attach);
    }

    /* use shared inboxes? */
    if (xs_type(xs_dict_get(srv_config, "shared_inboxes")) == XSTYPE_TRUE) {
        xs *d = xs_dict_new();
        xs *si = xs_fmt("%s/shared-inbox", srv_baseurl);
        d = xs_dict_append(d, "sharedInbox", si);
        msg = xs_dict_set(msg, "endpoints", d);
    }

    return msg;
}


xs_dict *msg_create(snac *snac, const xs_dict *object)
/* creates a 'Create' message */
{
    xs_dict *msg = msg_base(snac, "Create", "@wrapper", snac->actor, NULL, object);
    xs_val *v;

    if ((v = xs_dict_get(object, "attributedTo")))
        msg = xs_dict_append(msg, "attributedTo", v);

    if ((v = xs_dict_get(object, "cc")))
        msg = xs_dict_append(msg, "cc", v);

    if ((v = xs_dict_get(object, "to")))
        msg = xs_dict_append(msg, "to", v);
    else
        msg = xs_dict_append(msg, "to", public_address);

    return msg;
}


xs_dict *msg_undo(snac *snac, char *object)
/* creates an 'Undo' message */
{
    xs_dict *msg = msg_base(snac, "Undo", "@object", snac->actor, "@now", object);
    const char *to;

    if (xs_type(object) == XSTYPE_DICT && (to = xs_dict_get(object, "object")))
        msg = xs_dict_append(msg, "to", to);

    return msg;
}


xs_dict *msg_delete(snac *snac, char *id)
/* creates a 'Delete' + 'Tombstone' for a local entry */
{
    xs *tomb = xs_dict_new();
    xs_dict *msg = NULL;

    /* sculpt the tombstone */
    tomb = xs_dict_append(tomb, "type", "Tombstone");
    tomb = xs_dict_append(tomb, "id",   id);

    /* now create the Delete */
    msg = msg_base(snac, "Delete", "@object", snac->actor, "@now", tomb);

    msg = xs_dict_append(msg, "to", public_address);

    return msg;
}


xs_dict *msg_follow(snac *snac, const char *q)
/* creates a 'Follow' message */
{
    xs *actor_o = NULL;
    xs *actor   = NULL;
    xs_dict *msg = NULL;
    int status;

    xs *url_or_uid = xs_strip_i(xs_str_new(q));

    if (xs_startswith(url_or_uid, "https:/"))
        actor = xs_dup(url_or_uid);
    else
    if (!valid_status(webfinger_request(url_or_uid, &actor, NULL)) || actor == NULL) {
        snac_log(snac, xs_fmt("cannot resolve user %s to follow", url_or_uid));
        return NULL;
    }

    /* request the actor */
    status = actor_request(actor, &actor_o);

    if (valid_status(status)) {
        /* check if the actor is an alias */
        char *r_actor = xs_dict_get(actor_o, "id");

        if (r_actor && strcmp(actor, r_actor) != 0) {
            snac_log(snac, xs_fmt("actor to follow is an alias %s -> %s", actor, r_actor));
        }

        msg = msg_base(snac, "Follow", "@dummy", snac->actor, NULL, r_actor);
    }
    else
        snac_log(snac, xs_fmt("cannot get actor to follow %s %d", actor, status));

    return msg;
}


xs_dict *msg_note(snac *snac, const xs_str *content, const xs_val *rcpts,
                  xs_str *in_reply_to, xs_list *attach, int priv)
/* creates a 'Note' message */
{
    xs *ntid = tid(0);
    xs *id   = xs_fmt("%s/p/%s", snac->actor, ntid);
    xs *ctxt = NULL;
    xs *fc2  = NULL;
    xs *fc1  = NULL;
    xs *to   = NULL;
    xs *cc   = xs_list_new();
    xs *irt  = NULL;
    xs *tag  = xs_list_new();
    xs *atls = xs_list_new();
    xs_dict *msg = msg_base(snac, "Note", id, NULL, "@now", NULL);
    xs_list *p;
    xs_val *v;

    if (rcpts == NULL)
        to = xs_list_new();
    else {
        if (xs_type(rcpts) == XSTYPE_STRING) {
            to = xs_list_new();
            to = xs_list_append(to, rcpts);
        }
        else
            to = xs_dup(rcpts);
    }

    /* format the content */
    fc2 = not_really_markdown(content, &atls);

    if (in_reply_to != NULL && *in_reply_to) {
        xs *p_msg = NULL;
        xs *wrk   = NULL;

        /* demand this thing */
        timeline_request(snac, &in_reply_to, &wrk, 0);

        if (valid_status(object_get(in_reply_to, &p_msg))) {
            /* add this author as recipient */
            char *a, *v;

            if ((a = xs_dict_get(p_msg, "attributedTo")) && xs_list_in(to, a) == -1)
                to = xs_list_append(to, a);

            /* add this author to the tag list as a mention */
            xs *t_href = NULL;
            xs *t_name = NULL;

            if (!xs_is_null(a) && valid_status(webfinger_request(a, &t_href, &t_name))) {
                xs *t = xs_dict_new();

                t = xs_dict_append(t, "type", "Mention");
                t = xs_dict_append(t, "href", t_href);
                t = xs_dict_append(t, "name", t_name);

                tag = xs_list_append(tag, t);
            }

            /* get the context, if there is one */
            if ((v = xs_dict_get(p_msg, "context")))
                ctxt = xs_dup(v);

            /* propagate the conversation field, if there is one */
            if ((v = xs_dict_get(p_msg, "conversation")))
                msg = xs_dict_append(msg, "conversation", v);

            /* if this message is public, ours will also be */
            if (!priv && is_msg_public(p_msg) && xs_list_in(to, public_address) == -1)
                to = xs_list_append(to, public_address);
        }

        irt = xs_dup(in_reply_to);
    }
    else
        irt = xs_val_new(XSTYPE_NULL);

    /* extract the mentions and hashtags and convert the content */
    fc1 = process_tags(snac, fc2, &tag);

    /* create the attachment list, if there are any */
    if (!xs_is_null(attach)) {
        while (xs_list_iter(&attach, &v)) {
            xs *d            = xs_dict_new();
            const char *url  = xs_list_get(v, 0);
            const char *alt  = xs_list_get(v, 1);
            const char *mime = xs_mime_by_ext(url);

            d = xs_dict_append(d, "mediaType", mime);
            d = xs_dict_append(d, "url",       url);
            d = xs_dict_append(d, "name",      alt);
            d = xs_dict_append(d, "type",
                xs_startswith(mime, "image/") ? "Image" : "Document");

            atls = xs_list_append(atls, d);
        }
    }

    if (ctxt == NULL)
        ctxt = xs_fmt("%s#ctxt", id);

    /* add all mentions to the cc */
    p = tag;
    while (xs_list_iter(&p, &v)) {
        if (xs_type(v) == XSTYPE_DICT) {
            char *t;

            if ((t = xs_dict_get(v, "type")) != NULL && strcmp(t, "Mention") == 0) {
                if ((t = xs_dict_get(v, "href")) != NULL)
                    cc = xs_list_append(cc, t);
            }
        }
    }

    /* no recipients? must be for everybody */
    if (!priv && xs_list_len(to) == 0)
        to = xs_list_append(to, public_address);

    /* delete all cc recipients that also are in the to */
    p = to;
    while (xs_list_iter(&p, &v)) {
        int i;

        if ((i = xs_list_in(cc, v)) != -1)
            cc = xs_list_del(cc, i);
    }

    msg = xs_dict_append(msg, "attributedTo", snac->actor);
    msg = xs_dict_append(msg, "summary",      "");
    msg = xs_dict_append(msg, "content",      fc1);
    msg = xs_dict_append(msg, "context",      ctxt);
    msg = xs_dict_append(msg, "url",          id);
    msg = xs_dict_append(msg, "to",           to);
    msg = xs_dict_append(msg, "cc",           cc);
    msg = xs_dict_append(msg, "inReplyTo",    irt);
    msg = xs_dict_append(msg, "tag",          tag);

    msg = xs_dict_append(msg, "sourceContent", content);

    if (xs_list_len(atls))
        msg = xs_dict_append(msg, "attachment", atls);

    return msg;
}


xs_dict *msg_ping(snac *user, const char *rcpt)
/* creates a Ping message (https://humungus.tedunangst.com/r/honk/v/tip/f/docs/ping.txt) */
{
    xs_dict *msg = msg_base(user, "Ping", "@dummy", user->actor, NULL, NULL);

    msg = xs_dict_append(msg, "to", rcpt);

    return msg;
}


xs_dict *msg_pong(snac *user, const char *rcpt, const char *object)
/* creates a Pong message (https://humungus.tedunangst.com/r/honk/v/tip/f/docs/ping.txt) */
{
    xs_dict *msg = msg_base(user, "Pong", "@dummy", user->actor, NULL, object);

    msg = xs_dict_append(msg, "to", rcpt);

    return msg;
}


xs_dict *msg_question(snac *user, const char *content, xs_list *attach,
                      const xs_list *opts, int multiple, int end_secs)
/* creates a Question message */
{
    xs_dict *msg = msg_note(user, content, NULL, NULL, attach, 0);
    int max      = 8;
    xs_set seen;

    msg = xs_dict_set(msg, "type", "Question");

    /* make it non-editable */
    msg = xs_dict_del(msg, "sourceContent");

    xs *o = xs_list_new();
    xs_list *p = (xs_list *)opts;
    xs_str *v;
    xs *replies = xs_json_loads("{\"type\":\"Collection\",\"totalItems\":0}");

    xs_set_init(&seen);

    while (max && xs_list_iter(&p, &v)) {
        if (*v) {
            xs *v2 = xs_dup(v);
            xs *d  = xs_dict_new();

            if (strlen(v2) > 60) {
                v2[60] = '\0';
                v2 = xs_str_cat(v2, "...");
            }

            if (xs_set_add(&seen, v2) == 1) {
                d = xs_dict_append(d, "name",    v2);
                d = xs_dict_append(d, "replies", replies);
                o = xs_list_append(o, d);

                max--;
            }
        }
    }

    xs_set_free(&seen);

    msg = xs_dict_append(msg, multiple ? "anyOf" : "oneOf", o);

    /* set the end time */
    time_t t = time(NULL) + end_secs;
    xs *et = xs_str_utctime(t, ISO_DATE_SPEC);

    msg = xs_dict_append(msg, "endTime", et);

    return msg;
}


int update_question(snac *user, const char *id)
/* updates the poll counts */
{
    xs *msg   = NULL;
    xs *rcnt  = xs_dict_new();
    xs *lopts = xs_list_new();
    xs_list *opts;
    xs_list *p;
    xs_val *v;

    /* get the object */
    if (!valid_status(object_get(id, &msg)))
        return -1;

    /* closed? do nothing more */
    if (xs_dict_get(msg, "closed"))
        return -2;

    /* get the options */
    if ((opts = xs_dict_get(msg, "oneOf")) == NULL &&
        (opts = xs_dict_get(msg, "anyOf")) == NULL)
        return -3;

    /* fill the initial count */
    p = opts;
    while (xs_list_iter(&p, &v)) {
        const char *name = xs_dict_get(v, "name");
        if (name) {
            lopts = xs_list_append(lopts, name);
            rcnt  = xs_dict_set(rcnt, name, xs_stock_0);
        }
    }

    xs_set s;
    xs_set_init(&s);

    /* iterate now the children (the votes) */
    xs *chld = object_children(id);
    p = chld;
    while (xs_list_iter(&p, &v)) {
        xs *obj = NULL;

        if (!valid_status(object_get_by_md5(v, &obj)))
            continue;

        const char *name = xs_dict_get(obj, "name");
        const char *atto = xs_dict_get(obj, "attributedTo");

        if (name && atto) {
            /* get the current count */
            const xs_number *cnt = xs_dict_get(rcnt, name);

            if (xs_type(cnt) == XSTYPE_NUMBER) {
                /* if it exists, increment */
                xs *ucnt = xs_number_new(xs_number_get(cnt) + 1);
                rcnt = xs_dict_set(rcnt, name, ucnt);

                xs_set_add(&s, atto);
            }
        }
    }

    xs *rcpts = xs_set_result(&s);

    /* create a new list of options with their new counts */
    xs *nopts = xs_list_new();
    p = lopts;
    while (xs_list_iter(&p, &v)) {
        const xs_number *cnt = xs_dict_get(rcnt, v);

        if (xs_type(cnt) == XSTYPE_NUMBER) {
            xs *d1 = xs_dict_new();
            xs *d2 = xs_dict_new();

            d2 = xs_dict_append(d2, "type",       "Collection");
            d2 = xs_dict_append(d2, "totalItems", cnt);

            d1 = xs_dict_append(d1, "type",    "Note");
            d1 = xs_dict_append(d1, "name",    v);
            d1 = xs_dict_append(d1, "replies", d2);

            nopts = xs_list_append(nopts, d1);
        }
    }

    /* update the list */
    msg = xs_dict_set(msg, xs_dict_get(msg, "oneOf") != NULL ? "oneOf" : "anyOf", nopts);

    /* due date? */
    int closed = 0;
    const char *end_time = xs_dict_get(msg, "endTime");
    if (!xs_is_null(end_time)) {
        xs *now = xs_str_utctime(0, ISO_DATE_SPEC);

        /* is now greater than the endTime? */
        if (strcmp(now, end_time) >= 0) {
            xs *et    = xs_dup(end_time);
            msg       = xs_dict_set(msg, "closed", et);

            closed = 1;
        }
    }

    /* update the count of voters */
    xs *vcnt = xs_number_new(xs_list_len(rcpts));
    msg = xs_dict_set(msg, "votersCount", vcnt);
    msg = xs_dict_set(msg, "cc", rcpts);

    /* store */
    object_add_ow(id, msg);

    snac_debug(user, 1, xs_fmt("recounted poll %s", id));
    timeline_touch(user);

    /* send an update message to all voters */
    xs *u_msg = msg_update(user, msg);
    u_msg = xs_dict_set(u_msg, "cc", rcpts);

    enqueue_message(user, u_msg);

    if (closed) {
        xs *c_msg = msg_update(user, msg);
        notify(user, "Update", "Question", user->actor, c_msg);
    }

    return 0;
}


/** queues **/

int process_input_message(snac *snac, xs_dict *msg, xs_dict *req)
/* processes an ActivityPub message from the input queue */
/* return values: -1, fatal error; 0, transient error, retry;
   1, processed and done; 2, propagate to users (only when no user is set) */
{
    char *actor = xs_dict_get(msg, "actor");
    char *type  = xs_dict_get(msg, "type");
    xs *actor_o = NULL;
    int a_status;
    int do_notify = 0;

    if (xs_is_null(actor) || *actor == '\0') {
        srv_debug(0, xs_fmt("malformed message (bad actor)"));
        return -1;
    }

    /* question votes may not have a type */
    if (xs_is_null(type))
        type = "Note";

    /* reject uninteresting messages right now */
    if (strcmp(type, "Add") == 0) {
        srv_debug(0, xs_fmt("Ignored message of type '%s'", type));
        return -1;
    }

    char *object, *utype;

    object = xs_dict_get(msg, "object");
    if (object != NULL && xs_type(object) == XSTYPE_DICT)
        utype = xs_dict_get(object, "type");
    else
        utype = "(null)";

    /* special case for Delete messages */
    if (strcmp(type, "Delete") == 0) {
        /* if the actor is not here, do not even try */
        if (!object_here(actor)) {
            srv_debug(0, xs_fmt("dropped 'Delete' message from unknown actor '%s'", actor));
            return -1;
        }
    }

    /* bring the actor */
    a_status = actor_request(actor, &actor_o);

    /* do not retry permanent failures */
    if (a_status == 404 || a_status == 410 || a_status < 0) {
        srv_debug(1, xs_fmt("dropping message due to actor error %s %d", actor, a_status));
        return -1;
    }

    if (!valid_status(a_status)) {
        /* do not retry 'Delete' messages */
        if (strcmp(type, "Delete") == 0) {
            srv_debug(1, xs_fmt("dropping 'Delete' message due to actor error %s %d", actor, a_status));
            return -1;
        }

        /* other actor download errors may need a retry */
        srv_debug(1, xs_fmt("error requesting actor %s %d -- retry later", actor, a_status));
        return 0;
    }

    /* check the signature */
    xs *sig_err = NULL;

    if (!check_signature(req, &sig_err)) {
        srv_log(xs_fmt("bad signature %s (%s)", actor, sig_err));

        srv_archive_error("check_signature", sig_err, req, msg);
        return -1;
    }

    /* if no user is set, no further checks can be done; propagate */
    if (snac == NULL)
        return 2;

    /* reject messages that are not for this user */
    if (!is_msg_for_me(snac, msg)) {
        snac_debug(snac, 1, xs_fmt("message from %s of type '%s' not for us", actor, type));

        return 1;
    }

    /* if it's a DM from someone we don't follow, reject the message */
    if (xs_type(xs_dict_get(snac->config, "drop_dm_from_unknown")) == XSTYPE_TRUE) {
        if (strcmp(utype, "Note") == 0 && !is_msg_public(msg) &&
            !following_check(snac, actor)) {
            snac_log(snac, xs_fmt("DM rejected from unknown actor %s", actor));

            return 1;
        }
    }

    if (strcmp(type, "Follow") == 0) { /** **/
        if (!follower_check(snac, actor)) {
            xs *f_msg = xs_dup(msg);
            xs *reply = msg_accept(snac, f_msg, actor);

            post_message(snac, actor, reply);

            if (xs_is_null(xs_dict_get(f_msg, "published"))) {
                /* add a date if it doesn't include one (Mastodon) */
                xs *date = xs_str_utctime(0, ISO_DATE_SPEC);
                f_msg = xs_dict_set(f_msg, "published", date);
            }

            timeline_add(snac, xs_dict_get(f_msg, "id"), f_msg);

            follower_add(snac, actor);

            snac_log(snac, xs_fmt("new follower %s", actor));
            do_notify = 1;
        }
        else
            snac_log(snac, xs_fmt("repeated 'Follow' from %s", actor));
    }
    else
    if (strcmp(type, "Undo") == 0) { /** **/
        if (strcmp(utype, "Follow") == 0) { /** **/
            if (valid_status(follower_del(snac, actor))) {
                snac_log(snac, xs_fmt("no longer following us %s", actor));
                do_notify = 1;
            }
            else
                snac_log(snac, xs_fmt("error deleting follower %s", actor));
        }
        else
            snac_debug(snac, 1, xs_fmt("ignored 'Undo' for object type '%s'", utype));
    }
    else
    if (strcmp(type, "Create") == 0) { /** **/
        if (is_muted(snac, actor))
            snac_log(snac, xs_fmt("ignored 'Create' + '%s' from muted actor %s", utype, actor));

        if (strcmp(utype, "Note") == 0) { /** **/
            char *id          = xs_dict_get(object, "id");
            char *in_reply_to = xs_dict_get(object, "inReplyTo");
            xs *wrk           = NULL;

            if (!xs_is_null(in_reply_to) && is_hidden(snac, in_reply_to)) {
                snac_debug(snac, 0, xs_fmt("dropped reply %s to hidden post %s", id, in_reply_to));
            }
            else {
                timeline_request(snac, &in_reply_to, &wrk, 0);

                if (timeline_add(snac, id, object)) {
                    snac_log(snac, xs_fmt("new 'Note' %s %s", actor, id));
                    do_notify = 1;
                }

                /* if it has a "name" field, it may be a vote for a question */
                const char *name = xs_dict_get(object, "name");

                if (!xs_is_null(name) && *name && !xs_is_null(in_reply_to) && *in_reply_to)
                    update_question(snac, in_reply_to);
            }
        }
        else
        if (strcmp(utype, "Question") == 0) { /**  **/
            char *id = xs_dict_get(object, "id");

            if (timeline_add(snac, id, object))
                snac_log(snac, xs_fmt("new 'Question' %s %s", actor, id));
        }
        else
            snac_debug(snac, 1, xs_fmt("ignored 'Create' for object type '%s'", utype));
    }
    else
    if (strcmp(type, "Accept") == 0) { /** **/
        if (strcmp(utype, "(null)") == 0) {
            const char *obj_id = xs_dict_get(msg, "object");

            /* if the accepted object id is a string that may
               be created by us, it's a follow */
            if (xs_type(obj_id) == XSTYPE_STRING &&
                xs_startswith(obj_id, srv_baseurl) &&
                xs_endswith(obj_id, "/Follow"))
                utype = "Follow";
        }

        if (strcmp(utype, "Follow") == 0) { /** **/
            if (following_check(snac, actor)) {
                following_add(snac, actor, msg);
                snac_log(snac, xs_fmt("confirmed follow from %s", actor));
            }
            else
                snac_log(snac, xs_fmt("spurious follow accept from %s", actor));
        }
        else
        if (strcmp(utype, "Create") == 0) {
            /* some implementations send Create confirmations, go figure */
            snac_debug(snac, 1, xs_dup("ignored 'Accept' + 'Create'"));
        }
        else {
            srv_archive_error("accept", "ignored Accept", req, msg);
            snac_debug(snac, 1, xs_fmt("ignored 'Accept' for object type '%s'", utype));
        }
    }
    else
    if (strcmp(type, "Like") == 0) { /** **/
        if (xs_type(object) == XSTYPE_DICT)
            object = xs_dict_get(object, "id");

        timeline_admire(snac, object, actor, 1);
        snac_log(snac, xs_fmt("new 'Like' %s %s", actor, object));
        do_notify = 1;
    }
    else
    if (strcmp(type, "Announce") == 0) { /** **/
        if (xs_type(object) == XSTYPE_DICT)
            object = xs_dict_get(object, "id");

        if (is_limited(snac, actor) && !xs_startswith(object, snac->actor))
            snac_log(snac, xs_fmt("dropped 'Announce' from limited actor %s", actor));
        else {
            xs *a_msg = NULL;
            xs *wrk   = NULL;

            timeline_request(snac, &object, &wrk, 0);

            if (valid_status(object_get(object, &a_msg))) {
                const char *who = xs_dict_get(a_msg, "attributedTo");

                if (who && !is_muted(snac, who)) {
                    /* bring the actor */
                    xs *who_o = NULL;

                    if (valid_status(actor_request(who, &who_o))) {
                        timeline_admire(snac, object, actor, 0);
                        snac_log(snac, xs_fmt("new 'Announce' %s %s", actor, object));
                        do_notify = 1;
                    }
                    else
                        snac_log(snac, xs_fmt("dropped 'Announce' on actor request error %s", who));
                }
                else
                    snac_log(snac, xs_fmt("ignored 'Announce' about muted actor %s", who));
            }
            else
                snac_debug(snac, 1, xs_fmt("error requesting 'Announce' object %s", object));
        }
    }
    else
    if (strcmp(type, "Update") == 0) { /** **/
        if (xs_match(utype, "Person|Service")) { /** **/
            actor_add(actor, xs_dict_get(msg, "object"));
            timeline_touch(snac);

            snac_log(snac, xs_fmt("updated actor %s", actor));
        }
        else
        if (xs_match(utype, "Note|Page|Article")) { /** **/
            const char *id = xs_dict_get(object, "id");

            if (object_here(id)) {
                object_add_ow(id, object);
                timeline_touch(snac);

                snac_log(snac, xs_fmt("updated post %s", id));
            }
            else
                snac_log(snac, xs_fmt("dropped update for unknown post %s", id));
        }
        else
        if (strcmp(utype, "Question") == 0) { /** **/
            const char *id     = xs_dict_get(object, "id");
            const char *closed = xs_dict_get(object, "closed");

            object_add_ow(id, object);
            timeline_touch(snac);

            snac_log(snac, xs_fmt("%s poll %s", closed == NULL ? "updated" : "closed", id));

            if (closed != NULL)
                do_notify = 1;
        }
        else
            snac_log(snac, xs_fmt("ignored 'Update' for object type '%s'", utype));
    }
    else
    if (strcmp(type, "Delete") == 0) { /** **/
        if (xs_type(object) == XSTYPE_DICT)
            object = xs_dict_get(object, "id");

        if (valid_status(timeline_del(snac, object)))
            snac_debug(snac, 1, xs_fmt("new 'Delete' %s %s", actor, object));
        else
            snac_debug(snac, 1, xs_fmt("ignored 'Delete' for unknown object %s", object));
    }
    else
    if (strcmp(type, "Pong") == 0) { /** **/
        snac_log(snac, xs_fmt("'Pong' received from %s", actor));
    }
    else
    if (strcmp(type, "Ping") == 0) { /** **/
        snac_log(snac, xs_fmt("'Ping' requested from %s", actor));

        xs *rsp = msg_pong(snac, actor, xs_dict_get(msg, "id"));

        enqueue_output_by_actor(snac, rsp, actor, 0);
    }
    else
        snac_debug(snac, 1, xs_fmt("process_input_message type '%s' ignored", type));

    if (do_notify) {
        notify(snac, type, utype, actor, msg);

        timeline_touch(snac);
    }

    return 1;
}


int send_email(char *msg)
/* invoke sendmail with email headers and body in msg */
{
    FILE *f;
    int status;
    int fds[2];
    pid_t pid;
    if (pipe(fds) == -1) return -1;
    pid = vfork();
    if (pid == -1) return -1;
    else if (pid == 0) {
        dup2(fds[0], 0);
        close(fds[0]);
        close(fds[1]);
        execl("/usr/sbin/sendmail", "sendmail", "-t", (char *) NULL);
        _exit(1);
    }
    close(fds[0]);
    if ((f = fdopen(fds[1], "w")) == NULL) {
        close(fds[1]);
        return -1;
    }
    fprintf(f, "%s\n", msg);
    fclose(f);
    if (waitpid(pid, &status, 0) == -1) return -1;
    return status;
}


void process_user_queue_item(snac *snac, xs_dict *q_item)
/* processes an item from the user queue */
{
    char *type;
    int queue_retry_max = xs_number_get(xs_dict_get(srv_config, "queue_retry_max"));

    if ((type = xs_dict_get(q_item, "type")) == NULL)
        type = "output";

    if (strcmp(type, "message") == 0) {
        xs_dict *msg = xs_dict_get(q_item, "message");
        xs *rcpts    = recipient_list(snac, msg, 1);
        xs_set inboxes;
        xs_list *p;
        xs_str *actor;

        xs_set_init(&inboxes);

        /* iterate the recipients */
        p = rcpts;
        while (xs_list_iter(&p, &actor)) {
            xs *inbox = get_actor_inbox(actor);

            if (inbox != NULL) {
                /* add to the set and, if it's not there, send message */
                if (xs_set_add(&inboxes, inbox) == 1)
                    enqueue_output(snac, msg, inbox, 0, 0);
            }
            else
                snac_log(snac, xs_fmt("cannot find inbox for %s", actor));
        }

        /* if it's public, send to the collected inboxes */
        if (is_msg_public(msg)) {
            xs *shibx = inbox_list();
            xs_str *inbox;

            p = shibx;
            while (xs_list_iter(&p, &inbox)) {
                if (xs_set_add(&inboxes, inbox) == 1)
                    enqueue_output(snac, msg, inbox, 0, 0);
            }
        }

        xs_set_free(&inboxes);
    }
    else
    if (strcmp(type, "input") == 0) {
        /* process the message */
        xs_dict *msg = xs_dict_get(q_item, "message");
        xs_dict *req = xs_dict_get(q_item, "req");
        int retries  = xs_number_get(xs_dict_get(q_item, "retries"));

        if (xs_is_null(msg))
            return;

        if (!process_input_message(snac, msg, req)) {
            if (retries > queue_retry_max)
                snac_log(snac, xs_fmt("input giving up"));
            else {
                /* reenqueue */
                enqueue_input(snac, msg, req, retries + 1);
                snac_log(snac, xs_fmt("input requeue #%d", retries + 1));
            }
        }
    }
    else
    if (strcmp(type, "close_question") == 0) {
        /* the time for this question has ended */
        const char *id = xs_dict_get(q_item, "message");

        if (!xs_is_null(id))
            update_question(snac, id);
    }
    else
    if (strcmp(type, "request_replies") == 0) {
        const char *id = xs_dict_get(q_item, "message");

        if (!xs_is_null(id))
            timeline_request_replies(snac, id);
    }
    else
        snac_log(snac, xs_fmt("unexpected user q_item type '%s'", type));
}


int process_user_queue(snac *snac)
/* processes a user's queue */
{
    int cnt = 0;
    xs *list = user_queue(snac);

    xs_list *p = list;
    xs_str *fn;

    while (xs_list_iter(&p, &fn)) {
        xs *q_item = dequeue(fn);

        if (q_item == NULL) {
            snac_log(snac, xs_fmt("process_user_queue q_item error"));
            continue;
        }

        process_user_queue_item(snac, q_item);
        cnt++;
    }

    return cnt;
}


void process_queue_item(xs_dict *q_item)
/* processes an item from the global queue */
{
    char *type = xs_dict_get(q_item, "type");
    int queue_retry_max = xs_number_get(xs_dict_get(srv_config, "queue_retry_max"));

    if (strcmp(type, "output") == 0) {
        int status;
        xs_str *inbox  = xs_dict_get(q_item, "inbox");
        xs_str *keyid  = xs_dict_get(q_item, "keyid");
        xs_str *seckey = xs_dict_get(q_item, "seckey");
        xs_dict *msg   = xs_dict_get(q_item, "message");
        int retries    = xs_number_get(xs_dict_get(q_item, "retries"));
        int p_status   = xs_number_get(xs_dict_get(q_item, "p_status"));
        xs *payload    = NULL;
        int p_size     = 0;

        if (xs_is_null(inbox) || xs_is_null(msg) || xs_is_null(keyid) || xs_is_null(seckey)) {
            srv_log(xs_fmt("output message error: missing fields"));
            return;
        }

        if (is_instance_blocked(inbox)) {
            srv_debug(0, xs_fmt("discarded output message to blocked instance %s", inbox));
            return;
        }

        /* deliver (if previous error status was a timeout, try now longer) */
        status = send_to_inbox_raw(keyid, seckey, inbox, msg,
                    &payload, &p_size, p_status == 599 ? 20 : 3);

        if (payload) {
            if (p_size > 64) {
                /* trim the message */
                payload[64] = '\0';
                payload = xs_str_cat(payload, "...");
            }

            /* strip ugly control characters */
            payload = xs_replace_i(payload, "\n", "");
            payload = xs_replace_i(payload, "\r", "");

            if (*payload)
                payload = xs_str_wrap_i(" [", payload, "]");
        }
        else
            payload = xs_str_new(NULL);

        srv_log(xs_fmt("output message: sent to inbox %s %d%s", inbox, status, payload));

        if (!valid_status(status)) {
            retries++;

            /* if it's not the first time it fails with a timeout,
               penalize the server by skipping one retry */
            if (p_status == status && status == 499)
                retries++;

            /* error sending; requeue? */
            if (status == 400 || status == 404 || status == 405 || status == 410 || status < 0)
                /* explicit error: discard */
                srv_log(xs_fmt("output message: fatal error %s %d", inbox, status));
            else
            if (retries > queue_retry_max)
                srv_log(xs_fmt("output message: giving up %s %d", inbox, status));
            else {
                /* requeue */
                enqueue_output_raw(keyid, seckey, msg, inbox, retries, status);
                srv_log(xs_fmt("output message: requeue %s #%d", inbox, retries));
            }
        }
    }
    else
    if (strcmp(type, "email") == 0) {
        /* send this email */
        xs_str *msg = xs_dict_get(q_item, "message");
        int retries = xs_number_get(xs_dict_get(q_item, "retries"));

        if (!send_email(msg))
            srv_debug(1, xs_fmt("email message sent"));
        else {
            retries++;

            if (retries > queue_retry_max)
                srv_log(xs_fmt("email giving up (errno: %d)", errno));
            else {
                /* requeue */
                srv_log(xs_fmt(
                    "email requeue #%d (errno: %d)", retries, errno));

                enqueue_email(msg, retries);
            }
        }
    }
    else
    if (strcmp(type, "telegram") == 0) {
        /* send this via telegram */
        char *bot   = xs_dict_get(q_item, "bot");
        char *msg   = xs_dict_get(q_item, "message");
        xs *chat_id = xs_dup(xs_dict_get(q_item, "chat_id"));
        int status  = 0;

        /* chat_id must start with a - */
        if (!xs_startswith(chat_id, "-"))
            chat_id = xs_str_wrap_i("-", chat_id, NULL);

        xs *url  = xs_fmt("https:/" "/api.telegram.org/bot%s/sendMessage", bot);
        xs *body = xs_fmt("{\"chat_id\":%s,\"text\":\"%s\"}", chat_id, msg);

        xs *headers = xs_dict_new();
        headers = xs_dict_append(headers, "content-type", "application/json");

        xs *rsp = xs_http_request("POST", url, headers,
                                   body, strlen(body), &status, NULL, NULL, 0);
        rsp = xs_free(rsp);

        srv_debug(0, xs_fmt("telegram post %d", status));
    }
    else
    if (strcmp(type, "purge") == 0) {
        srv_log(xs_dup("purge start"));

        purge_all();

        srv_log(xs_dup("purge end"));
    }
    else
    if (strcmp(type, "input") == 0) {
        xs_dict *msg = xs_dict_get(q_item, "message");
        xs_dict *req = xs_dict_get(q_item, "req");
        int retries  = xs_number_get(xs_dict_get(q_item, "retries"));

        /* do some instance-level checks */
        int r = process_input_message(NULL, msg, req);

        if (r == 0) {
            /* transient error? retry */
            int queue_retry_max = xs_number_get(xs_dict_get(srv_config, "queue_retry_max"));

            if (retries > queue_retry_max)
                srv_log(xs_fmt("shared input giving up"));
            else {
                /* reenqueue */
                enqueue_shared_input(msg, req, retries + 1);
                srv_log(xs_fmt("shared input requeue #%d", retries + 1));
            }
        }
        else
        if (r == 2) {
            /* redistribute the input message to all users */
            char *ntid = xs_dict_get(q_item, "ntid");
            xs *tmpfn  = xs_fmt("%s/tmp/%s.json", srv_basedir, ntid);
            FILE *f;

            if ((f = fopen(tmpfn, "w")) != NULL) {
                xs_json_dump(q_item, 4, f);
                fclose(f);
            }

            xs *users = user_list();
            xs_list *p = users;
            char *v;
            int cnt = 0;

            while (xs_list_iter(&p, &v)) {
                snac user;

                if (user_open(&user, v)) {
                    if (is_msg_for_me(&user, msg)) {
                        xs *fn = xs_fmt("%s/queue/%s.json", user.basedir, ntid);

                        snac_debug(&user, 1, xs_fmt("enqueue_input (from shared inbox) %s", fn));

                        if (link(tmpfn, fn) < 0)
                            srv_log(xs_fmt("link(%s, %s) error", tmpfn, fn));

                        cnt++;
                    }

                    user_free(&user);
                }
            }

            unlink(tmpfn);

            if (cnt == 0)
                srv_debug(1, xs_fmt("no valid recipients for %s", tmpfn));
        }
    }
    else
        srv_log(xs_fmt("unexpected q_item type '%s'", type));
}


int process_queue(void)
/* processes the global queue */
{
    int cnt = 0;
    xs *list = queue();

    xs_list *p = list;
    xs_str *fn;

    while (xs_list_iter(&p, &fn)) {
        xs *q_item = dequeue(fn);

        if (q_item != NULL) {
            job_post(q_item, 0);
            cnt++;
        }
    }

    return cnt;
}


/** HTTP handlers */

int activitypub_get_handler(const xs_dict *req, const char *q_path,
                            char **body, int *b_size, char **ctype)
{
    int status = 200;
    char *accept = xs_dict_get(req, "accept");
    snac snac;
    xs *msg = NULL;

    if (accept == NULL)
        return 0;

    if (xs_str_in(accept, "application/activity+json") == -1 &&
        xs_str_in(accept, "application/ld+json") == -1)
        return 0;

    xs *l = xs_split_n(q_path, "/", 2);
    char *uid, *p_path;

    uid = xs_list_get(l, 1);
    if (!user_open(&snac, uid)) {
        /* invalid user */
        srv_debug(1, xs_fmt("activitypub_get_handler bad user %s", uid));
        return 404;
    }

    p_path = xs_list_get(l, 2);

    *ctype  = "application/activity+json";

    if (p_path == NULL) {
        /* if there was no component after the user, it's an actor request */
        msg = msg_actor(&snac);
        *ctype = "application/ld+json; profile=\"https://www.w3.org/ns/activitystreams\"";

        char *ua = xs_dict_get(req, "user-agent");

        snac_debug(&snac, 0, xs_fmt("serving actor [%s]", ua ? ua : "No UA"));
    }
    else
    if (strcmp(p_path, "outbox") == 0) {
        xs *id = xs_fmt("%s/outbox", snac.actor);
        xs *elems = timeline_simple_list(&snac, "public", 0, 20);
        xs *list = xs_list_new();
        msg = msg_collection(&snac, id);
        char *p, *v;

        p = elems;
        while (xs_list_iter(&p, &v)) {
            xs *i = NULL;

            if (valid_status(object_get_by_md5(v, &i))) {
                char *type = xs_dict_get(i, "type");
                char *id   = xs_dict_get(i, "id");

                if (type && id && strcmp(type, "Note") == 0 && xs_startswith(id, snac.actor)) {
                    xs *c_msg = msg_create(&snac, i);
                    list = xs_list_append(list, c_msg);
                }
            }
        }

        /* replace the 'orderedItems' with the latest posts */
        xs *items = xs_number_new(xs_list_len(list));
        msg = xs_dict_set(msg, "orderedItems", list);
        msg = xs_dict_set(msg, "totalItems",   items);
    }
    else
    if (strcmp(p_path, "followers") == 0 || strcmp(p_path, "following") == 0) {
        xs *id = xs_fmt("%s/%s", snac.actor, p_path);
        msg = msg_collection(&snac, id);
    }
    else
    if (xs_startswith(p_path, "p/")) {
        xs *id = xs_fmt("%s/%s", snac.actor, p_path);

        status = object_get(id, &msg);

        /* don't return non-public objects */
        if (valid_status(status) && !is_msg_public(msg))
            status = 404;
    }
    else
        status = 404;

    if (status == 200 && msg != NULL) {
        *body   = xs_json_dumps(msg, 4);
        *b_size = strlen(*body);
    }

    snac_debug(&snac, 1, xs_fmt("activitypub_get_handler serving %s %d", q_path, status));

    user_free(&snac);

    return status;
}


int activitypub_post_handler(const xs_dict *req, const char *q_path,
                             char *payload, int p_size,
                             char **body, int *b_size, char **ctype)
/* processes an input message */
{
    (void)b_size;

    int status = 202; /* accepted */
    char *i_ctype = xs_dict_get(req, "content-type");
    snac snac;
    char *v;

    if (i_ctype == NULL) {
        *body  = xs_str_new("no content-type");
        *ctype = "text/plain";
        return 400;
    }

    if (xs_str_in(i_ctype, "application/activity+json") == -1 &&
        xs_str_in(i_ctype, "application/ld+json") == -1)
        return 0;

    /* decode the message */
    xs *msg = xs_json_loads(payload);
    const char *id = xs_dict_get(msg, "id");

    if (msg == NULL) {
        srv_log(xs_fmt("activitypub_post_handler JSON error %s", q_path));

        srv_archive_error("activitypub_post_handler", "JSON error", req, payload);

        *body  = xs_str_new("JSON error");
        *ctype = "text/plain";
        return 400;
    }

    if (id && is_instance_blocked(id)) {
        srv_debug(1, xs_fmt("full instance block for %s", id));

        *body  = xs_str_new("blocked");
        *ctype = "text/plain";
        return 403;
    }

    /* get the user and path */
    xs *l = xs_split_n(q_path, "/", 2);

    if (xs_list_len(l) == 2 && strcmp(xs_list_get(l, 1), "shared-inbox") == 0) {
        enqueue_shared_input(msg, req, 0);
        return 202;
    }

    if (xs_list_len(l) != 3 || strcmp(xs_list_get(l, 2), "inbox") != 0) {
        /* strange q_path */
        srv_debug(1, xs_fmt("activitypub_post_handler unsupported path %s", q_path));
        return 404;
    }

    const char *uid = xs_list_get(l, 1);
    if (!user_open(&snac, uid)) {
        /* invalid user */
        srv_debug(1, xs_fmt("activitypub_post_handler bad user %s", uid));
        return 404;
    }

    /* if it has a digest, check it now, because
       later the payload won't be exactly the same */
    if ((v = xs_dict_get(req, "digest")) != NULL) {
        xs *s1 = xs_sha256_base64(payload, p_size);
        xs *s2 = xs_fmt("SHA-256=%s", s1);

        if (strcmp(s2, v) != 0) {
            srv_log(xs_fmt("digest check FAILED"));

            *body  = xs_str_new("bad digest");
            *ctype = "text/plain";
            status = 400;
        }
    }

    /* if the message is from a muted actor, reject it right now */
    if (!xs_is_null(v = xs_dict_get(msg, "actor")) && *v) {
        if (is_muted(&snac, v)) {
            snac_log(&snac, xs_fmt("rejected message from MUTEd actor %s", v));

            *body  = xs_str_new("rejected");
            *ctype = "text/plain";
            status = 403;
        }
    }

    if (valid_status(status)) {
        enqueue_input(&snac, msg, req, 0);
        *ctype = "application/activity+json";
    }

    user_free(&snac);

    return status;
}
