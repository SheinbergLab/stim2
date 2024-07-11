#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sockapi.h>

main(int argc, char *argv[])
{
  int i, id;
  char *result;
  if (argc < 4) {
    printf("usage: testrmt server port cmd\n");
    exit(0);
  }

  result = sock_send(argv[1], atoi(argv[2]), argv[3]);
  if (!result) { 
     printf("Error establishing socket comm with server %s\n", 
     argv[1]);
  }
  else {
    puts(result);
  }
}
	
	
	
   
