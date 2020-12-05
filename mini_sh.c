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

#define WARN(msg, ...) fprintf(stderr, "minish : " msg "\n", ##__VA_ARGS__);

int nullFs;

/**
 * Check if `str` starts with `pre`
 */
int startsWith(const char *pre, const char *str)
{
    size_t lenpre = strlen(pre),
           lenstr = strlen(str);
    return lenstr < lenpre ? FALSE : memcmp(pre, str, lenpre) == 0;
}

/**
 * Find the first operation. |,& is perior to <,>.
*/
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
        case ';':
            SET_OP();
            break;
        case '|':
        case '&':
            if (*op != ';')
            {
                SET_OP();
            }
            break;
        case '<':
        case '>':
            if (*op != ';' && *op != '|' && *op != '&')
            {
                SET_OP();
            }
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
        char buf[512];                                 // Assign buffer
        for (int len; (len = read(fi, buf, 512)) > 0;) // Read until EOF
            write(fd, buf, len);                       // Write to stream
    }
    else
    {
        WARN("Could not open file %s.", path);
        exit(errno);
    }
    close(fi);
    return 0;
}

int parser(char *input, int pipeIn, int pipeOut)
{
#define CLOSE()                       \
    {                                 \
        if (pipeIn != STDIN_FILENO)   \
            close(pipeIn);            \
        if (pipeOut != STDOUT_FILENO) \
            close(pipeOut);           \
    }

    int subprocess = fork();
    if (subprocess > 0)
    {
        // Close useless pipes (Very important)
        CLOSE();
        return subprocess;
    }
    if (subprocess < 0)
    {
        WARN("Could not create subprocess.\n");
        exit(127);
    }

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
        int pip[2];                               // Define a pipe
        pipe(pip);                                // Initialize pipe
        int pid1 = parser(frnt, pipeIn, pip[1]);  // First part, read from stdin and write to pipe
        int pid2 = parser(back, pip[0], pipeOut); // Second part, read from pipe and writ to stdout
        waitpid(pid1, NULL, 0);                   // Wait until first(supplier) process finished
        waitpid(pid2, NULL, 0);
        break;
    }
    case '&':
    {
        int pid1 = parser(frnt, nullFs, pipeOut);
        int pid2 = parser(back, pipeIn, pipeOut);
        waitpid(pid2, NULL, 0);
        break;
    }
    case ';':
    {
        int pid1 = parser(frnt, nullFs, pipeOut);
        waitpid(pid1, NULL, 0);
        int pid2 = parser(back, pipeIn, pipeOut);
        waitpid(pid2, NULL, 0);
        break;
    }
    case '<':
    {
        close(pipeIn);
        char *tokens[512];
        if (tokenize(back, tokens))
        {
            int pip[2]; // Define pipes
            int pid1;
            int pid2;
            pipe(pip); // Initialize pipes
            {
                int pid1 = fork(); // Subprocess muse be used to properly close pipes.
                if (!pid1)
                {
                    streamFile(tokens[0], pip[1]); // Stream file to pipe
                    close(pip[0]);                 // Close useless pipes
                    close(pip[1]);                 //
                    exit(0);                       // Exit subprocess
                }
                else
                {
                    close(pip[1]); // Close subprocess in parent process
                }
            }
            {
                pid2 = parser(frnt, pip[0], pipeOut); // Run child process
                close(pip[1]);
            }
            waitpid(pid2, NULL, 0); // Wait until child process is finished
            waitpid(pid1, NULL, 0);
        }
        break;
    }
    case '>':
    {
        char *tokens[512];
        if (tokenize(back, tokens))
        {
            int fd = open(tokens[0], O_WRONLY | O_CREAT); // Open file
            if (fd > 0)
            {
                int pid = parser(frnt, pipeIn, fd); // Run child process
                close(pipeOut);
                waitpid(pid, NULL, 0); // Wait until child process is finished
                close(fd);             // Close unused file descriptor
            }
            else
            {
                WARN("Cannot open file");
            }
        }
        break;
    }
    default:
    {
        // Parse input and get arguments
        char *args[100];
        int tokenNum = tokenize(input, args);

        // Ignore empty query
        if (tokenNum == 0)
            break;

        // Assign pipes
        if (dup2(pipeIn, STDIN_FILENO) < 0)
        {
            WARN("Cannot duplicated input pipe");
            exit(errno);
        }
        if (dup2(pipeOut, STDOUT_FILENO) < 0)
        {
            WARN("Cannot duplicated output pipe");
            exit(errno);
        }

        // Close unused pipes
        CLOSE();

        // Execute program
        execvp(*args, args);
        WARN("Command not found");
        exit(127);
    }
    }
    exit(0);
}

int main()
{
    nullFs = open("/dev/null", O_WRONLY);
    char input[512];
    char *args[512];

#define CMD(compare) startsWith((compare), input)

    while (1)
    {
        printf("msh # ");
        fgets(input, 512, stdin);

        if (CMD("exit") || CMD("quit"))
        {
            break;
        }
        else if (CMD("cd"))
        {
            if (tokenize(input, args))
                chdir(args[1]);
        }
        else if (CMD("type"))
        {
            if (tokenize(input, args))
                streamFile(args[1], STDOUT_FILENO);
        }
        else
        {
            int pid = parser(input, STDIN_FILENO, STDOUT_FILENO);
            waitpid(pid, NULL, 0);
        }
    }

    return 0;
#undef CMD
}
