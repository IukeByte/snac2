/* copyright (c) 2022 - 2023 grunfink / MIT license */

#ifndef _XS_URL_H

#define _XS_URL_H

#ifndef _XS_H
#error You must include xs.h first.
#endif

#include <stdbool.h>

bool xs_is_http_https(const xs_val *testee, const char **restOfUrl);

bool xs_compare_url(const char *url1, const char *url2);


#ifdef XS_IMPLEMENTATION


bool xs_is_http_https(const xs_val *str, const char **restOfUrl)
{
    if (*str++ != 'h' || *str++ != 't' || *str++ != 't' || *str++ != 'p')
        return false;
    
    if (str[0] == 's') str++;
    
    if (*str++ != ':' || *str++ != '/' || *str++ != '/')
        return false;
    
    if (restOfUrl)
        *restOfUrl = str;
    return true;
}


bool xs_compare_url(const char *url1, const char *url2)
{
    const char* restOfUrl1;
    const char* restOfUrl2;
    if (xs_is_http_https(url1, &restOfUrl1)
            && xs_is_http_https(url2, &restOfUrl2)) {
        restOfUrl1 = url1;
        restOfUrl2 = url2;
    }
    
   return strcmp(url1, url2) == 0;
}

#endif /* XS_IMPLEMENTATION */

#endif /* _XS_URL_H */

