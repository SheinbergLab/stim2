#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <readline/readline.h>
#include "sockapi.h"

#ifdef STREAMCTRL
#define rmt_init(a) stream_init(a)
#define rmt_send(a) stream_send(a)
#define rmt_close(a) stream_close(a)
#define PROMPT "stream> "
#else
#define PROMPT "stim> "
#endif

main (int argc, char *argv[])
{
  char *input, *result;
  char *server;

  if (argc < 2) {
	  printf("usage: stimctrl server [command(s)]\n");
	  exit(0);
  }
  else server = argv[1];

  /* Non interactive */
  if (argc > 2) {
    char *cmd;
    int i, len = 0;
    
    /* Loop through and figure out how long the command line is */
    for (i = 2; i < argc; i++) {
      len += (strlen(argv[i])+1);
    }
    cmd = (char *) calloc(len, sizeof(char));
    for (i = 2; i < argc; i++) {
      strcat(cmd, argv[i]);
      strcat(cmd, " ");
    }

    if (!rmt_init(server)) {
      fprintf(stderr, 
	      "stimctrl: error connecting to server %s\n", server);
      exit(-1);
    }
    
    result = rmt_send(cmd);
    if (strlen(result)) {
      printf("%s\n", result);
    }
    rmt_close();
    free(cmd);
    exit(0);
  }
  
  /* Interactive (using readline) */
  while (1) {
    input = readline(PROMPT);
    if (input) {
      if (strcmp(input,"exit") && strcmp(input,"quit")) {
	add_history (input);
	if (!rmt_init(server)) {
	  fprintf(stderr, 
		  "stimctrl: error connecting to server %s\n", server);
	  exit(-1);
	}
	
	result = rmt_send(input);
	if (strlen(result)) {
	  printf("%s\n", result);
	}
	rmt_close();
      }
      else goto done;
    }
    else {
    done:
      exit(0);
    }
  }
}
