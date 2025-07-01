/*
 * Simple BGP IPC Test Program
 * Based on OpenBGPD's architecture
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/uio.h>
#include <sys/time.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "imsg.h"

#define PFD_PIPE_CHILD	0
#define PFD_MAX		1

/* Message types between parent and child */
enum imsg_type {
    IMSG_NONE,
    IMSG_HELLO,
    IMSG_ECHO,
    IMSG_SHUTDOWN
};

static void sighdlr(int);
static pid_t start_child(int [2]);
static int dispatch_imsg(struct imsgbuf *);
static void getsockpair(int [2]);

volatile sig_atomic_t	 quit = 0;
pid_t			 child_pid = 0;

static void
sighdlr(int sig)
{
    switch (sig) {
    case SIGTERM:
    case SIGINT:
        quit = 1;
        break;
    }
}

int
main(int argc, char *argv[])
{
    struct imsgbuf	 ibuf;
    struct pollfd	 pfd[PFD_MAX];
    int			 pipe_child[2];
    int			 n, nfds;
    int			 timeout;

    /* Set up signal handler */
    signal(SIGTERM, sighdlr);
    signal(SIGINT, sighdlr);

    /* Set up pipes for IPC */
    getsockpair(pipe_child);

    /* Start child process */
    if ((child_pid = start_child(pipe_child)) == -1)
        errx(1, "could not start child process");

    /* Parent process continues here */
    printf("Parent: started with pid %d, child pid %d\n", getpid(), child_pid);

    /* Close unused socket ends */
    close(pipe_child[1]);

    /* Set up imsg for communication with child */
    imsgbuf_init(&ibuf, pipe_child[0]);

    /* Send initial hello message to child */
    if (imsg_compose(&ibuf, IMSG_HELLO, 0, 0, -1, NULL, 0) == -1)
        err(1, "imsg_compose error");
    
    printf("Parent: sent HELLO message to child\n");
    
    if (imsgbuf_flush(&ibuf) == -1)
        err(1, "imsgbuf_flush error");

    /* Main loop */
    timeout = 5000;
    while (quit == 0) {
        pfd[PFD_PIPE_CHILD].fd = ibuf.fd;
        pfd[PFD_PIPE_CHILD].events = POLLIN;
        if (imsgbuf_queuelen(&ibuf) > 0)
            pfd[PFD_PIPE_CHILD].events |= POLLOUT;

        nfds = poll(pfd, PFD_MAX, timeout);
        if (nfds == -1) {
            if (errno == EINTR)
                continue;
            err(1, "poll error");
        }

        if (nfds == 0) {
            /* Send an echo message to child every timeout */
            if (imsg_compose(&ibuf, IMSG_ECHO, 0, 0, -1, 
                "ping", strlen("ping") + 1) == -1)
                err(1, "imsg_compose error");
            
            printf("Parent: sent ECHO message to child\n");
            
            if (imsgbuf_flush(&ibuf) == -1)
                err(1, "imsgbuf_flush error");
            continue;
        }

        if (pfd[PFD_PIPE_CHILD].revents & POLLIN) {
            if (imsgbuf_read(&ibuf) == -1)
                err(1, "imsgbuf_read error");
            if ((n = dispatch_imsg(&ibuf)) == -1)
                err(1, "dispatch_imsg error");
            if (n == 0)
                quit = 1;
        }

        if (pfd[PFD_PIPE_CHILD].revents & POLLOUT)
            if (imsgbuf_flush(&ibuf) == -1)
                err(1, "imsgbuf_flush error");
    }

    /* Send shutdown message to child */
    if (imsg_compose(&ibuf, IMSG_SHUTDOWN, 0, 0, -1, NULL, 0) == -1)
        err(1, "imsg_compose error");
    
    printf("Parent: sent SHUTDOWN message to child\n");
    
    if (imsgbuf_flush(&ibuf) == -1)
        err(1, "imsgbuf_flush error");

    /* Wait for child to exit */
    if (child_pid > 0) {
        int status;
        printf("Parent: waiting for child to exit...\n");
        waitpid(child_pid, &status, 0);
        printf("Parent: child exited with status %d\n", 
            WIFEXITED(status) ? WEXITSTATUS(status) : -1);
    }

    /* Clean up */
    imsgbuf_clear(&ibuf);
    close(pipe_child[0]);

    return 0;
}

static pid_t
start_child(int pipe_fds[2])
{
    pid_t pid;
    struct imsgbuf ibuf;
    struct pollfd pfd;
    struct imsg imsg;
    ssize_t n;

    pid = fork();
    if (pid == -1)
        err(1, "fork");
    if (pid > 0)
        return pid;  /* Parent returns the child's pid */

    /* Child process continues here */
    setproctitle("child process");
    printf("Child: started with pid %d\n", getpid());

    /* Close unused socket ends */
    close(pipe_fds[0]);

    /* Set up imsg for communication with parent */
    imsgbuf_init(&ibuf, pipe_fds[1]);

    /* Child process loop */
    while (1) {
        pfd.fd = ibuf.fd;
        pfd.events = POLLIN;
        if (imsgbuf_queuelen(&ibuf) > 0)
            pfd.events |= POLLOUT;

        if (poll(&pfd, 1, -1) == -1) {
            if (errno == EINTR)
                continue;
            err(1, "poll error");
        }

        if (pfd.revents & POLLIN) {
            if (imsgbuf_read(&ibuf) == -1)
                err(1, "imsgbuf_read error");

            while ((n = imsg_get(&ibuf, &imsg)) > 0) {
                switch (imsg_get_type(&imsg)) {
                case IMSG_HELLO:
                    printf("Child: received HELLO from parent\n");
                    /* Reply with hello */
                    if (imsg_compose(&ibuf, IMSG_HELLO, 0, 0, -1, 
                        "hello", strlen("hello") + 1) == -1)
                        err(1, "imsg_compose error");
                    break;
                    
                case IMSG_ECHO:
                    {
                        char buf[64] = {0};
                        if (imsg_get_len(&imsg) > 0) {
                            if (imsg_get_data(&imsg, buf, 
                                    sizeof(buf) < imsg_get_len(&imsg) ? 
                                    sizeof(buf) : imsg_get_len(&imsg)) == -1)
                                err(1, "imsg_get_data error");
                        }
                        printf("Child: received ECHO from parent: %s\n", buf);
                        
                        /* Reply with pong */
                        if (imsg_compose(&ibuf, IMSG_ECHO, 0, 0, -1, 
                            "pong", strlen("pong") + 1) == -1)
                            err(1, "imsg_compose error");
                    }
                    break;
                    
                case IMSG_SHUTDOWN:
                    printf("Child: received SHUTDOWN from parent\n");
                    imsg_free(&imsg);
                    imsgbuf_clear(&ibuf);
                    close(pipe_fds[1]);
                    printf("Child: exiting\n");
                    exit(0);
                    break;
                    
                default:
                    printf("Child: received unknown message type %u\n", 
                        imsg_get_type(&imsg));
                    break;
                }
                imsg_free(&imsg);
            }
        }

        if (pfd.revents & POLLOUT)
            if (imsgbuf_flush(&ibuf) == -1)
                err(1, "imsgbuf_flush error");
    }

    /* This point should never be reached */
    exit(1);
}

static int
dispatch_imsg(struct imsgbuf *ibuf)
{
    struct imsg imsg;
    ssize_t n;

    while ((n = imsg_get(ibuf, &imsg)) > 0) {
        switch (imsg_get_type(&imsg)) {
        case IMSG_HELLO:
            {
                char buf[64] = {0};
                if (imsg_get_len(&imsg) > 0) {
                    if (imsg_get_data(&imsg, buf, 
                            sizeof(buf) < imsg_get_len(&imsg) ? 
                            sizeof(buf) : imsg_get_len(&imsg)) == -1)
                        err(1, "imsg_get_data error");
                }
                printf("Parent: received HELLO from child: %s\n", buf);
            }
            break;
            
        case IMSG_ECHO:
            {
                char buf[64] = {0};
                if (imsg_get_len(&imsg) > 0) {
                    if (imsg_get_data(&imsg, buf, 
                            sizeof(buf) < imsg_get_len(&imsg) ? 
                            sizeof(buf) : imsg_get_len(&imsg)) == -1)
                        err(1, "imsg_get_data error");
                }
                printf("Parent: received ECHO response from child: %s\n", buf);
            }
            break;
            
        default:
            printf("Parent: received unknown message type %u\n", 
                imsg_get_type(&imsg));
            break;
        }
        imsg_free(&imsg);
    }

    if (n == -1)
        return -1;
    return 0;
}

static void
getsockpair(int pipe_fds[2])
{
    if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK,
        PF_UNSPEC, pipe_fds) == -1)
        err(1, "socketpair");
}

