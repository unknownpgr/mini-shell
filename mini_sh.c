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
#define TRUE  !FALSE

#define EOL          1
#define ARG          2
#define AMPERSAND    3
#define PIPE         4
#define REDIRECT_OUT 5
#define REDIRECT_IN  6

#define FOREGROUND 1
#define BACKGROUND 2

char *ptr, *tok;

int nullPtr;

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
    case '|':
        type = PIPE;
        break;
    case '>':
        type = REDIRECT_OUT;
        break;
    case '<':
        type = REDIRECT_IN;
        break;
    default:
        type = ARG;
        while ((*ptr != ' ') && (*ptr != '&') && (*ptr != '\t') && (*ptr != '\0'))
            *tok++ = *ptr++;
    }
    *tok++ = '\0';
    return (type);
}

int equal(char *str1, char *str2) { return !strcmp(str1, str2); }

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
        // If such command exists, process will be replaced and below will not be
        // executed.
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

void findOp(char *input, char *op, int *opPos)
{
    // Subprocess
    *op = -1;
    *opPos = -1;

#define SET_OP()        \
    {                   \
        *op = input[i]; \
        *opPos = i;     \
    }

    // Find operator position
    for (int i = 0; input[i]; i++)
    {
        switch (input[i])
        {
        case '|':
        case '&':
            if (*opPos < 0)
            {
                SET_OP();
            }
            else if (*op == '>' || *op == '<')
            {
                SET_OP();
            }
            break;
        case '<':
        case '>':
            if (*opPos < 0)
            {
                SET_OP();
            }
            break;
        }
    }
#undef SET_OP
}

int tokenize(char *input, char **tokens)
{
#define IS_EMPTY(c) (((c) == ' ') || ((c) == '\t') || ((c) == '\r') || ((c) == '\n') || ((c) == '\0'))
    char pre = 1;
    char cur;
    int tokenIndex = 0;
    for (int i = 0; input[i]; i++)
    {
        cur = IS_EMPTY(input[i]);
        if (pre && !cur)
        {
            tokens[tokenIndex++] = input + i;
        }
        else if (!pre && cur)
        {
            input[i] = '\0';
        }
    }
    return tokenIndex;
#undef IS_EMPTY
}

int parser(char *input, int pipeIn, int pipeOut)
{
    int subprocess = fork();
    if (subprocess > 0)
        return subprocess;
    if (subprocess < 0)
    {
        fprintf(stderr, "Could not create subprocess.\n");
        exit(127);
    }

    // Subprocess
    char op;
    int opPos;
    findOp(input, &op, &opPos);

    // Split string
    input[opPos] = '\0';

    switch (op)
    {
    case '|':
    {
        int pip[2];                                            // Define a pipe
        pipe(pip);                                             // Initialize pipe
        int pid1 = parser(input, pipeIn, pip[0]);              // First part
        int pid2 = parser(input + opPos + 1, pip[1], pipeOut); // Second part
        waitpid(pid1, NULL, 0);
        waitpid(pid2, NULL, 0);
        break;
    }
    case '&':
    {
        int pid1 = parser(input, nullPtr, pipeOut);
        int pid2 = parser(input + opPos + 1, pipeIn, pipeOut);
        waitpid(pid2, NULL, 0);
        break;
    }
    case '<':
        break;
    case '>':
        break;
    default:
    {
        // Parse input and get tokens(arguments)
        char *tokens[100];
        int tokenNum = tokenize(input, tokens);

        // Ignore empty query
        if (tokenNum == 0)
            break;

        // Assign pipes
        dup2(pipeIn, STDIN_FILENO);
        dup2(pipeOut, STDOUT_FILENO);

        // Execute program
        execvp(*tokens, tokens);
    }
    }

    exit(0);
}

int main()
{
    nullPtr = open("/dev/null", O_WRONLY);

    static char input[512];
    printf("msh # ");
    while (gets(input))
    {
        int pid = parser(input, STDIN_FILENO, STDOUT_FILENO);
        waitpid(pid, NULL, 0);
        printf("msh # ");
    }
}
