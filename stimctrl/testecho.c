#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include <rtcapi.h>
	
#define DATA "toggle_stim\n"
	
/*
 * This program creates a  socket and inits a connection with the socket
 * given in the command line.   One message is sent over the connection and
 * then the socket is closed, ending the connection. The form of the cmd
 * line is streamwrite hostname portnumber
 */
	
main(int argc, char *argv[])
{
	char buf[256];
	char *eol;
	float elapsed;
	int sock, len, param, n;
	unsigned long addr;
	struct sockaddr_in server;
	struct hostent *hp, *gethostbyname();

	if (argc < 3) {
		fprintf(stderr, "usage: testsock host port\n");
		exit(0);
	}

	rtc_open("/dev/rtclock");
	
	/* Create socket */
	sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock < 0) {
		perror("opening stream socket");
		exit(1);
	}
	/* Connect socket  using name specified by  command line.  */
	server.sin_family = AF_INET;
#ifndef NO_DNS
	hp = gethostbyname(argv[1]);
	if (hp == 0) {
		fprintf(stderr, "%s: unknown host\n", argv[1]);
		exit(2);
	}
	memcpy(&server.sin_addr, hp->h_addr, hp->h_length);
#else
	addr = inet_addr("193.175.138.106");
	memcpy(&server.sin_addr, &addr, sizeof(addr));
#endif
	server.sin_port = htons(atoi(argv[2]));
	
	if (connect(sock, (struct sockaddr *)&server, sizeof(server)) < 0) {
		perror("connecting stream socket");
		exit(1);
	}

	param = 1;
	setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &param, sizeof(param));
//	param = 3;
//	setsockopt(sock, SOL_SOCKET, SO_RCVLOWAT, &param, sizeof(param));
	for (;;) {
		sleep(1);
		rtc_setStart(NULL);
		if (write(sock, DATA, sizeof(DATA)) < 0)
			perror("writing on stream socket");

		if ((n = read(sock, buf, sizeof(buf))) < 0)
			perror("reading stream socket");
		elapsed = rtc_elapsed();
		if (eol = strchr(buf, '\n')) {
			*eol = 0;
		}
		if (eol = strchr(buf, '\r')) {
			*eol = 0;
		}
		printf("%.2fms: [%s]\n", elapsed, buf);
	}
	close(sock);
}
