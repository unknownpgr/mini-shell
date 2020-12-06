#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define FALSE    0
#define TRUE     !FALSE
#define PID_NULL -1
#define PIPE_RD  0
#define PIPE_WR  1
#define BUF_LEN  512

#define WARN(msg, ...) fprintf(stderr, "minish : " msg "\n", ##__VA_ARGS__);

int nullFs;

/**
 * Find the first operator.
*/
void findOp(char *input, char *op, int *opPos)
{

    // Subprocess
    *op = -1;
    *opPos = -1;

#define SET_OP(cond)    \
    if (cond)           \
    {                   \
        *op = input[i]; \
        *opPos = i;     \
    }

    // Find operator position
    for (int i = 0; input[i]; i++)
    {
        switch (input[i])
        {
        case ';':
            SET_OP(TRUE);
            break;
        case '|':
        case '&':
            SET_OP(*op != ';');
            break;
        case '<':
        case '>':
            SET_OP(*op != ';' && *op != '|' && *op != '&');
            break;
        }
    }
#undef SET_OP
}

/**
 * Toknize given input string.
 * 1. Insert EOL('\0') at the end of each word
 * 2. Fill `tokens` array (last element is always NULL)
 * 3. Return the number of `tokens` array
 */
int tokenize(char *input, char **tokens)
{
#define IS_EMPTY(c) (((c) == ' ') || ((c) == '\t') || ((c) == '\r') || ((c) == '\n'))
    char pre = 1; // If previous character is whitespace
    char cur;     // If current characer is whitespace
    int tokenIndex = 0;
    for (int i = 0; input[i]; i++)
    {
        cur = IS_EMPTY(input[i]);
        if (pre && !cur)                      // If word starts at here
            tokens[tokenIndex++] = input + i; // Append this position into tokens
        else if (!pre && cur)                 // If word ends at here
            input[i] = '\0';                  // Insert EOL
        pre = cur;
    }
    tokens[tokenIndex] = NULL; // Fill last element 0
    return tokenIndex;         // Return the length of tokens array
#undef IS_EMPTY
}

/**
 * Read file from `path` and write it to `fd`
 */
int streamFile(char *path, int fd)
{
    int fi = open(path, O_RDONLY);
    if (fi >= 0)
    {
        char buf[BUF_LEN];                                 // Assign buffer
        for (int len; (len = read(fi, buf, BUF_LEN)) > 0;) // Read until EOF
            write(fd, buf, len);                           // Write to stream
    }
    else
    {
        WARN("Could not open file %s.", path);
        exit(errno);
    }
    close(fi);
    return 0;
}

/**
 * Parse input and run command with given pipeIn and pipeOut.
 * return pid of created subprocess
 * If subprocess not created(or command executed at host process), return PID_NULL.
 */
int parser(char *input, int pipeIn, int pipeOut)
{
    // Find operator and split string
    char op, *frnt, *back;
    {
        int opPos;
        findOp(input, &op, &opPos);
        input[opPos] = '\0';
        frnt = input;
        back = input + opPos + 1;
    }

    switch (op)
    {
    case '|':
    {
        int pip[2];                                     // Define a pipe
        pipe(pip);                                      // Initialize pipe
        int pid1 = parser(frnt, pipeIn, pip[PIPE_WR]);  // Process 1. Read from stdin and write to pipe
        close(pip[PIPE_WR]);                            // Close pipe. pip[PIPE_WR] must be closed here. After fork `pid2` process, It can not be closed.
        int pid2 = parser(back, pip[PIPE_RD], pipeOut); // Process 2. Read from pipe and writ to stdout
        close(pip[PIPE_RD]);
        return pid2;
    }
    case '&':
    {
        parser(frnt, nullFs, pipeOut);            // Process 1. Do not read and wrtie to stdut
        int pid2 = parser(back, pipeIn, pipeOut); // Process 2. Read from stdin and write to stdout
        return pid2;
    }
    case ';':
    {
        int pid1 = parser(frnt, pipeIn, pipeOut); // Process 1. Read from stdin and write to stdout
        waitpid(pid1, NULL, 0);                   // Wait until process 1 is finished
        int pid2 = parser(back, pipeIn, pipeOut); // Process 2. Read from stdin and write to stdout
        return pid2;
    }
    case '<':
    {
        char *tokens[BUF_LEN];
        if (!tokenize(back, tokens))
            return PID_NULL;

        int pip[2];        // Define pipes
        pipe(pip);         // Initialize pipes
        int pid1 = fork(); // Subprocess muse be used to properly close pipes.
        if (!pid1)
        {
            streamFile(tokens[0], pip[PIPE_WR]); // Stream file to pipe
            close(pip[PIPE_RD]);                 // Close pipes both pipe at child process
            close(pip[PIPE_WR]);                 //
            exit(0);                             // Exit subprocess
        }
        close(pip[PIPE_WR]);                            // Close pipe 1 at host process. Therefore, only pip[PIPE_RD] is alive.
        int pid2 = parser(frnt, pip[PIPE_RD], pipeOut); // Run child process
        close(pip[PIPE_RD]);                            // Close pip[PIPE_RD]
        return pid2;
    }
    case '>':
    {
        char *tokens[BUF_LEN];
        if (!tokenize(back, tokens))
            return PID_NULL;

        int fd = open(tokens[0], O_CREAT | O_RDWR); // Open file to write output
        if (fd > 0)
        {
            int pid = parser(frnt, pipeIn, fd); // Run child process
            close(fd);                          // Close unused file descriptor
            return pid;
        }
        else
        {
            WARN("Cannot open file");
        }
        close(fd);
        return PID_NULL;
    }
    default:
    {
        // Parse input and get arguments
        char *args[BUF_LEN];
        int tokenNum = tokenize(input, args);

        // Ignore empty command
        if (tokenNum == 0)
            return PID_NULL;

#define CMD(compare) (!strcmp((compare), args[0]))

        if (CMD("exit") || CMD("quit"))
            exit(0);

        if (CMD("cd"))
        {
            if (tokenNum > 1)
                chdir(args[1]);
            return PID_NULL;
        }

        if (CMD("type"))
        {
            if (tokenNum < 2)
                return PID_NULL;

            int pid = fork();
            if (pid)
                return pid;

            streamFile(args[1], pipeOut);
            exit(0);
        }

#undef CMD

        int pid = fork();
        if (pid)
            return pid;

        // Assign pipes
        dup2(pipeIn, STDIN_FILENO);
        dup2(pipeOut, STDOUT_FILENO);

        // Close pipes. Avoid to close stdin/out, check it.
        if (pipeIn != STDIN_FILENO)
            close(pipeIn);
        if (pipeOut != STDOUT_FILENO)
            close(pipeOut);

        // Execute program
        execvp(*args, args);
        WARN("Command not found");
        exit(127);
    }
    }
    return PID_NULL;
}

int main()
{
    nullFs = open("/dev/null", O_WRONLY); // Null file descriptor to ignore some input
    char input[BUF_LEN];                  // User input buffer

    while (1)
    {
        printf("msh # ");                                     // Print shell text
        fgets(input, BUF_LEN, stdin);                         // Read user input
        int pid = parser(input, STDIN_FILENO, STDOUT_FILENO); // Parse and process
        waitpid(pid, NULL, 0);                                // Wait until progess ends
    }

    return 0;
}
