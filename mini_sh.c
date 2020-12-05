#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define FALSE 0
#define TRUE !FALSE

#define EOL 1
#define ARG 2
#define AMPERSAND 3
#define PIPE 4
#define REDIRECT 5

#define FOREGROUND 1
#define BACKGROUND 2

char *ptr, *tok;

int get_token(char **outptr)
{
	int type;

	*outptr = tok;
	while ((*ptr == ' ') || (*ptr == '\t'))
		ptr++;

	*tok++ = *ptr;

	switch (*ptr++)
	{
	case '\0':
		type = EOL;
		break;
	case '&':
		type = AMPERSAND;
		break;
	default:
		type = ARG;
		while ((*ptr != ' ') && (*ptr != '&') &&
			   (*ptr != '\t') && (*ptr != '\0'))
			*tok++ = *ptr++;
	}
	*tok++ = '\0';
	return (type);
}

int equal(char *str1, char *str2)
{
	return !strcmp(str1, str2);
}

int execute(char **comm, int how)
{
	int pid = fork();
	if (pid < 0)
	{
		fprintf(stderr, "minish : fork error\n");
		return -1;
	}
	else if (pid == 0)
	{
		// Child process area
		execvp(*comm, comm);
		// If such command exists, process will be replaced and below will not be executed.
		fprintf(stderr, "minish : command not found\n");
		exit(127);
	}

	// Parent process area
	if (how == BACKGROUND)
	{
		// Background execution
		printf("[%d]\n", pid);
	}
	else
	{
		// Foreground execution
		// Wait until child process is finished
		while (waitpid(pid, NULL, 0) < 0)
			if (errno != EINTR)
				return -1;
	}
	return 0;
}

int parse_and_execute(char *input)
{
	static char tokens[1024];
	static char *arg[1024];
	int type;
	int quit = FALSE;
	int narg = 0;
	int finished = FALSE;

	ptr = input;
	tok = tokens;
	while (!finished)
	{
		switch (type = get_token(&arg[narg]))
		{
		case ARG:
			narg++;
			break;
		case EOL:
		case AMPERSAND:
			if (equal(arg[0], "quit") || equal(arg[0], "exit"))
			{
				quit = TRUE;
			}
			else if (equal(arg[0], "cd"))
			{
				chdir(arg[1]);
			}
			else if (equal(arg[0], "type"))
			{
				if (narg > 1)
				{
					int fid = open(arg[1], O_RDONLY);
					if (fid >= 0)
					{
						char buf[512];
						for (int len; (len = read(fid, buf, 512)) > 0;)
						{
							for (int i = 0; i < len; i++)
								putchar(buf[i]);
						}
					}
					close(fid);
				}
			}
			else
			{
				int how = (type == AMPERSAND) ? BACKGROUND : FOREGROUND;
				arg[narg] = NULL;
				if (narg != 0)
					execute(arg, how);
			}
			narg = 0;
			if (type == EOL)
				finished = TRUE;
			break;
		}
	}
	return quit;
}

int main()
{
	static char input[512];
	printf("msh # ");
	while (gets(input))
	{
		if (parse_and_execute(input))
			break;
		printf("msh # ");
	}
}
