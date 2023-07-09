
#ifndef _ASSERT_H
#define _ASSERT_H

#include <stdio.h>

#define assert(b) \
	do { if (!(b)) fprintf(stderr, "Assert failed! [%s:%d]", __FILE__, __LINE__,)  } while (false);

#define assert_str_equals(str1, str2, msg) \
	if (strcmp((str1), (str2))) { \
		fprintf(stderr, "Assert failed: strings not equal! [%s:%d]\n  str1=%s\n  str2=%s\n", __FILE__, __LINE__, (str1), (str2)); \
		fputs(msg, stderr); \
		return 99; \
	}


#endif // _ASSERT_H

