#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <time.h>

#define MAX_LENGTH 50

typedef enum { 
    NEW, 
    RUNNING,
    STOPPED, 
    EXITED 
} ProcessState;

typedef struct timer {
    struct timespec tmp;
    struct timespec start;
    float elapsed;
    float work;
} Timer;

typedef struct Process {
    char name[MAX_LENGTH];
    int pid;
    ProcessState state;
    int entryTime;
    struct Process *next;
    struct Process *prev;
    Timer timer;
} Process;

// Global head pointer for accessing the list in the signal handler
Process *head = NULL;

// Reads the Processlications from the file and stores them in the list
void readProcessFromFile(char *filename) {
    FILE *file = fopen(filename, "r");
    if (file == NULL) {
        perror("Error opening file");
        exit(EXIT_FAILURE);
    }

    char ProcessName[MAX_LENGTH];
    int entryTime;
    Process *last = NULL;
    while (fscanf(file, "%s %d", ProcessName, &entryTime) != EOF) {
        Process *newProcess = (Process *)malloc(sizeof(Process));
        if (newProcess == NULL) {
            perror("Failed to allocate memory for new Process");
            exit(EXIT_FAILURE);
        }
        strncpy(newProcess->name, ProcessName, MAX_LENGTH);
        newProcess->pid = -1;
        newProcess->state = NEW;
        newProcess->entryTime = entryTime;
        newProcess->next = NULL;
        newProcess->prev = last;
        if (last != NULL) {
            last->next = newProcess;
        } else {
            head = newProcess;
        }
        last = newProcess;
    }

    fclose(file);
}

void printProcess(Process *current) {
    printf("PID %d - CMD: %s\n\tElapsed Time: %.2f s\n\tWorkload Time: %.2f s\n", 
            current->pid, current->name, (double)current->timer.elapsed / 1000, (double)current->timer.work / 1000);
}

// Signal handler for SIGCHLD
void sigchldHandler(int sig) {
    int status;
    pid_t pid;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        Process *current = head;
        while (current != NULL) {
            if (current->pid == pid) {
                struct timespec now;
                clock_gettime(CLOCK_MONOTONIC, &now);
                current->timer.elapsed = (now.tv_sec - current->timer.start.tv_sec) * 1000 + 
                        (now.tv_nsec - current->timer.start.tv_nsec) / 1000000.0;
                current->timer.work += (now.tv_sec - current->timer.tmp.tv_sec) * 1000 + 
                        (now.tv_nsec - current->timer.tmp.tv_nsec) / 1000000.0;
                current->state = EXITED;

                printProcess(current);
                break;
            }
            current = current->next;
        }
    }
}

// Start an Processlication
void startProcess(Process *Process) {
    pid_t pid = fork();
    if (pid == 0) { // child
        execl(Process->name, Process->name, NULL);
        perror("Failed to start Process");
        exit(EXIT_FAILURE);
    } else if (pid > 0) { // parent
        Process->pid = pid;
        Process->state = RUNNING;
        clock_gettime(CLOCK_MONOTONIC, &Process->timer.start);
    } else {
        perror("fork failed");
        exit(EXIT_FAILURE);
    }
}

int isAllStopped(Process *head) {
    Process *current = head;
    while (current != NULL) {
        if (current->state != EXITED) {
            return 0;
        }
        current = current->next;
    }
    return 1;
}

// Implementation of the FCFS scheduling
void scheduleFCFS(Process *head) {
    Process *current = head;
    while (!isAllStopped(head)) {
        startProcess(current);
        waitpid(current->pid, NULL, 0); // Wait for the Processlication to finish
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        current->timer.elapsed = (now.tv_sec - current->timer.start.tv_sec) * 1000 + 
                (now.tv_nsec - current->timer.start.tv_nsec) / 1000000.0;
        current->timer.work = current->timer.elapsed;

        printProcess(current);

        current->state = EXITED;
        current = current->next;
    }
}

// Implementation of the RR scheduling
void scheduleRR(Process *head, int quantum) {
    struct timespec ts;
    ts.tv_sec = quantum / 1000;
    ts.tv_nsec = (quantum % 1000) * 1000000L;

    
    while (!isAllStopped(head)) {
        Process *current = head;

        while (current != NULL) {
            if (current->state == NEW) {
                startProcess(current);
                kill(current->pid, SIGSTOP);
                current->state = STOPPED;
                continue;
            } else if (current->state == STOPPED) {
                kill(current->pid, SIGCONT);
                struct timespec now;
                clock_gettime(CLOCK_MONOTONIC, &now);
                current->timer.tmp = now;
                current->state = RUNNING;
            }

            if (current->state == RUNNING) {
                nanosleep(&ts, NULL);
                if (current->state == EXITED) {
                    current = current->next;
                    continue;
                }
                kill(current->pid, SIGSTOP);
                struct timespec now;
                clock_gettime(CLOCK_MONOTONIC, &now);
                current->timer.work += (now.tv_sec - current->timer.tmp.tv_sec) * 1000 + 
                        (now.tv_nsec - current->timer.tmp.tv_nsec) / 1000000.0;
                current->state = STOPPED;
            }

            current = current->next;
        }
    }
}

int main(int argc, char *argv[]) {
    char *policy;
    int quantum = 0;

    // Check for minimum number of arguments
    if (argc < 3) {
        printf("Usage: %s <policy> [<quantum>] <input_filename>\n", argv[0]);
        return 1;
    }

    policy = argv[1];

    // Check if the policy is RR and handle the quantum argument
    if (strcmp(policy, "RR") == 0) {
        if (argc < 4) {
            printf("Usage for RR: %s RR <quantum> <input_filename>\n", argv[0]);
            return 1;
        }
        quantum = atoi(argv[2]);
        readProcessFromFile(argv[3]);
    } else {
        readProcessFromFile(argv[2]);
    }

    signal(SIGCHLD, sigchldHandler);
    
    if (strcmp(policy, "FCFS") == 0) {
        scheduleFCFS(head);
    } else if (strcmp(policy, "RR") == 0) {
        scheduleRR(head, quantum);
    } else {
        printf("Invalid scheduling policy.\n");
        return 1;
    }

    // Clear list and free memory
    while (head != NULL) {
        Process *temp = head;
        head = head->next;
        free(temp);
    }

    return 0;
}