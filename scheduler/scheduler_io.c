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
} App;

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
            fclose(file);
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

void startApp(App *app) {
    pid_t pid = fork();
    if (pid == 0) { // Child process
        execl(app->name, app->name, NULL);
        perror("Failed to start app");
        exit(EXIT_FAILURE);
    } else if (pid > 0) { // Parent process
        app->pid = pid;
        app->state = RUNNING;
    } else {
        perror("Fork failed");
        exit(EXIT_FAILURE);
    }
}

void sigchldHandler(int sig) {
    // Implementation for SIGCHLD handler
}

void sigusr1Handler(int sig, siginfo_t *si, void *unused) {
    // Implementation for SIGUSR1 handler
}

void sigusr2Handler(int sig, siginfo_t *si, void *unused) {
    // Implementation for SIGUSR2 handler
}

void scheduleFCFS(App *head) {
    // Implementation for FCFS scheduling
}

void scheduleRR(App *head, int quantum) {
    // Implementation for Round Robin scheduling
}

void freeAppList(App *head) {
    while (head != NULL) {
        App *temp = head;
        head = head->next;
        free(temp);
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

    struct sigaction sa1, sa2;
    sa1.sa_flags = SA_SIGINFO;
    sa1.sa_sigaction = sigusr1Handler;
    sigemptyset(&sa1.sa_mask);
    sigaction(SIGUSR1, &sa1, NULL);

    sa2.sa_flags = SA_SIGINFO;
    sa2.sa_sigaction = sigusr2Handler;
    sigemptyset(&sa2.sa_mask);
    sigaction(SIGUSR2, &sa2, NULL);

    signal(SIGCHLD, sigchldHandler);

    if (strcmp(policy, "FCFS") == 0) {
        scheduleFCFS(head);
    } else if (strcmp(policy, "RR") == 0) {
        scheduleRR(head, quantum);
    } else {
        printf("Invalid scheduling policy.\n");
        return 1;
    }

    freeAppList(head);
    return 0;
}