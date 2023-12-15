/* snac - A simple, minimalistic ActivityPub instance */
/* copyright (c) 2022 - 2023 grunfink et al. / MIT license */

#include "xs.h"
#include "xs_regex.h"
#include "xs_mime.h"
#include "xs_html.h"

#include "snac.h"

/* emoticons, people laughing and such */
const char *smileys[] = {
    ":-)",        "&#128578;",
    ":-D",        "&#128512;",
    "X-D",        "&#128518;",
    ";-)",        "&#128521;",
    "B-)",        "&#128526;",
    ">:-(",       "&#128545;",
    ":-(",        "&#128542;",
    ":-*",        "&#128536;",
    ":-/",        "&#128533;",
    "8-o",        "&#128562;",
    "%-)",        "&#129322;",
    ":_(",        "&#128546;",
    ":-|",        "&#128528;",
    "<3",         "&#10084;&#65039;",
    ":facepalm:", "&#129318;",
    ":shrug:",    "&#129335;",
    ":shrug2:",   "&#175;\\_(&#12484;)_/&#175;",
    ":eyeroll:",  "&#128580;",
    ":beer:",     "&#127866;",
    ":beers:",    "&#127867;",
    ":munch:",    "&#128561;",
    ":thumb:",    "&#128077;",
    NULL,         NULL
};


static xs_str *format_line(const char *line, xs_list **attach)
/* formats a line */
{
    xs_str *s = xs_str_new(NULL);
    char *p, *v;

    /* split by markup */
    xs *sm = xs_regex_split(line,
        "(`[^`]+`|\\*\\*?[^\\*]+\\*?\\*|https?:/" "/[^[:space:]]+)");
    int n = 0;

    p = sm;
    while (xs_list_iter(&p, &v)) {
        if ((n & 0x1)) {
            /* markup */
            if (xs_startswith(v, "`")) {
                xs *s1 = xs_crop_i(xs_dup(v), 1, -1);
                xs *e1 = encode_html(s1);
                xs *s2 = xs_fmt("<code>%s</code>", e1);
                s = xs_str_cat(s, s2);
            }
            else
            if (xs_startswith(v, "**")) {
                xs *s1 = xs_crop_i(xs_dup(v), 2, -2);
                xs *s2 = xs_fmt("<b>%s</b>", s1);
                s = xs_str_cat(s, s2);
            }
            else
            if (xs_startswith(v, "*")) {
                xs *s1 = xs_crop_i(xs_dup(v), 1, -1);
                xs *s2 = xs_fmt("<i>%s</i>", s1);
                s = xs_str_cat(s, s2);
            }
            else
            if (xs_startswith(v, "http")) {
                xs *u  = xs_replace(v, "#", "&#35;");
                xs *v2 = xs_strip_chars_i(xs_dup(u), ".");

                const char *mime = xs_mime_by_ext(v2);

                if (attach != NULL && xs_startswith(mime, "image/")) {
                    /* if it's a link to an image, insert it as an attachment */
                    xs *d = xs_dict_new();

                    d = xs_dict_append(d, "mediaType", mime);
                    d = xs_dict_append(d, "url",       v2);
                    d = xs_dict_append(d, "name",      "");
                    d = xs_dict_append(d, "type",      "Image");

                    *attach = xs_list_append(*attach, d);
                }
                else {
                    xs *s1 = xs_fmt("<a href=\"%s\" target=\"_blank\">%s</a>", v2, u);
                    s = xs_str_cat(s, s1);
                }
            }
            else
                s = xs_str_cat(s, v);
        }
        else
            /* surrounded text, copy directly */
            s = xs_str_cat(s, v);

        n++;
    }

    return s;
}


xs_str *not_really_markdown(const char *content, xs_list **attach)
/* formats a content using some Markdown rules */
{
    xs_str *s  = xs_str_new(NULL);
    int in_pre = 0;
    int in_blq = 0;
    xs *list;
    char *p, *v;

    /* work by lines */
    list = xs_split(content, "\n");

    p = list;
    while (xs_list_iter(&p, &v)) {
        xs *ss = NULL;

        if (strcmp(v, "```") == 0) {
            if (!in_pre)
                s = xs_str_cat(s, "<pre>");
            else
                s = xs_str_cat(s, "</pre>");

            in_pre = !in_pre;
            continue;
        }

        if (in_pre) {
            // Encode all HTML characters when we're in pre element until we are out.
            ss = encode_html(v);

            s = xs_str_cat(s, ss);
            s = xs_str_cat(s, "<br>");
            continue;
        }

        else
            ss = xs_strip_i(format_line(v, attach));

        if (xs_startswith(ss, "---")) {
            /* delete the --- */
            ss = xs_strip_i(xs_crop_i(ss, 3, 0));
            s = xs_str_cat(s, "<hr>");

            s = xs_str_cat(s, ss);

            continue;
        }

        if (xs_startswith(ss, ">")) {
            /* delete the > and subsequent spaces */
            ss = xs_strip_i(xs_crop_i(ss, 1, 0));

            if (!in_blq) {
                s = xs_str_cat(s, "<blockquote>");
                in_blq = 1;
            }

            s = xs_str_cat(s, ss);
            s = xs_str_cat(s, "<br>");

            continue;
        }

        if (in_blq) {
            s = xs_str_cat(s, "</blockquote>");
            in_blq = 0;
        }

        s = xs_str_cat(s, ss);
        s = xs_str_cat(s, "<br>");
    }

    if (in_blq)
        s = xs_str_cat(s, "</blockquote>");
    if (in_pre)
        s = xs_str_cat(s, "</pre>");

    /* some beauty fixes */
    s = xs_replace_i(s, "<br><br><blockquote>", "<br><blockquote>");
    s = xs_replace_i(s, "</blockquote><br>", "</blockquote>");
    s = xs_replace_i(s, "</pre><br>", "</pre>");

    {
        /* traditional emoticons */
        const char **emo = smileys;

        while (*emo) {
            s = xs_replace_i(s, emo[0], emo[1]);
            emo += 2;
        }
    }

    return s;
}


const char *valid_tags[] = {
    "a", "p", "br", "br/", "blockquote", "ul", "ol", "li", "cite", "small",
    "span", "i", "b", "u", "s", "pre", "code", "em", "strong", "hr", "img", "del", "bdi", NULL
};

xs_str *sanitize(const char *content)
/* cleans dangerous HTML output */
{
    xs_str *s = xs_str_new(NULL);
    xs *sl;
    int n = 0;
    char *p, *v;

    sl = xs_regex_split(content, "</?[^>]+>");

    p = sl;

    n = 0;
    while (xs_list_iter(&p, &v)) {
        if (n & 0x1) {
            xs *s1  = xs_strip_i(xs_crop_i(xs_dup(v), v[1] == '/' ? 2 : 1, -1));
            xs *l1  = xs_split_n(s1, " ", 1);
            xs *tag = xs_tolower_i(xs_dup(xs_list_get(l1, 0)));
            xs *s2  = NULL;
            int i;

            /* check if it's one of the valid tags */
            for (i = 0; valid_tags[i]; i++) {
                if (strcmp(tag, valid_tags[i]) == 0)
                    break;
            }

            if (valid_tags[i]) {
                /* accepted tag: rebuild it with only the accepted elements */
                xs *el = xs_regex_select(v, "(src|href|rel|class|target)=\"[^\"]*\"");
                xs *s3 = xs_join(el, " ");

                s2 = xs_fmt("<%s%s%s%s>",
                    v[1] == '/' ? "/" : "", tag, xs_list_len(el) ? " " : "", s3);

                s = xs_str_cat(s, s2);
            } else {
                /* else? just show it with encoded code.. that's it. */
                xs *el = encode_html(v);
                s = xs_str_cat(s, el);
            }
        }
        else {
            /* non-tag */
            s = xs_str_cat(s, v);
        }

        n++;
    }

    return s;
}


xs_str *encode_html(const char *str)
/* escapes html characters */
{
    xs_str *encoded = xs_html_encode((char *)str);

    /* Restore only <br>. Probably safe. Let's hope nothing goes wrong with this. */
    encoded = xs_replace_i(encoded, "&lt;br&gt;", "<br>");

    return encoded;
}
