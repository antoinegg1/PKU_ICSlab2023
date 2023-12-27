/* 
 * tsh - A tiny shell program with job control
 * 
* Description:
 *   This shell program provides basic functionality similar to traditional shells.
 *   It supports various built-in commands such as quit, jobs, bg, fg, kill, and nohup with a Central State Output Server.
 *   Using sio_put series to replace printf and realizing the redirection function.
 *   Additionally, it manages signals, handles job control, and facilitates process management.
 *   The shell operates by parsing user commands, forking processes to execute them, and
 *   managing foreground and background jobs.
 *
 * <Put your name and login ID here>
 */
#include <assert.h>
#include <bits/types/sigset_t.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <errno.h>
#include <stdarg.h>

/* Misc manifest constants */
#define MAXLINE    1024   /* max line size */
#define MAXARGS     128   /* max args on a command line */
#define MAXJOBS      16   /* max jobs at any point in time */
#define MAXJID    1<<16   /* max job ID */

/* Job states */
#define UNDEF         0   /* undefined */
#define FG            1   /* running in foreground */
#define BG            2   /* running in background */
#define ST            3   /* stopped */

/* 
 * Jobs states: FG (foreground), BG (background), ST (stopped)
 * Job state transitions and enabling actions:
 *     FG -> ST  : ctrl-z
 *     ST -> FG  : fg command
 *     ST -> BG  : bg command
 *     BG -> FG  : fg command
 * At most 1 job can be in the FG state.
 */

/* Parsing states */
#define ST_NORMAL   0x0   /* next token is an argument */
#define ST_INFILE   0x1   /* next token is the input file */
#define ST_OUTFILE  0x2   /* next token is the output file */

/* Global variables */
extern char **environ;      /* defined in libc */
char prompt[] = "tsh> ";    /* command line prompt (DO NOT CHANGE) */
int verbose = 0;            /* if true, print additional output */
int nextjid = 1;            /* next job ID to allocate */
char sbuf[MAXLINE];         /* for composing sprintf messages */

struct job_t {              /* The job struct */
    pid_t pid;              /* job PID */
    int jid;                /* job ID [1, 2, ...] */
    int state;              /* UNDEF, BG, FG, or ST */
    char cmdline[MAXLINE];  /* command line */
};
struct job_t job_list[MAXJOBS]; /* The job list */

struct cmdline_tokens {
    int argc;               /* Number of arguments */
    char *argv[MAXARGS];    /* The arguments list */
    char *infile;           /* The input file */
    char *outfile;          /* The output file */
    enum builtins_t {       /* Indicates if argv[0] is a builtin command */
        BUILTIN_NONE,
        BUILTIN_QUIT,
        BUILTIN_JOBS,
        BUILTIN_BG,
        BUILTIN_FG,
        BUILTIN_KILL,
        BUILTIN_NOHUP} builtins;
};

/* End global variables */

/* Function prototypes */
void eval(char *cmdline);
void sigchld_handler(int sig);
void sigtstp_handler(int sig);
void sigint_handler(int sig);
/* Here are helper routines that we've provided for you */
int parseline(const char *cmdline, struct cmdline_tokens *tok); 
void sigquit_handler(int sig);

void clearjob(struct job_t *job);
void initjobs(struct job_t *job_list);
int maxjid(struct job_t *job_list); 
int addjob(struct job_t *job_list, pid_t pid, int state, char *cmdline);
int deletejob(struct job_t *job_list, pid_t pid); 
pid_t fgpid(struct job_t *job_list);
struct job_t *getjobpid(struct job_t *job_list, pid_t pid);
struct job_t *getjobjid(struct job_t *job_list, int jid); 
int pid2jid(pid_t pid); 
void listjobs(struct job_t *job_list, int output_fd);

void usage(void);
void unix_error(char *msg);
void app_error(char *msg);
ssize_t sio_puts(char s[]);
ssize_t sio_putl(long v);
ssize_t sio_put(const char *fmt, ...);
void sio_error(char s[]);

typedef void handler_t(int);
handler_t *Signal(int signum, handler_t *handler);

//Function defined by the
// Function to execute built-in commands (jobs, bg, fg, kill, nohup)
int buildtin_command(struct cmdline_tokens *token); 
// Wrapper function for fork() to handle errors
pid_t Fork(void);
// Function to execute the 'bg' command
void bgpro(struct cmdline_tokens* token); 
// Function to execute the 'fg' command
void fgpro(struct cmdline_tokens* token); 
// Function for Central State Output Server (CSOS)
void CSOS(struct cmdline_tokens* token, int ntype); 
// Function to handle redirection in command execution
int redirection(struct cmdline_tokens* token); 
// Function to execute the 'nohup' command
void nohope(struct cmdline_tokens* token); 
// Function to execute the 'kill' command
void killing(struct cmdline_tokens* token); 
/*
 * main - The shell's main routine 
 */
int 
main(int argc, char **argv) 
{
    char c;
    char cmdline[MAXLINE];    /* cmdline for fgets */
    int emit_prompt = 1; /* emit prompt (default) */

    /* Redirect stderr to stdout (so that driver will get all output
     * on the pipe connected to stdout) */
    dup2(1, 2);

    /* Parse the command line */
    while ((c = getopt(argc, argv, "hvp")) != EOF) {
        switch (c) {
        case 'h':             /* print help message */
            usage();
            break;
        case 'v':             /* emit additional diagnostic info */
            verbose = 1;
            break;
        case 'p':             /* don't print a prompt */
            emit_prompt = 0;  /* handy for automatic testing */
            break;
        default:
            usage();
        }
    }

    /* Install the signal handlers */

    /* These are the ones you will need to implement */
    Signal(SIGINT,  sigint_handler);   /* ctrl-c */
    Signal(SIGTSTP, sigtstp_handler);  /* ctrl-z */
    Signal(SIGCHLD, sigchld_handler);  /* Terminated or stopped child */
    Signal(SIGTTIN, SIG_IGN);
    Signal(SIGTTOU, SIG_IGN);

    /* This one provides a clean way to kill the shell */
    Signal(SIGQUIT, sigquit_handler); 

    /* Initialize the job list */
    initjobs(job_list);

    /* Execute the shell's read/eval loop */
    while (1) {
        //read
        if (emit_prompt) {
            printf("%s", prompt);
            fflush(stdout);
        }
        if ((fgets(cmdline, MAXLINE, stdin) == NULL) && ferror(stdin))
            app_error("fgets error");
        if (feof(stdin)) { 
            /* End of file (ctrl-d) */
            printf ("\n");
            fflush(stdout);
            fflush(stderr);
            exit(0);
        }
        
        /* Remove the trailing newline */
        cmdline[strlen(cmdline)-1] = '\0';
        
        /* Evaluate the command line */
        eval(cmdline);
        
        fflush(stdout);
        fflush(stdout);
    } 
    
    exit(0); /* control never reaches here */
}

/* 
 * eval - Evaluate the command line that the user has just typed in
 * 
 * If the user has requested a built-in command (quit, jobs, bg or fg)
 * then execute it immediately. Otherwise, fork a child process and
 * run the job in the context of the child. If the job is running in
 * the foreground, wait for it to terminate and then return.  Note:
 * each child process must have a unique process group ID so that our
 * background children don't receive SIGINT (SIGTSTP) from the kernel
 * when we type ctrl-c (ctrl-z) at the keyboard.  
 */
void eval(char *cmdline) {
    int bg;                         // Variable to determine whether the job runs in the background or foreground
    struct cmdline_tokens token;    // Structure to hold parsed command line tokens
    pid_t pid;                      // Process ID

    bg = parseline(cmdline, &token);    // Parse the command line and get whether the job should be run in the background

    if (bg == -1)   // Check for parsing errors
        CSOS(&token, 2);

    if (token.argv[0] == NULL)   // Ignore empty lines
        CSOS(&token, 3);

    sigset_t mask, prev_mask;     // Define signal masks for handling signals
    sigemptyset(&mask);           // Initialize the signal mask

    if (!buildtin_command(&token)) {    // If the command is not a built-in command
        sigaddset(&mask, SIGCHLD);     // Add SIGCHLD to the signal mask
        sigaddset(&mask, SIGTSTP);     // Add SIGTSTP to the signal mask
        sigaddset(&mask, SIGINT);      // Add SIGINT to the signal mask

        sigprocmask(SIG_BLOCK, &mask, &prev_mask);   // Block the signals

        if ((pid = Fork()) == 0) {  // Fork a child process to execute the user's command
            sigprocmask(SIG_SETMASK, &prev_mask, NULL);  // Unblock signals in the child process

            setpgid(0, 0);  // Set the process group ID

            if (redirection(&token)) {  // Handle input/output redirection
                execve(token.argv[0], token.argv, environ);  // Execute the command
                CSOS(&token, 0);  // Handle execution error
            } 
            else {
                if (execve(token.argv[0], token.argv, environ) < 0) {
                    CSOS(&token, 0);  // Handle execution error
                }
            }
        }

        addjob(job_list, pid, (bg ? BG : FG), cmdline);  // Add the job to the job list

        sigprocmask(SIG_SETMASK, &prev_mask, NULL);  // Unblock signals

        if (!bg) {
            // Wait for foreground process to finish
            while (pid == fgpid(job_list)) {
                sigsuspend(&prev_mask);  // Suspend until the foreground process ends
            }
        } 
        else {
            sio_puts("[");
            sio_putl(pid2jid(pid));
            sio_puts("] (");
            sio_putl(pid);
            sio_puts(") ");
            sio_puts(cmdline);  // Print background process information
        }
    }

    return;
}


/* 
 * parseline - Parse the command line and build the argv array.
 * 
 * Parameters:
 *   cmdline:  The command line, in the form:
 *
 *                command [arguments...] [< infile] [> oufile] [&]
 *
 *   tok:      Pointer to a cmdline_tokens structure. The elements of this
 *             structure will be populated with the parsed tokens. Characters 
 *             enclosed in single or double quotes are treated as a single
 *             argument. 
 * Returns:
 *   1:        if the user has requested a BG job
 *   0:        if the user has requested a FG job  
 *  -1:        if cmdline is incorrectly formatted
 * 
 * Note:       The string elements of tok (e.g., argv[], infile, outfile) 
 *             are statically allocated inside parseline() and will be 
 *             overwritten the next time this function is invoked.
 */
int 
parseline(const char *cmdline, struct cmdline_tokens *tok) 
{

    static char array[MAXLINE];          /* holds local copy of command line */
    const char delims[10] = " \t\r\n";   /* argument delimiters (white-space) */
    char *buf = array;                   /* ptr that traverses command line */
    char *next;                          /* ptr to the end of the current arg */
    char *endbuf;                        /* ptr to end of cmdline string */
    int is_bg;                           /* background job? */
             
    int parsing_state;                   /* indicates if the next token is the
                                            input or output file */

    if (cmdline == NULL) {
        (void) fprintf(stderr, "Error: command line is NULL\n");
        return -1;
    }

    (void) strncpy(buf, cmdline, MAXLINE);
    endbuf = buf + strlen(buf);

    tok->infile = NULL;
    tok->outfile = NULL;

    /* Build the argv list */
    parsing_state = ST_NORMAL;
    tok->argc = 0;

    while (buf < endbuf) {
        /* Skip the white-spaces */
        buf += strspn (buf, delims);
        if (buf >= endbuf) break;

        /* Check for I/O redirection specifiers */
        if (*buf == '<') {
            if (tok->infile) {
                (void) fprintf(stderr, "Error: Ambiguous I/O redirection\n");
                return -1;
            }
            parsing_state |= ST_INFILE;
            buf++;
            continue;
        }
        if (*buf == '>') {
            if (tok->outfile) {
                (void) fprintf(stderr, "Error: Ambiguous I/O redirection\n");
                return -1;
            }
            parsing_state |= ST_OUTFILE;
            buf ++;
            continue;
        }

        if (*buf == '\'' || *buf == '\"') {
            /* Detect quoted tokens */
            buf++;
            next = strchr (buf, *(buf-1));
        } else {
            /* Find next delimiter */
            next = buf + strcspn (buf, delims);
        }
        
        if (next == NULL) {
            /* Returned by strchr(); this means that the closing
               quote was not found. */
            (void) fprintf (stderr, "Error: unmatched %c.\n", *(buf-1));
            return -1;
        }

        /* Terminate the token */
        *next = '\0';

        /* Record the token as either the next argument or the i/o file */
        switch (parsing_state) {
        case ST_NORMAL:
            tok->argv[tok->argc++] = buf;
            break;
        case ST_INFILE:
            tok->infile = buf;
            break;
        case ST_OUTFILE:
            tok->outfile = buf;
            break;
        default:
            (void) fprintf(stderr, "Error: Ambiguous I/O redirection\n");
            return -1;
        }
        parsing_state = ST_NORMAL;

        /* Check if argv is full */
        if (tok->argc >= MAXARGS-1) break;

        buf = next + 1;
    }

    if (parsing_state != ST_NORMAL) {
        (void) fprintf(stderr,
                       "Error: must provide file name for redirection\n");
        return -1;
    }

    /* The argument list must end with a NULL pointer */
    tok->argv[tok->argc] = NULL;

    if (tok->argc == 0)  /* ignore blank line */
        return 1;

    if (!strcmp(tok->argv[0], "quit")) {                 /* quit command */
        tok->builtins = BUILTIN_QUIT;
    } else if (!strcmp(tok->argv[0], "jobs")) {          /* jobs command */
        tok->builtins = BUILTIN_JOBS;
    } else if (!strcmp(tok->argv[0], "bg")) {            /* bg command */
        tok->builtins = BUILTIN_BG;
    } else if (!strcmp(tok->argv[0], "fg")) {            /* fg command */
        tok->builtins = BUILTIN_FG;
    } else if (!strcmp(tok->argv[0], "kill")) {          /* kill command */
        tok->builtins = BUILTIN_KILL;
    } else if (!strcmp(tok->argv[0], "nohup")) {            /* kill command */
        tok->builtins = BUILTIN_NOHUP;
    } else {
        tok->builtins = BUILTIN_NONE;
    }

    /* Should the job run in the background? */
    if ((is_bg = (*tok->argv[tok->argc-1] == '&')) != 0)
        tok->argv[--tok->argc] = NULL;

    return is_bg;
}


/*
 * buildtin_command - Execute built-in commands if the first argument is a built-in command
 *
 * This function checks the 'builtins' field of the 'cmdline_tokens' structure
 * to determine if the provided command is a built-in command. If it's a built-in command,
 * it executes the corresponding action for that command. If not, it returns 0.
 *
 * Arguments:
 *   - token: Pointer to the 'cmdline_tokens' structure containing command details
 *
 * Returns:
 *   - 1 if the command is a built-in command and executed successfully
 *   - 0 if the command is not a built-in command
 */
int buildtin_command(struct cmdline_tokens *token) {
    switch (token->builtins) {
        case BUILTIN_QUIT:
            exit(0);
        case BUILTIN_JOBS:
            if (token->outfile != NULL) {
                return 0;
            }
            listjobs(job_list, STDOUT_FILENO);
            return 1;
        case BUILTIN_BG:
            bgpro(token);
            return 1;
        case BUILTIN_FG:
            fgpro(token);
            return 1;
        case BUILTIN_KILL:
            killing(token);
            return 1;
        case BUILTIN_NOHUP:
            nohope(token);
            return 1;
        default:
            return 0; // Not a built-in command
    }
    return 0; // Default case, not a built-in command
}

/*
 * Fork - Wrapper function for the fork system call
 *
 * This function calls the fork system call and checks for errors.
 * If the fork call fails (returns a value less than 0), it triggers
 * the 'CSOS' function with an error code '1' to handle the error.
 *
 * Returns:
 *   - The process ID (pid) of the child process to the parent process
 *   - 0 to the child process
 *   - Returns -1 if an error occurred
 */
pid_t Fork(void) {
    pid_t pid;

    // Call fork system call and check for errors
    if ((pid = fork()) < 0) {
        // Handle fork error by invoking CSOS function with error code '1'
        CSOS(NULL, 1);
    }
    
    // Return the process ID (pid) to parent process or child process
    return pid;
}
/*
 * bgpro - Move a specified job to run in the background
 *
 * If the argument is a job ID, it retrieves the corresponding job from the job list
 * and continues its execution by sending a SIGCONT signal. If it's a process ID,
 * it retrieves the corresponding job based on the ID. It then sets the state of
 * the job to indicate that it's running in the background (BG).
 * This function doesn't wait for the job to complete, it immediately moves
 * the specified job to the background for execution.
 */

void bgpro(struct cmdline_tokens* token) {
    char *ID = token->argv[1];
    pid_t pid;
    struct job_t *job = NULL;
    // Check if the argument begins with '%' to determine whether it's a job ID or process ID
    if (ID[0] == '%') {
        // Get the job corresponding to the provided job ID
        job = getjobjid(job_list, atoi(ID + 1));
        pid = job->pid;
    } 
    else {
        // Get the job corresponding to the provided process ID
        pid = atoi(ID);
        job = getjobpid(job_list, pid);
    }
    // Send a SIGCONT signal to the process or job to continue its execution
    kill(-pid, SIGCONT);
    // Set the job's state to indicate it's running in the background
    job->state = BG;
    // Print the job information (job ID, process ID, command line)
    sio_puts("[");
    sio_putl(pid2jid(pid));
    sio_puts("] (");
    sio_putl(pid);
    sio_puts(") ");
    sio_puts(job->cmdline);
    return;
}
/*
 * fgpro - Bring a specified job to the foreground
 *
 * If the argument is a job ID, it retrieves the corresponding job from the job list
 * and continues its execution by sending a SIGCONT signal. If it's a process ID,
 * it retrieves the corresponding job based on the ID. It then sets the state of
 * the job to indicate that it's running in the foreground (FG).
 * The function enters a loop, suspending the shell until the foreground process
 * completes, identified by the condition 'pid == fgpid(job_list)'.
 * This ensures the shell waits until the specified foreground job finishes.
 */
void fgpro(struct cmdline_tokens* token) {
    char *ID = token->argv[1];
    pid_t pid;
    struct job_t *job;
    sigset_t prev_mask;
    // Determine if the argument is a job ID or process ID based on '%' prefix
    if (ID[0] == '%') {
        // Get the job corresponding to the provided job ID
        job = getjobjid(job_list, atoi(ID + 1));
        pid = job->pid;
    } 
    else {
        // Get the job corresponding to the provided process ID
        pid = atoi(ID);
        job = getjobpid(job_list, pid);
    }
    // Send a SIGCONT signal to continue the specified process or job
    kill(-pid, SIGCONT);
    // Set the job's state to indicate it's running in the foreground
    job->state = FG;
    // Wait for the foreground process to finish
    while (pid == fgpid(job_list)) {
        sigsuspend(&prev_mask); // Suspend until the process finishes
    }
    return;
}
/*
 * CSOS - Handle error messages for shell commands
 *
 * This function takes a 'cmdline_tokens' structure and an 'ntype' indicating
 * the type of error encountered during command execution. It switches based
 * on the error type and prints an appropriate error message to the console.
 * After printing the error message, it exits the shell with exit code 0.
 *
 * Arguments:
 *   - token: Pointer to the 'cmdline_tokens' structure containing command details
 *   - ntype: Integer representing the type of error encountered (0 to 3)
 */
void CSOS(struct cmdline_tokens* token,int ntype){
    switch(ntype){
        case 0: {
            // Error type 0: Command not found
            sio_puts(token->argv[0]);
            sio_puts(": Command not found.\n");
            break;
        }
        case 1: {
            // Error type 1: Fork error
            sio_puts("Fork error");
            break;
        }
        case 2: {
            // Error type 2: Parsing error
            sio_puts("Parsing error");
            break;
        }
        case 3: {
            // Error type 3: Empty lines
            sio_puts("Empty lines");
            break;
        }
    }
    exit(0);
}
/*
 * redirection - Handle input/output redirection for a command
 *
 * This function takes a 'cmdline_tokens' structure containing input/output file names
 * and performs file redirection for the command if necessary. If an input file is specified,
 * it opens the file for reading and redirects stdin to that file descriptor. If an output
 * file is specified, it opens the file for writing and redirects stdout to that file descriptor.
 * It returns 1 if output redirection occurs, else returns 0.
 *
 * Arguments:
 *   - token: Pointer to the 'cmdline_tokens' structure containing command details
 *
 * Returns:
 *   - 1 if output redirection occurs, 0 otherwise
 */
int redirection(struct cmdline_tokens* token){
    int f1, f2;
    int ret = 0;
    // Check if input file is specified
    if (token->infile != NULL) {
        // Open the input file for reading
        f1 = open(token->infile, O_RDONLY);
        // Redirect stdin to the input file descriptor
        dup2(f1, STDIN_FILENO);
        // Close the file descriptor
        close(f1);
    }

    // Check if output file is specified
    if (token->outfile != NULL) {
        // Open the output file for writing
        f2 = open(token->outfile, O_WRONLY);
        // Redirect stdout to the output file descriptor
        dup2(f2, STDOUT_FILENO);
        // Close the file descriptor
        close(f2);
        ret = 1; // Set ret to 1 indicating output redirection occurred
    }

    return ret; // Return 1 if output redirection occurs, else return 0
}
/*
 * nohope - Execute a command, ignoring SIGHUP and redirecting output
 *          for the 'nohup' command
 *
 * This function forks a child process to execute the specified command
 * with 'nohup'. In the child process, it ignores the SIGHUP signal,
 * redirects standard output and error to the file named "nohup.out",
 * and redirects standard input to '/dev/null'. Then it executes the
 * user-specified command using 'execve'.
 *
 * Arguments:
 *   - token: Pointer to the 'cmdline_tokens' structure containing command details
 */
void nohope(struct cmdline_tokens* token) {
    pid_t pid = fork(); // Fork a child process

    if (pid == 0) {
        // Child process
        // Ignore SIGHUP signal in the child process
        signal(SIGHUP, SIG_IGN);

        // Create or append to the "nohup.out" output file
        int output_fd = open("nohup.out", O_WRONLY | O_CREAT | O_APPEND, 0666);

        // Redirect standard output and error to the output file descriptor
        dup2(output_fd, STDOUT_FILENO);
        dup2(output_fd, STDERR_FILENO);
        
        // Redirect standard input to /dev/null (if required)
        dup2(open("/dev/null", O_RDONLY), STDIN_FILENO);

        close(output_fd); // Close the file descriptor

        // Execute the user-specified command
        execve(token->argv[0], token->argv, environ);
    }
}
/*
 * killing - Send a termination signal to a specified process or process group
 *
 * This function takes a 'cmdline_tokens' structure and sends a termination signal
 * (SIGTERM) to a specified process or process group ID. It determines whether the
 * provided argument represents a process or process group and sends the SIGTERM
 * signal accordingly to terminate the specified process(es).
 *
 * Arguments:
 *   - token: Pointer to the 'cmdline_tokens' structure containing command details
 */
void killing(struct cmdline_tokens* token) {
    pid_t pid = 0;
    int jid = 0;
    struct job_t *job = NULL;
    int isGroup = 0;

    /* Get the process or process group ID */
    if (token->argv[1][0] == '%') {
        jid = atoi(token->argv[1] + 1);
        isGroup = jid < 0;
        job = getjobjid(job_list, jid > 0 ? jid : -jid);

        // Check if job or process group exists
        if (!job) {
           if (isGroup) {
                sio_puts("%");
                sio_putl(jid);
                sio_puts(": No such process group\n");
            } 
            else {
                sio_puts("%");
                sio_putl(jid);
                sio_puts(": No such job\n");
            }
            return;
        }
        pid = isGroup ? -job->pid : job->pid;
    } 
    else {
        pid = atoi(token->argv[1]);
        isGroup = pid < 0;

        // Check if process exists
        if (!getjobpid(job_list, jid > 0 ? jid : -jid)){
            if (isGroup) {
                sio_puts("%");
                sio_putl(jid);
                sio_puts(": No such process group\n");
            } 
            else {
                sio_puts("%");
                sio_putl(jid);
                sio_puts(": No such job\n");
            }
            return;
        }
    }

    kill(pid, SIGTERM); /* Send SIGTERM signal to specified process or process group */
}

/*****************
 * Signal handlers
 *****************/

/* 
 * sigchld_handler - The kernel sends a SIGCHLD to the shell whenever
 *     a child job terminates (becomes a zombie), or stops because it
 *     received a SIGSTOP, SIGTSTP, SIGTTIN or SIGTTOU signal. The 
 *     handler reaps all available zombie children, but doesn't wait 
 *     for any other currently running children to terminate.  
 */
void sigchld_handler(int sig) {
    pid_t pid;
    int old_errno = errno;
    int status;
    sigset_t mask_all, mask_previous;
    sigfillset(&mask_all);

    while ((pid = waitpid(-1, &status, WNOHANG | WUNTRACED | WCONTINUED)) > 0) {
        sigprocmask(SIG_BLOCK, &mask_all, &mask_previous);

        if (WIFEXITED(status)) {
            // Child process exited normally
            deletejob(job_list, pid);
        } 
        else if (WIFCONTINUED(status)) {
            // Child process was continued
            struct job_t *job = getjobpid(job_list, pid);
            job->state = BG;
        } 
        else if (WIFSTOPPED(status)) {
            // Child process was stopped
            sio_puts("Job [");
            sio_putl(pid2jid(pid));
            sio_puts("] (");
            sio_putl(pid);
            sio_puts(") stopped by signal 20\n");
            getjobpid(job_list, pid)->state = ST;
        } 
        else if (WIFSIGNALED(status)) {
            // Child process terminated by a signal
            if (WTERMSIG(status) == SIGHUP) {
                // Child process terminated by SIGHUP signal
                sio_puts("Job [");
                sio_putl(pid2jid(pid));
                sio_puts("] (");
                sio_putl(pid);
                sio_puts(") terminated by signal 1\n");
                deletejob(job_list, pid);
            } 
            else {
                // Child process terminated by other signal
                sio_puts("Job [");
                sio_putl(pid2jid(pid));
                sio_puts("] (");
                sio_putl(pid);
                sio_puts(") terminated by signal 2\n");
                deletejob(job_list, pid);
            }
        }

        sigprocmask(SIG_SETMASK, &mask_previous, NULL);
    }

    if (pid != 0 && errno != ECHILD) {
        // Handle errors if waitpid fails
        sio_puts("waitpid ");
        sio_putl(pid);
        sio_puts(" error, errno is ");
        sio_putl(errno);
        sio_puts("\n");
    }

    errno = old_errno;
}

/* 
 * sigint_handler - The kernel sends a SIGINT to the shell whenver the
 *    user types ctrl-c at the keyboard.  Catch it and send it along
 *    to the foreground job.  
 */
void sigint_handler(int sig) {
    // Get the process ID of the foreground job
    pid_t pid = fgpid(job_list);

    // Send SIGINT signal to the process group
    kill(-pid, sig);
    return;
}

/*
 * sigtstp_handler - The kernel sends a SIGTSTP to the shell whenever
 *     the user types ctrl-z at the keyboard. Catch it and suspend the
 *     foreground job by sending it a SIGTSTP.  
 */
void sigtstp_handler(int sig) {
    // Get the process ID of the foreground job
    pid_t pid = fgpid(job_list);

    // Send SIGTSTP signal to the process group
    kill(-pid, sig);
    return;
}

/*
 * sigquit_handler - The driver program can gracefully terminate the
 *    child shell by sending it a SIGQUIT signal.
 */
void 
sigquit_handler(int sig) 
{
    sio_error("Terminating after receipt of SIGQUIT signal\n");
}


/*********************
 * End signal handlers
 *********************/

/***********************************************
 * Helper routines that manipulate the job list
 **********************************************/

/* clearjob - Clear the entries in a job struct */
void 
clearjob(struct job_t *job) {
    job->pid = 0;
    job->jid = 0;
    job->state = UNDEF;
    job->cmdline[0] = '\0';
}

/* initjobs - Initialize the job list */
void 
initjobs(struct job_t *job_list) {
    int i;

    for (i = 0; i < MAXJOBS; i++)
        clearjob(&job_list[i]);
}

/* maxjid - Returns largest allocated job ID */
int 
maxjid(struct job_t *job_list) 
{
    int i, max=0;

    for (i = 0; i < MAXJOBS; i++)
        if (job_list[i].jid > max)
            max = job_list[i].jid;
    return max;
}

/* addjob - Add a job to the job list */
int 
addjob(struct job_t *job_list, pid_t pid, int state, char *cmdline) 
{
    int i;

    if (pid < 1)
        return 0;

    for (i = 0; i < MAXJOBS; i++) {
        if (job_list[i].pid == 0) {
            job_list[i].pid = pid;
            job_list[i].state = state;
            job_list[i].jid = nextjid++;
            if (nextjid > MAXJOBS)
                nextjid = 1;
            strcpy(job_list[i].cmdline, cmdline);
            if(verbose){
                printf("Added job [%d] %d %s\n",
                       job_list[i].jid,
                       job_list[i].pid,
                       job_list[i].cmdline);
            }
            return 1;
        }
    }
    printf("Tried to create too many jobs\n");
    return 0;
}

/* deletejob - Delete a job whose PID=pid from the job list */
int 
deletejob(struct job_t *job_list, pid_t pid) 
{
    int i;

    if (pid < 1)
        return 0;

    for (i = 0; i < MAXJOBS; i++) {
        if (job_list[i].pid == pid) {
            clearjob(&job_list[i]);
            nextjid = maxjid(job_list)+1;
            return 1;
        }
    }
    return 0;
}

/* fgpid - Return PID of current foreground job, 0 if no such job */
pid_t 
fgpid(struct job_t *job_list) {
    int i;

    for (i = 0; i < MAXJOBS; i++)
        if (job_list[i].state == FG)
            return job_list[i].pid;
    return 0;
}

/* getjobpid  - Find a job (by PID) on the job list */
struct job_t 
*getjobpid(struct job_t *job_list, pid_t pid) {
    int i;

    if (pid < 1)
        return NULL;
    for (i = 0; i < MAXJOBS; i++)
        if (job_list[i].pid == pid)
            return &job_list[i];
    return NULL;
}

/* getjobjid  - Find a job (by JID) on the job list */
struct job_t *getjobjid(struct job_t *job_list, int jid) 
{
    int i;

    if (jid < 1)
        return NULL;
    for (i = 0; i < MAXJOBS; i++)
        if (job_list[i].jid == jid)
            return &job_list[i];
    return NULL;
}

/* pid2jid - Map process ID to job ID */
int 
pid2jid(pid_t pid) 
{
    int i;

    if (pid < 1)
        return 0;
    for (i = 0; i < MAXJOBS; i++)
        if (job_list[i].pid == pid) {
            return job_list[i].jid;
        }
    return 0;
}

/* listjobs - Print the job list */
void 
listjobs(struct job_t *job_list, int output_fd) 
{
    int i;
    char buf[MAXLINE << 2];

    for (i = 0; i < MAXJOBS; i++) {
        memset(buf, '\0', MAXLINE);
        if (job_list[i].pid != 0) {
            sprintf(buf, "[%d] (%d) ", job_list[i].jid, job_list[i].pid);
            if(write(output_fd, buf, strlen(buf)) < 0) {
                fprintf(stderr, "Error writing to output file\n");
                exit(1);
            }
            memset(buf, '\0', MAXLINE);
            switch (job_list[i].state) {
            case BG:
                sprintf(buf, "Running    ");
                break;
            case FG:
                sprintf(buf, "Foreground ");
                break;
            case ST:
                sprintf(buf, "Stopped    ");
                break;
            default:
                sprintf(buf, "listjobs: Internal error: job[%d].state=%d ",
                        i, job_list[i].state);
            }
            if(write(output_fd, buf, strlen(buf)) < 0) {
                fprintf(stderr, "Error writing to output file\n");
                exit(1);
            }
            memset(buf, '\0', MAXLINE);
            sprintf(buf, "%s\n", job_list[i].cmdline);
            if(write(output_fd, buf, strlen(buf)) < 0) {
                fprintf(stderr, "Error writing to output file\n");
                exit(1);
            }
        }
    }
}
/******************************
 * end job list helper routines
 ******************************/


/***********************
 * Other helper routines
 ***********************/

/*
 * usage - print a help message
 */
void 
usage(void) 
{
    printf("Usage: shell [-hvp]\n");
    printf("   -h   print this message\n");
    printf("   -v   print additional diagnostic information\n");
    printf("   -p   do not emit a command prompt\n");
    exit(1);
}

/*
 * unix_error - unix-style error routine
 */
void 
unix_error(char *msg)
{
    fprintf(stdout, "%s: %s\n", msg, strerror(errno));
    exit(1);
}

/*
 * app_error - application-style error routine
 */
void 
app_error(char *msg)
{
    fprintf(stdout, "%s\n", msg);
    exit(1);
}

/* Private sio_functions */
/* sio_reverse - Reverse a string (from K&R) */
static void sio_reverse(char s[])
{
    int c, i, j;

    for (i = 0, j = strlen(s)-1; i < j; i++, j--) {
        c = s[i];
        s[i] = s[j];
        s[j] = c;
    }
}

/* sio_ltoa - Convert long to base b string (from K&R) */
static void sio_ltoa(long v, char s[], int b) 
{
    int c, i = 0;
    
    do {  
        s[i++] = ((c = (v % b)) < 10)  ?  c + '0' : c - 10 + 'a';
    } while ((v /= b) > 0);
    s[i] = '\0';
    sio_reverse(s);
}

/* sio_strlen - Return length of string (from K&R) */
static size_t sio_strlen(const char s[])
{
    int i = 0;

    while (s[i] != '\0')
        ++i;
    return i;
}

/* sio_copy - Copy len chars from fmt to s (by Ding Rui) */
void sio_copy(char *s, const char *fmt, size_t len)
{
    if(!len)
        return;

    for(size_t i = 0;i < len;i++)
        s[i] = fmt[i];
}

/* Public Sio functions */
ssize_t sio_puts(char s[]) /* Put string */
{
    return write(STDOUT_FILENO, s, sio_strlen(s));
}

ssize_t sio_putl(long v) /* Put long */
{
    char s[128];
    
    sio_ltoa(v, s, 10); /* Based on K&R itoa() */ 
    return sio_puts(s);
}

ssize_t sio_put(const char *fmt, ...) // Put to the console. only understands %d
{
  va_list ap;
  char str[MAXLINE]; // formatted string
  char arg[128];
  const char *mess = "sio_put: Line too long!\n";
  int i = 0, j = 0;
  int sp = 0;
  int v;

  if (fmt == 0)
    return -1;

  va_start(ap, fmt);
  while(fmt[j]){
    if(fmt[j] != '%'){
        j++;
        continue;
    }

    sio_copy(str + sp, fmt + i, j - i);
    sp += j - i;

    switch(fmt[j + 1]){
    case 0:
    		va_end(ap);
        if(sp >= MAXLINE){
            write(STDOUT_FILENO, mess, sio_strlen(mess));
            return -1;
        }
        
        str[sp] = 0;
        return write(STDOUT_FILENO, str, sp);

    case 'd':
        v = va_arg(ap, int);
        sio_ltoa(v, arg, 10);
        sio_copy(str + sp, arg, sio_strlen(arg));
        sp += sio_strlen(arg);
        i = j + 2;
        j = i;
        break;

    case '%':
        sio_copy(str + sp, "%", 1);
        sp += 1;
        i = j + 2;
        j = i;
        break;

    default:
        sio_copy(str + sp, fmt + j, 2);
        sp += 2;
        i = j + 2;
        j = i;
        break;
    }
  } // end while

  sio_copy(str + sp, fmt + i, j - i);
  sp += j - i;

	va_end(ap);
  if(sp >= MAXLINE){
    write(STDOUT_FILENO, mess, sio_strlen(mess));
    return -1;
  }
  
  str[sp] = 0;
  return write(STDOUT_FILENO, str, sp);
}

void sio_error(char s[]) /* Put error message and exit */
{
    sio_puts(s);
    _exit(1);
}

/*
 * Signal - wrapper for the sigaction function
 */
handler_t 
*Signal(int signum, handler_t *handler) 
{
    struct sigaction action, old_action;

    action.sa_handler = handler;  
    sigemptyset(&action.sa_mask); /* block sigs of type being handled */
    action.sa_flags = SA_RESTART; /* restart syscalls if possible */

    if (sigaction(signum, &action, &old_action) < 0)
        unix_error("Signal error");
    return (old_action.sa_handler);
}

