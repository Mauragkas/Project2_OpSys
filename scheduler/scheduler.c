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
} AppState;

typedef struct app {
    char name[MAX_LENGTH];
    int pid;
    AppState state;
    int entryTime;
    struct app *next;
    struct app *prev;
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

// Implementation of the FCFS scheduling
void scheduleFCFS(App *head) {
    App *current = head;
    while (current != NULL) {
        startApp(current);
        waitpid(current->pid, NULL, 0); // Wait for the application to finish
        current->state = EXITED;
        current = current->next;
    }
}

// Signal handler for SIGCHLD
void sigchldHandler(int sig) {
    pid_t pid;
    int status;

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

// Updated checkAllExited function
int checkAllExited(App *head) {
    App *current = head;
    int allExited = 1; // Assume all processes have exited

    while (current != NULL) {
        if (current->state != EXITED) {
            allExited = 0; // Found a process that has not exited
            break;
        }
        current = current->next;
    }
    
    return allExited; // Return the result
}

// Modified scheduleRR function
void scheduleRR(App *head, int quantum) {
    struct timespec ts;
    ts.tv_sec = quantum / 1000;
    ts.tv_nsec = (quantum % 1000) * 1000000L;

    App *current = head;
    int allExited = 0; // Initialize to 0

    while (!allExited) { // Continue looping until all processes have exited
        if (current == NULL) {
            // Restart from the head if the end of the list is reached
            current = head;
        }

        if (current->state == NEW) {
            startApp(current);
            current->state = RUNNING;
        }

        if (current->state == RUNNING) {
            kill(current->pid, SIGCONT); // Continue the process if it was stopped
            nanosleep(&ts, NULL);        // Run for the time quantum
            kill(current->pid, SIGSTOP); // Stop the process
            current->state = STOPPED;
        }

        // Move to the next process
        current = current->next;

        // Check if all apps have exited in each iteration
        allExited = checkAllExited(head);
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
        readAppsFromFile(argv[3]);
    } else {
        readAppsFromFile(argv[2]);
    }

    if (strcmp(policy, "FCFS") == 0) {
        scheduleFCFS(head);
    } else if (strcmp(policy, "RR") == 0) {
        signal(SIGCHLD, sigchldHandler);
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
