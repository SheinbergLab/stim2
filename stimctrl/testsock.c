#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sockapi.h>

main(int argc, char *argv[])
{
	char *buf;
	int nbytes;
	int sock;

	if (argc < 4) {
		printf("usage: testsock server port command\n");
		exit(0);
	}

	sock = socket_open(argv[1], atoi(argv[2]));
	switch (sock) {
	  case -1: printf("socket: call failed\n"); exit(0);
	  case -2: printf("socket: bad host name\n"); exit(0);
	  case -3: printf("socket: connection refused\n"); exit(0);
	}

	socket_send(sock, argv[3], strlen(argv[3]), &buf, &nbytes);
	printf("%s", buf);
	socket_close(sock);
}
	
	
	
   
