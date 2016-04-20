#ifndef utilis
#define utilis

#include <string.h>
#include <time.h>
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/time.h>
#include <stdlib.h>

char *get_datetime();
char **string_to_argv(char *string, int *argc);
int is_delimetr(char symbol);
void add_flags(int fd, int flags);

#endif
