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
} AppState;

typedef struct app {
    char name[MAX_LENGTH];
    int pid;
    AppState state;
    int entryTime;
    struct app *next;
    struct app *prev;
    int waitingForIO;
    int completedIO;
} App;

// Global head pointer for accessing the list in the signal handler
App *head = NULL;

// Reads the applications from the file and stores them in the list
void readAppsFromFile(char *filename) {
    FILE *file = fopen(filename, "r");
    if (file == NULL) {
        perror("Error opening file");
        exit(EXIT_FAILURE);
    }

    char appName[MAX_LENGTH];
    int entryTime;
    App *last = NULL;
    while (fscanf(file, "%s %d", appName, &entryTime) != EOF) {
        App *newApp = (App *)malloc(sizeof(App));
        if (newApp == NULL) {
            perror("Failed to allocate memory for new app");
            exit(EXIT_FAILURE);
        }
        strncpy(newApp->name, appName, MAX_LENGTH);
        newApp->pid = -1;
        newApp->state = NEW;
        newApp->entryTime = entryTime;
        newApp->next = NULL;
        newApp->prev = last;
        if (last != NULL) {
            last->next = newApp;
        } else {
            head = newApp;
        }
        last = newApp;
    }

    fclose(file);
}

// Start an application
void startApp(App *app) {
    pid_t pid = fork();
    if (pid == 0) { // child
        execl(app->name, app->name, NULL);
        perror("Failed to start app");
        exit(EXIT_FAILURE);
    } else if (pid > 0) { // parent
        app->pid = pid;
        app->state = RUNNING;
    } else {
        perror("fork failed");
        exit(EXIT_FAILURE);
    }
}

// Signal handler for SIGUSR1
void sigusr1Handler(int sig) {
    App *current = head;
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
    App *current = head;
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
        App *current = head;
        while (current != NULL) {
            if (current->pid == pid) {
                current->state = EXITED;
                break;
            }
            current = current->next;
        }
    }
}


// Implementation of the FCFS scheduling
void scheduleFCFS(App *head) {
    App *current = head;
    while (current != NULL) {
        if (current->state == NEW) {
            startApp(current);
        }

        if (current->state == RUNNING || current->state == READY_TO_RUN) {
            waitpid(current->pid, NULL, 0); // Wait for the application to finish or stop for I/O
            if (current->waitingForIO) {
                current->state = WAITING_FOR_IO;
                // Wait for I/O to complete
                while (!current->completedIO) {
                    // Spin or sleep
                }
                current->state = READY_TO_RUN;
            }
        }

        if (current->state == EXITED) {
            current = current->next;
        }
    }
}

// Implementation of the RR scheduling
void scheduleRR(App *head, int quantum) {
    struct timespec ts;
    ts.tv_sec = quantum / 1000;
    ts.tv_nsec = (quantum % 1000) * 1000000L;

    while (1) {
        int allExited = 1;
        App *current = head;

        while (current != NULL) {
            if (current->state == NEW) {
                startApp(current);
            }

            if (current->state == RUNNING || current->state == READY_TO_RUN) {
                nanosleep(&ts, NULL);
                kill(current->pid, SIGSTOP);
                current->state = STOPPED;
            }

            if (current->state == WAITING_FOR_IO && current->completedIO) {
                current->state = READY_TO_RUN;
                current->completedIO = 0;
            } else if (current->state == STOPPED) {
                kill(current->pid, SIGCONT);
                current->state = RUNNING;
            }

            // Check if the process has exited
            if (waitpid(current->pid, NULL, WNOHANG) > 0) {
                current->state = EXITED;
            }

            if (current->state != EXITED) {
                allExited = 0;
            }

            current = current->next;
        }

        if (allExited) {
            break; // All processes have exited
        }
    }
}

void freeAppList(App *head) {
    while (head != NULL) {
        App *temp = head;
        head = head->next;
        free(temp);
    }
}

int main(int argc, char *argv[]) {
    char *policy;
    int quantum = 0;

    struct sigaction sa;

// Set up SIGUSR1 handler
sa.sa_handler = &sigusr1Handler;
sigemptyset(&sa.sa_mask);
sa.sa_flags = 0;
sigaction(SIGUSR1, &sa, NULL);

// Set up SIGUSR2 handler
sa.sa_handler = &sigusr2Handler;
sigemptyset(&sa.sa_mask);
sa.sa_flags = 0;
sigaction(SIGUSR2, &sa, NULL);

// Set up SIGCHLD handler
sa.sa_handler = &sigchldHandler;
sigemptyset(&sa.sa_mask);
sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
sigaction(SIGCHLD, &sa, NULL);


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
        readAppsFromFile(argv[3]);
    } else {
        readAppsFromFile(argv[2]);
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
        App *temp = head;
        head = head->next;
        free(temp);
    }

    return 0;
}