#include <sys/types.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/uio.h>

#include <errno.h>
#include <event.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "imsg.h"  /* From compat/ directory */

/* Define message types similar to bgpd */
#define IMSG_NONE       0
#define IMSG_TEST_MSG   1

/* Simple program structure mimicking bgpd */
struct imsgbuf ibuf;
struct event ev_main;

/* Function to handle incoming messages */
void
dispatch_imsg(int fd, short event, void *bula)
{
    struct imsg imsg;
    ssize_t n;
    char *data;

    if ((n = imsg_read(&ibuf)) == -1) {
        fprintf(stderr, "imsg_read error: %s\n", strerror(errno));
        exit(1);
    }
    if (n == 0) {
        /* Connection closed */
        fprintf(stderr, "Connection closed\n");
        event_del(&ev_main);
        return;
    }

    for (;;) {
        if ((n = imsg_get(&ibuf, &imsg)) == -1) {
            fprintf(stderr, "imsg_get error: %s\n", strerror(errno));
            exit(1);
        }
        if (n == 0)
            break;  /* No more messages */

        printf("Received message type %u, len %zu\n",
               imsg.hdr.type, imsg.hdr.len - IMSG_HEADER_SIZE);

        /* Process message based on type */
        switch (imsg.hdr.type) {
        case IMSG_TEST_MSG:
            data = imsg.data;
            printf("Message content: %s\n", data);
            break;
        default:
            printf("Unknown message type\n");
            break;
        }

        imsg_free(&imsg);
    }
}

int
main(int argc, char *argv[])
{
    int pipe_fds[2];
    struct pollfd pfd[1];
    int nfds;
    char buffer[1024];

    /* Create a socket pair for IPC */
    if (socketpair(AF_UNIX, SOCK_STREAM, PF_UNSPEC, pipe_fds) == -1) {
        perror("socketpair");
        exit(1);
    }

    /* Fork to create parent and child processes */
    pid_t pid = fork();
    if (pid == -1) {
        perror("fork");
        exit(1);
    }

    if (pid == 0) {
        /* Child process - sender */
        close(pipe_fds[0]);

        /* Initialize imsg in child */
        imsg_init(&ibuf, pipe_fds[1]);

        /* Send messages from stdin */
        printf("Child: Enter messages to send (each line will be sent as a message):\n");
        while (fgets(buffer, sizeof(buffer), stdin) != NULL) {
            /* Remove newline */
            buffer[strcspn(buffer, "\n")] = '\0';

            /* Send message */
            if (imsg_compose(&ibuf, IMSG_TEST_MSG, 0, 0, -1,
                            buffer, strlen(buffer) + 1) == -1) {
                perror("imsg_compose");
                exit(1);
            }

            /* Flush the message */
            if (msgbuf_write(&ibuf.w) == -1) {
                perror("msgbuf_write");
                exit(1);
            }
        }

        close(pipe_fds[1]);
        exit(0);
    } else {
        /* Parent process - receiver */
        close(pipe_fds[1]);

        printf("Parent: Waiting for messages from child...\n");

        /* Initialize imsg in parent */
        imsg_init(&ibuf, pipe_fds[0]);

        /* Set up polling */
        pfd[0].fd = pipe_fds[0];
        pfd[0].events = POLLIN;

        /* To test with stdin instead, you can change this to: */
        if (argc > 1 && strcmp(argv[1], "--stdin") == 0) {
            printf("Using stdin for input instead of socket\n");
            pfd[0].fd = STDIN_FILENO;
        }

        /* Main loop */
        while (1) {
            nfds = poll(pfd, 1, -1);
            if (nfds == -1) {
                if (errno != EINTR) {
                    perror("poll");
                    exit(1);
                }
                continue;
            }

            if (pfd[0].revents & POLLIN) {
                dispatch_imsg(pfd[0].fd, 0, NULL);
            }
        }
    }

    return 0;
}

