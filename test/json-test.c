
#include <stdio.h>
#include <stdbool.h>

#include "assert.h"

#include "../xs.h"
#include "../xs_json.h"
#include "../xs_url.h"

int main(int argc, char* argv[]) {
	bool quiet = argc > 1 && strcmp(argv[1], "-q") == 0;
	
	if (!quiet)
		printf("Verbose mode\n");
	
	const char* testjson = 
		"\n"
		"# comment 1\n"
		"	// comment 2\n"
		"{  # comment 3\n"
		"   // comment 4\n"
		"\"scheme\": \"https\"    # comment 5\n"
		"                     ,   // comment 6\n"
		"\"prefix\": # comment 7\n"
		"            \"/fedi\",\n"
		"\"cssurls\"  // comment 8\n"
		"           : [ # comment 9\n"
		"             \"url1\" // comment 10\n"
		"                     , # comment 11\n"
		"             \"https://url2/\"   // comment 12\n"
		"] # comment 13\n"
		"}	// comment 14\n"
		"# comment 15\n"
		;
	const char* cleanjson =
		"{\n"
		"    \"prefix\": \"/fedi2\",\n" // (replaced -> on first position now)
		"    \"scheme\": \"https\",\n"
		"    \"cssurls\": [\n"
		"        \"url1\",\n"
		"        \"https://url2/\"\n"
		"    ]\n"
		"}" // Keine Leerzeile am Ende der Datei
		;
	
	char *dummy_config = xs_json_loadsC(testjson, true);
	
	assert_str_equals(xs_dict_get(dummy_config, "scheme"), "https", "");
	dummy_config = xs_dict_set(dummy_config, "prefix", "/fedi2");
	xs *backjson = xs_json_dumps_pp(dummy_config, 4);
	assert_str_equals(backjson, cleanjson, "\n\n(In case just entries are permutated, just adapt the test or stabilize json write-back)\n\n");

	if (!quiet)
		printf("\tTest OK\n");
	return 0;
}

