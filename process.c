#include <stdio.h>
#include <stdlib.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/msg.h>
#include <sys/sem.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <string.h>
#include <time.h>

#define P(s) semop(s, &pop, 1)
#define V(s) semop(s, &vop, 1)

typedef struct message1
{
    long type;
    int pid;
} message1;

typedef struct message3
{
    long type;
    int pageorframe;
    int pid;
} message3;

int main(int argc, char *argv[])
{
    struct sembuf pop = {0, -1, 0};
    struct sembuf vop = {0, 1, 0};

    if (argc != 4)
    {
        printf("Usage: %s <Reference String> <Virtual Address Space size> <Physical Address Space size>\n", argv[0]);
        exit(1);
    }

    int msgid1 = atoi(argv[2]);
    int msgid3 = atoi(argv[3]);

    char *refstr;
    strcpy(refstr, argv[1]);

    printf("Process has started\n");
    key_t key = ftok("master.c", 4);
    int semid = semget(key, 1, IPC_CREAT | 0666);

    int pid = getpid();

    message1 msg1;
    msg1.type = 1;
    msg1.pid = pid;

    // send pid to ready queue
    msgsnd(msgid1, (void *)&msg1, sizeof(message1), 0);

    // wait till scheduler signals to start
    P(semid);

    // send the reference string to the scheduler, one character at a time
    int i = 0;
    while (refstr[i] != '\0')
    {
        message3 msg3;
        msg3.type = 1;
        msg3.pid = pid;
        int j = 0;
        // extract the page number from the reference string going character by character
        while (refstr[i] != '.' && refstr[i] != '\0')
        {
            j = j * 10 + (refstr[i] - '0');
            i++;
        }
        i++;
        msg3.pageorframe = j;
        msgsnd(msgid3, (void *)&msg3, sizeof(message3), 0);

        // wait for the mmu to allocate the frame
        msgrcv(msgid3, (void *)&msg3, sizeof(message3), 0, 0);

        // check the validity of the frame number
        if (msg3.pageorframe == -2)
        {
            printf("Process %d: ", pid);
            printf("Illegal Page Number\nTerminating\n");
            exit(1);
        }
        else if (msg3.pageorframe == -1)
        {
            printf("Process %d: ", pid);
            printf("Page Fault\nWaiting for page to be loaded\n");
            // wait for the page to be loaded
            // scheduler will signal when the page is loaded
            P(semid);
            continue;
        }
        else
        {
            printf("Process %d: ", pid);
            printf("Frame %d allocated\n", msg3.pageorframe);
        }
    }

    // send the termination signal to the mmu
    printf("Process %d: ", pid);
    printf("Got all frames properly\n");
    message3 msg3;
    msg3.type = 1;
    msg3.pid = pid;
    msg3.pageorframe = -9;

    msgsnd(msgid3, (void *)&msg3, sizeof(message3), 0);
    printf("Terminating\n");

    return 0;
}
