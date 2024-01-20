#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <time.h>

#define MAX_LENGTH 50

typedef enum {
	NEW, 
	RUNNING, 
	STOPPED, 
	WAITING_FOR_IO, 
	READY_TO_RUN, 
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
    struct Process *next;
    struct Process *prev;
    int waitingForIO;
    int completedIO;
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
    Process *last = NULL;
    while (fscanf(file, "%s", ProcessName) != EOF) {
        Process *newProcess = (Process *)malloc(sizeof(Process));
        if (newProcess == NULL) {
            perror("Failed to allocate memory for new Process");
            exit(EXIT_FAILURE);
        }
        strncpy(newProcess->name, ProcessName, MAX_LENGTH);
        newProcess->pid = -1;
        newProcess->state = NEW;
        newProcess->next = NULL;
        newProcess->prev = last;
        newProcess->waitingForIO = 0;
        newProcess->completedIO = 0;
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

// Signal handler for SIGUSR1
void sigusr1Handler(int sig) {
    // printf("Received SIGUSR1\n");
    Process *current = head;
    while (current != NULL) {
        if (current->state == RUNNING) {
            current->waitingForIO = 1;
            // Update any other relevant state or perform actions
            break;
        }
        current = current->next;
    }
}

// Signal handler for SIGUSR2
void sigusr2Handler(int sig) {
    // printf("Received SIGUSR2\n");s
    Process *current = head;
    while (current != NULL) {
        if (current->waitingForIO) {
            current->completedIO = 1;
            current->waitingForIO = 0;
            // Update any other relevant state or perform actions
            break;
        }
        current = current->next;
    }
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

// Implementation of the FCFS scheduling with refined logic
void scheduleFCFS(Process *head) {
    Process *current = head;
    int status;

    while (current != NULL) {
        // Start the process if it's new
        if (current->state == NEW) {
            startProcess(current);
        }

        // Wait for the process to change state (complete, stop for I/O, or exit)
        if (current->state == RUNNING || current->state == READY_TO_RUN) {
            pid_t result = waitpid(current->pid, &status, WUNTRACED);

            if (result == -1) {
                perror("waitpid failed");
                exit(EXIT_FAILURE);
            }

            if (WIFEXITED(status)) {
                // Process has exited
                current->state = EXITED;
                // printf("Process %s exited\n", current->name);
            } else if (WIFSTOPPED(status)) {
                // Process stopped for I/O
                current->state = WAITING_FOR_IO;
                // printf("Process %s waiting for I/O\n", current->name);
            } else if (WIFCONTINUED(status)) {
                // Process continued after I/O completion
                current->state = RUNNING;
            }
        }

        // If the process is waiting for I/O and the I/O is completed
        if (current->state == WAITING_FOR_IO && current->completedIO) {
            current->completedIO = 0; // Reset I/O completion flag
            kill(current->pid, SIGCONT); // Continue the process
            current->state = RUNNING; // Update state to RUNNING
        }

        // Move to the next process
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        current->timer.elapsed = (now.tv_sec - current->timer.start.tv_sec) * 1000 + 
                (now.tv_nsec - current->timer.start.tv_nsec) / 1000000.0;
        current->timer.work += current->timer.elapsed;
        current->timer.tmp = now;

        printProcess(current);

        current = current->next;
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
            } else if (current->state == STOPPED) {
                kill(current->pid, SIGCONT);
                struct timespec now;
                clock_gettime(CLOCK_MONOTONIC, &now);
                current->timer.tmp = now;
                current->state = RUNNING;
            }

            if (current->state == RUNNING || current->state == READY_TO_RUN) {
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
            } else if (current->state == WAITING_FOR_IO && current->completedIO) {
                current->state = READY_TO_RUN;
                current->completedIO = 0;
            }

            current = current->next;
        }
    }
}

void freeProcessList(Process *head) {
    while (head != NULL) {
        Process *temp = head;
        head = head->next;
        free(temp);
    }
}

int main(int argc, char *argv[]) {
    char *policy;
    int quantum = 0;

// Set up SIGUSR1 handler
signal(SIGUSR1, sigusr1Handler);

// Set up SIGUSR2 handler
signal(SIGUSR2, sigusr2Handler);

// Set up SIGCHLD handler
signal(SIGCHLD, sigchldHandler);

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