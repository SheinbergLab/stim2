#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sockapi.h>

main(int argc, char *argv[])
{
	int i, id;
	char *result;
	if (argc < 2) {
		printf("usage: testrmt server\n");
		exit(0);
	}

	rmt_init(argv[1]);
	for (i = 0; i < 100; i++) {
		rmt_send("clearscreen");
		result = rmt_send("setstim 0");
		sscanf(result, "%d", &id);
		printf("%s [%d]\n", result, id);
	}
	rmt_close();
}
	
	
	
   
