/*	$Id$	*/

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static char *
strtrim(char *str)
{
	int i;

	if (str == NULL)
		return (NULL);

	while (isspace((int)*str))
		str++;

	for (i = strlen(str) - 1; i >= 0; i--) {
		if (!isspace((int)str[i]))
			break;
		str[i] = '\0';
	}

	return (str);
}

int
main(int argc, char **argv)
{
	char buf[512];
	FILE *in, *out;

	if (argc != 2) {
		fprintf(stderr, "error: need 1 parameter\n");
		exit(1);
	}

	in = fopen(argv[1], "r");
	if (in == NULL) {
		perror("fopen(input)");
		exit(1);
	}

	out = NULL;
	while (fgets(buf, sizeof(buf), in) != NULL) {
		if (buf[0] == '>') {
			char fname[512];
			char *start;

			if (out != NULL)	
				fclose(out);

			strcpy(fname, buf + 1);
			start = strtrim(fname);
			strcat(start, ".fa");

			out = fopen(start, "w");
			if (out == NULL) {
				perror("fopen(out)");
				exit(1);
			}
		}

		if (fwrite(buf, strlen(buf), 1, out) != 1) {
			perror("fwrite(out)");
			exit(1);
		}
	}

	return (0);
}
