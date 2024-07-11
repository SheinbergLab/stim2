#if 0
*******************************************************************************
Project		MPI
Program 	sockapi.c
Date		1998Feb22
Author		David Sheinberg
Description	API functions for sending commands using TCP/IP sockets
*******************************************************************************
#endif
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdarg.h>
#include <sys/ioctl.h>

#include "sockapi.h"
	
#define STIM_PORT 4610	
static int rmt_socket = -1;

#define STREAM_PORT 4710	
static int stream_socket = -1;

static void socket_flush(int sock);

/*****************************************************************************/
/********************************* SOCKET_OPEN *******************************/
/*****************************************************************************/
/*
 * Purpose:	To open a communication socket
 * Input:	name of server, port number
 * Process:	open socket and connect to server
 * Output:	0 or greater: successful socket
 *                    -1: failed socket call
 *                    -2: bad host
 *                    -3: failed connect call
 */
/*****************************************************************************/
int socket_open(char *name, int port)
{
	int sock, param;
	struct sockaddr_in server;
	struct hostent *hp, *gethostbyname();
	
	/* Create socket */
	sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock < 0) {
		return -1;
	}
	server.sin_family = AF_INET;
	hp = gethostbyname(name);
	if (hp == 0) {
		return -2;
	}
	memcpy(&server.sin_addr, hp->h_addr, hp->h_length);
	server.sin_port = htons(port);
	
	if (connect(sock, (struct sockaddr *)&server, sizeof(server)) < 0) {
		return -3;
	}
	param = 1;
	setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &param, sizeof(param));
	socket_flush(sock);
	return sock;
}

static void socket_flush(int sock)
{
	static char buf[64];
	int flag = 1;
	int n;
	
	if (ioctl(sock, FIONBIO, &flag) < 0) {
		return;
	}
	do {
		n = read(sock, buf, sizeof(buf));
	} while (n >= 0);
	
	flag = 0;
	if (ioctl(sock, FIONBIO, &flag) < 0) {
		return;
	}
}

/*****************************************************************************/
/****************************** SOCKET_CLOSE**********************************/
/*****************************************************************************/
/*
 * Purpose:	Close socket
 * Input:	socket id
 * Process:	close socket
 * Output:	none
 */
/*****************************************************************************/
int socket_close(int sock)
{
	close(sock);
	return 1;
}

/*****************************************************************************/
/***************************** SOCKET_WRITE **********************************/
/*****************************************************************************/
/*
 * Purpose:	Write data over the socket
 * Input:	socket id, message, length of message
 * Process:	write message to socket
 * Output:	returns 1 on success, 0 on failure
 */
/*****************************************************************************/
int socket_write(int sock, char *message, int nbytes)
{
	if (write(sock, message, nbytes) < 0) {
		perror("writing socket");
		return 0;
	}
	return 1;
}

/*****************************************************************************/
/***************************** SOCKET_READ ***********************************/
/*****************************************************************************/
/*
 * Purpose:	Read data from the socket
 * Input:	socket id, char **message, message length *
 * Process:	read message from socket
 * Output:	returns 1 on success, 0 on failure
 * Limitations: buf is currently limited to 4096 characters
 */
/*****************************************************************************/
int socket_read(int sock, char **message, int *nbytes)
{
	static char buf[4096];
	int n;
	if ((n = read(sock, buf, sizeof(buf))) < 0) {
	  perror("reading stream socket");
	  if (message) *message = NULL;
	  if (nbytes) *nbytes = 0;
	  return 0;
	}

	if (message) *message = buf;
	if (nbytes) *nbytes = n;
	return 1;
}

/*****************************************************************************/
/****************************** SOCKET_SEND **********************************/
/*****************************************************************************/
/*
 * Purpose:	Write data over the socket and wait for reply
 * Input:	socket id, message, length of message
 * Process:	write message to socket and read reply
 * Output:	returns 1 on success, 0 on failure
 */
/*****************************************************************************/
int socket_send(int sock, char *sbuf, int sbytes, char **rbuf, int *rbytes)
{
	int status;
	status = socket_write(sock, sbuf, sbytes);
	if (!status) return 0;
	status = socket_read(sock, rbuf, rbytes);
	return status;
}


/*----------------------------------------------------------------------*/
/*                        "Remote" Functions                            */
/*----------------------------------------------------------------------*/

int rmt_sync(void)
{
  static int i = 0;
  char *res;
  char expected[32];
  if (rmt_socket == -1) return NULL;	/* Not connected */
  
  ++i;
  res = rmt_send("ping %d", i);
  sprintf(expected, "pong %d", i);
  if (strcmp(res, expected)) return 0;
  else return 1;
}

int rmt_init(char *server, int port)
{
  int i, ntries = 32;
  char *stim_server = STIM_INET_ADDRESS;
  int tcpport = port;
  
  if (rmt_socket >= 0) return 1; /* already open */
  
  if (!strcmp(server, "stim")) server = stim_server;

  /* 
   * This loop is an attempt to fix a read/write sync problem
   * encountered with Socket 4.24, which seems to not properly
   * flush its buffers properly (although I can't say for sure
   * this is the problem).  This is a brute force solution --
   * try onces, check the sync, if it's ok, break...
   */
     
  for (i = 0; i < ntries; i++) {
    rmt_socket = socket_open(server, tcpport);
    if (rmt_socket < 0) break;
    if (rmt_sync()) break;	/* success! */
    socket_close(rmt_socket);
  }
  if (i != ntries) return rmt_socket >= 0;
  else return 0;
}

int rmt_close(void)
{
	if (rmt_socket == -1) return 0;	/* Never opened */
	else socket_close(rmt_socket);
	rmt_socket = -1;
	return 1;
}

char *rmt_send(char *format, ...)
{
	char *rbuf, *eol;
	int rbytes, status, l;
	static char sbuf[SOCK_BUF_SIZE];

	va_list arglist;
	va_start(arglist, format);
	vsprintf(sbuf, format, arglist);
	va_end(arglist);
	
	if (rmt_socket == -1) return NULL;	/* Not connected */

	l = strlen(sbuf)-1;
	if (sbuf[l] != '\n') {
		sbuf[l+1] = '\n';
		sbuf[l+2] = '\0';
	}
	status = socket_send(rmt_socket, sbuf, strlen(sbuf), &rbuf, &rbytes);
	if (status == 0) return NULL;
	if (eol = strrchr(rbuf, '\n')) *eol = 0;
	if (eol = strrchr(rbuf, '\r')) *eol = 0;
	return rbuf;
}


/*----------------------------------------------------------------------*/
/*                        "Stream" Functions                            */
/*----------------------------------------------------------------------*/

int stream_sync(void)
{
  static int i = 0;
  char *res;
  char expected[32];
  if (stream_socket == -1) return NULL;	/* Not connected */
  
  ++i;
  res = stream_send("ping %d", i);
  sprintf(expected, "pong %d", i);
  if (strcmp(res, expected)) return 0;
  else return 1;
}

int stream_init(char *server)
{
  int i, ntries = 32;
  int tcpport = STREAM_PORT;
  
  if (stream_socket >= 0) return 1; /* already open */
  
  /* 
   * This loop is an attempt to fix a read/write sync problem
   * encountered with Socket 4.24, which seems to not properly
   * flush its buffers properly (although I can't say for sure
   * this is the problem).  This is a brute force solution --
   * try onces, check the sync, if it's ok, break...
   */
     
  for (i = 0; i < ntries; i++) {
    stream_socket = socket_open(server, tcpport);
    if (stream_socket < 0) break;
    if (stream_sync()) break;	/* success! */
    socket_close(stream_socket);
  }
  if (i != ntries) return stream_socket >= 0;
  else return 0;
}

int stream_close(void)
{
	if (stream_socket == -1) return 0;	/* Never opened */
	else socket_close(stream_socket);
	stream_socket = -1;
	return 1;
}

char *stream_send(char *format, ...)
{
	char *rbuf, *eol;
	int rbytes, status, l;
	static char sbuf[SOCK_BUF_SIZE];

	va_list arglist;
	va_start(arglist, format);
	vsprintf(sbuf, format, arglist);
	va_end(arglist);
	
	if (stream_socket == -1) return NULL;	/* Not connected */

	l = strlen(sbuf)-1;
	if (sbuf[l] != '\n') {
		sbuf[l+1] = '\n';
		sbuf[l+2] = '\0';
	}
	status = socket_send(stream_socket, sbuf, 
			     strlen(sbuf), &rbuf, &rbytes);
	if (status == 0) return NULL;
	if (eol = strrchr(rbuf, '\n')) *eol = 0;
	if (eol = strrchr(rbuf, '\r')) *eol = 0;
	return rbuf;
}



char *sock_send(char *server, int port, char *format, ...)
{
  int i, ntries = 32;
  int tcpport = STREAM_PORT;
  int sock;
  char *rbuf, *eol;
  int rbytes, status, l;
  static char sbuf[SOCK_BUF_SIZE];
  
  va_list arglist;
  va_start(arglist, format);
  vsprintf(sbuf, format, arglist);
  va_end(arglist);
	
  sock = socket_open(server, port);
  if (sock < 0) return NULL;

  l = strlen(sbuf)-1;
  if (sbuf[l] != '\n') {
    sbuf[l+1] = '\n';
    sbuf[l+2] = '\0';
  }
  status = socket_send(sock, sbuf, 
		       strlen(sbuf), &rbuf, &rbytes);
  if (status == 0) return NULL;
  if (eol = strrchr(rbuf, '\n')) *eol = 0;
  if (eol = strrchr(rbuf, '\r')) *eol = 0;
  
  socket_close(sock);
  return rbuf;
}


