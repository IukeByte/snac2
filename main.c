/* snac - A simple, minimalistic ActivityPub instance */
/* copyright (c) 2022 - 2023 grunfink et al. / MIT license */

#include "xs.h"
#include "xs_io.h"
#include "xs_json.h"

#include "snac.h"

#include <sys/stat.h>

int usage(void)
{
    printf("snac " VERSION " - A simple, minimalistic ActivityPub instance\n");
    printf("Copyright (c) 2022 - 2023 grunfink et al. / MIT license\n");
    printf("\n");
    printf("Commands:\n");
    printf("\n");
    printf("init [{basedir}]                    Initializes the data storage\n");
    printf("upgrade {basedir}                   Upgrade to a new version\n");
    printf("adduser {basedir} [{uid}]           Adds a new user\n");
    printf("deluser {basedir} {uid}             Deletes a user\n");
    printf("httpd {basedir}                     Starts the HTTPD daemon\n");
    printf("purge {basedir}                     Purges old data\n");
    printf("webfinger {basedir} {actor}         Queries about an actor (@user@host or actor url)\n");
    printf("queue {basedir} {uid}               Processes a user queue\n");
    printf("follow {basedir} {uid} {actor}      Follows an actor\n");
    printf("unfollow {basedir} {uid} {actor}    Unfollows an actor\n");
    printf("request {basedir} {uid} {url}       Requests an object\n");
    printf("actor {basedir} {uid} {url}         Requests an actor\n");
    printf("note {basedir} {uid} {'text'}       Sends a note to followers\n");
    printf("resetpwd {basedir} {uid}            Resets the password of a user\n");
    printf("ping {basedir} {uid} {actor}        Pings an actor\n");
    printf("webfinger_s {basedir} {uid} {actor} Queries about an actor (@user@host or actor url)\n");
    printf("pin {basedir} {uid} {msg_url}       Pins a message\n");
    printf("unpin {basedir} {uid} {msg_url}     Unpins a message\n");
    printf("block {basedir} {instance_url}      Blocks a full instance\n");
    printf("unblock {basedir} {instance_url}    Unblocks a full instance\n");
    printf("limit {basedir} {uid} {actor}       Limits an actor (drops their announces)\n");
    printf("unlimit {basedir} {uid} {actor}     Unlimits an actor\n");

/*    printf("question {basedir} {uid} 'opts'  Generates a poll (;-separated opts)\n");*/

    return 1;
}


char *get_argv(int *argi, int argc, char *argv[])
{
    if (*argi < argc)
        return argv[(*argi)++];
    else
        return NULL;
}


#define GET_ARGV() get_argv(&argi, argc, argv)

#include "xs_html.h"

xs_html *html_note(snac *user, char *summary,
                   char *div_id, char *form_id,
                   char *ta_plh, char *ta_content,
                   char *edit_id, char *actor_id,
                   xs_val *cw_yn, char *cw_text,
                   xs_val *mnt_only, char *redir,
                   char *in_reply_to, int poll);

int main(int argc, char *argv[])
{
    char *cmd;
    char *basedir;
    char *user;
    char *url;
    int argi = 1;
    snac snac;

    /* ensure group has write access */
    umask(0007);

    if ((cmd = GET_ARGV()) == NULL)
        return usage();

    if (strcmp(cmd, "init") == 0) { /** **/
        /* initialize the data storage */
        /* ... */
        basedir = GET_ARGV();

        return snac_init(basedir);
    }

    if (strcmp(cmd, "upgrade") == 0) { /** **/
        int ret;

        /* upgrade */
        if ((basedir = GET_ARGV()) == NULL)
            return usage();

        if ((ret = srv_open(basedir, 1)) == 1)
            srv_log(xs_dup("OK"));

        return ret;
    }

    if (strcmp(cmd, "markdown") == 0) { /** **/
        /* undocumented, for testing only */
        xs *c = xs_readall(stdin);
        xs *fc = not_really_markdown(c, NULL);

        printf("<html>\n%s\n</html>\n", fc);
        return 0;
    }

    if ((basedir = GET_ARGV()) == NULL)
        return usage();

    if (!srv_open(basedir, 0)) {
        srv_log(xs_fmt("error opening data storage at %s", basedir));
        return 1;
    }

    if (strcmp(cmd, "adduser") == 0) { /** **/
        user = GET_ARGV();

        return adduser(user);

        return 0;
    }

    if (strcmp(cmd, "httpd") == 0) { /** **/
        httpd();
        srv_free();
        return 0;
    }

    if (strcmp(cmd, "purge") == 0) { /** **/
        purge_all();
        return 0;
    }

    if ((user = GET_ARGV()) == NULL)
        return usage();

    if (strcmp(cmd, "block") == 0) { /** **/
        int ret = instance_block(user);

        if (ret < 0) {
            fprintf(stderr, "Error blocking instance %s: %d\n", user, ret);
            return 1;
        }

        return 0;
    }

    if (strcmp(cmd, "unblock") == 0) { /** **/
        int ret = instance_unblock(user);

        if (ret < 0) {
            fprintf(stderr, "Error unblocking instance %s: %d\n", user, ret);
            return 1;
        }

        return 0;
    }

    if (strcmp(cmd, "webfinger") == 0) { /** **/
        xs *actor = NULL;
        xs *uid = NULL;
        int status;

        status = webfinger_request(user, &actor, &uid);

        printf("status: %d\n", status);
        if (actor != NULL)
            printf("actor: %s\n", actor);
        if (uid != NULL)
            printf("uid: %s\n", uid);

        return 0;
    }

    if (!user_open(&snac, user)) {
        printf("invalid user '%s'\n", user);
        return 1;
    }

    lastlog_write(&snac, "cmdline");

    if (strcmp(cmd, "resetpwd") == 0) { /** **/
        return resetpwd(&snac);
    }

    if (strcmp(cmd, "deluser") == 0) { /** **/
        return deluser(&snac);
    }

    if (strcmp(cmd, "queue") == 0) { /** **/
        process_user_queue(&snac);
        return 0;
    }

    if (strcmp(cmd, "timeline") == 0) { /** **/
#if 0
        xs *list = local_list(&snac, XS_ALL);
        xs *body = html_timeline(&snac, list, 1);

        printf("%s\n", body);
        user_free(&snac);
        srv_free();
#endif

        xs *idx  = xs_fmt("%s/private.idx", snac.basedir);
        xs *list = index_list_desc(idx, 0, 256);
        xs *tl   = timeline_top_level(&snac, list);

        xs_json_dump(tl, 4, stdout);

        return 0;
    }

    if ((url = GET_ARGV()) == NULL)
        return usage();

    if (strcmp(cmd, "webfinger_s") == 0) { /** **/
        xs *actor = NULL;
        xs *uid = NULL;
        int status;

        status = webfinger_request_signed(&snac, url, &actor, &uid);

        printf("status: %d\n", status);
        if (actor != NULL)
            printf("actor: %s\n", actor);
        if (uid != NULL)
            printf("uid: %s\n", uid);

        return 0;
    }

    if (strcmp(cmd, "announce") == 0) { /** **/
        xs *msg = msg_admiration(&snac, url, "Announce");

        if (msg != NULL) {
            enqueue_message(&snac, msg);

            if (dbglevel) {
                xs_json_dump(msg, 4, stdout);
            }
        }

        return 0;
    }

    if (strcmp(cmd, "follow") == 0) { /** **/
        xs *msg = msg_follow(&snac, url);

        if (msg != NULL) {
            char *actor = xs_dict_get(msg, "object");

            following_add(&snac, actor, msg);

            enqueue_output_by_actor(&snac, msg, actor, 0);

            if (dbglevel) {
                xs_json_dump(msg, 4, stdout);
            }
        }

        return 0;
    }

    if (strcmp(cmd, "unfollow") == 0) { /** **/
        xs *object = NULL;

        if (valid_status(following_get(&snac, url, &object))) {
            xs *msg = msg_undo(&snac, xs_dict_get(object, "object"));

            following_del(&snac, url);

            enqueue_output_by_actor(&snac, msg, url, 0);

            snac_log(&snac, xs_fmt("unfollowed actor %s", url));
        }
        else
            snac_log(&snac, xs_fmt("actor is not being followed %s", url));

        return 0;
    }

    if (strcmp(cmd, "limit") == 0) { /** **/
        int ret;

        if (!following_check(&snac, url))
            snac_log(&snac, xs_fmt("actor %s is not being followed", url));
        else
        if ((ret = limit(&snac, url)) == 0)
            snac_log(&snac, xs_fmt("actor %s is now limited", url));
        else
            snac_log(&snac, xs_fmt("error limiting actor %s (%d)", url, ret));

        return 0;
    }

    if (strcmp(cmd, "unlimit") == 0) { /** **/
        int ret;

        if (!following_check(&snac, url))
            snac_log(&snac, xs_fmt("actor %s is not being followed", url));
        else
        if ((ret = unlimit(&snac, url)) == 0)
            snac_log(&snac, xs_fmt("actor %s is no longer limited", url));
        else
            snac_log(&snac, xs_fmt("error unlimiting actor %s (%d)", url, ret));

        return 0;
    }

    if (strcmp(cmd, "ping") == 0) { /** **/
        xs *actor_o = NULL;

        if (valid_status(actor_request(&snac, url, &actor_o))) {
            xs *msg = msg_ping(&snac, url);

            enqueue_output_by_actor(&snac, msg, url, 0);

            if (dbglevel) {
                xs_json_dump(msg, 4, stdout);
            }
        }
        else {
            srv_log(xs_fmt("Error getting actor %s", url));
            return 1;
        }

        return 0;
    }

    if (strcmp(cmd, "pin") == 0) { /** **/
        int ret = pin(&snac, url);
        if (ret < 0) {
            fprintf(stderr, "error pinning %s %d\n", url, ret);
            return 1;
        }

        return 0;
    }

    if (strcmp(cmd, "unpin") == 0) { /** **/
        int ret = unpin(&snac, url);
        if (ret < 0) {
            fprintf(stderr, "error unpinning %s %d\n", url, ret);
            return 1;
        }

        return 0;
    }

    if (strcmp(cmd, "question") == 0) { /** **/
        int end_secs = 5 * 60;
        xs *opts = xs_split(url, ";");

        xs *msg = msg_question(&snac, "Poll", NULL, opts, 0, end_secs);
        xs *c_msg = msg_create(&snac, msg);

        if (dbglevel) {
            xs_json_dump(c_msg, 4, stdout);
        }

        enqueue_message(&snac, c_msg);
        enqueue_close_question(&snac, xs_dict_get(msg, "id"), end_secs);

        timeline_add(&snac, xs_dict_get(msg, "id"), msg);

        return 0;
    }

    if (strcmp(cmd, "request") == 0) { /** **/
        int status;
        xs *data = NULL;

        status = activitypub_request(&snac, url, &data);

        printf("status: %d\n", status);

        if (data != NULL) {
            xs_json_dump(data, 4, stdout);
        }

        return 0;
    }

    if (strcmp(cmd, "actor") == 0) { /** **/
        int status;
        xs *data = NULL;

        status = actor_request(&snac, url, &data);

        printf("status: %d\n", status);

        if (valid_status(status)) {
            xs_json_dump(data, 4, stdout);
        }

        return 0;
    }

    if (strcmp(cmd, "note") == 0) { /** **/
        xs *content = NULL;
        xs *msg = NULL;
        xs *c_msg = NULL;
        char *in_reply_to = GET_ARGV();

        if (strcmp(url, "-e") == 0) {
            /* get the content from an editor */
            FILE *f;

            unlink("/tmp/snac-edit.txt");
            system("$EDITOR /tmp/snac-edit.txt");

            if ((f = fopen("/tmp/snac-edit.txt", "r")) != NULL) {
                content = xs_readall(f);
                fclose(f);

                unlink("/tmp/snac-edit.txt");
            }
            else {
                printf("Nothing to send\n");
                return 1;
            }
        }
        else
        if (strcmp(url, "-") == 0) {
            /* get the content from stdin */
            content = xs_readall(stdin);
        }
        else
            content = xs_dup(url);

        msg = msg_note(&snac, content, NULL, in_reply_to, NULL, 0);

        c_msg = msg_create(&snac, msg);

        if (dbglevel) {
            xs_json_dump(c_msg, 4, stdout);
        }

        enqueue_message(&snac, c_msg);

        timeline_add(&snac, xs_dict_get(msg, "id"), msg);

        return 0;
    }

    fprintf(stderr, "ERROR: bad command '%s'\n", cmd);

    return 1;
}
