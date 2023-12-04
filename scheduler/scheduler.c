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

// Reads the applications from the file and stores them in the list
void readAppsFromFile(App **head, char *filename) {
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
            *head = newApp;
        }
        last = newApp;
    }

    fclose(file);
}

// Χειριστής για το σήμα SIGCHLD
void sigchldHandler(int sig) {
    int status;
    pid_t pid;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        // Ενημέρωση της κατάστασης της διεργασίας
        // Εκτύπωση του συνολικού χρόνου εκτέλεσης
    }
}

// Εκκίνηση μιας εφαρμογής
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

// Implementation of the FCFS 
void scheduleFCFS(App *head) {
    App *current = head;
    while (current != NULL) {
        startApp(current);
        waitpid(current->pid, NULL, 0); // Wait for the application to finish
        current->state = EXITED;
        current = current->next;
    }
}

// Implementation of the RR
void scheduleRR(App *head, int quantum) {
    struct timespec ts;
    ts.tv_sec = quantum / 1000;
    ts.tv_nsec = (quantum % 1000) * 1000000L;

    App *current = head;
    while (current != NULL) {
        if (current->state != RUNNING) {
            startApp(current);
        }

        nanosleep(&ts, NULL);

        kill(current->pid, SIGSTOP);
        current->state = STOPPED;
        current = current->next;
    }
}

int main(int argc, char *argv[]) {
    App *head = NULL;
    char *policy;
    int quantum = 0;

    if (argc < 3) {
        printf("Usage: %s <policy> <input_filename> [<quantum>]\n", argv[0]);
        return 1;
    }

    policy = argv[1];
    readAppsFromFile(&head, argv[2]);

    if (strcmp(policy, "RR") == 0 && argc == 4) {
        quantum = atoi(argv[3]);
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
        App *temp = head;
        head = head->next;
        free(temp);
    }

    return 0;
}