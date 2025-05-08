#include <ncurses.h>
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <stdatomic.h>

#define NUM_YACHTS 20      // Total number of yachts
#define PORT_ROWS 5        // Number of rows in the port
#define PORT_COLS 8        // Number of columns in the port
#define SLOT_LENGTH 50     // Length of each port slot in meters
#define SLOT_WIDTH 10      // Width of each port slot in meters
#define MAX_QUEUE 10       // Max yachts in the waiting queue
#define MAX_DOCKED 20      // Max yachts in the docked list
#define MAX_WAIT_TIME 10   // Max wait time in seconds for fairness

// Structure for a yacht
typedef struct {
    int id;               // Unique ID
    int length;           // Length of the yacht in meters
    int width;            // Width of the yacht in meters
    atomic_int state;     // State: 1=waiting, 2=docked, 3=leaving
} Yacht;

// Port slot structure
typedef struct {
    int row;              // Row index of the slot
    int col;              // Column index of the slot
    int length;           // Length of the slot in meters
    int width;            // Width of the slot in meters
    atomic_int occupied;  // Occupied state: 0=free, 1=occupied
} PortSlot;

// Port and queue data
PortSlot port[PORT_ROWS][PORT_COLS]; // Port grid
Yacht queue[MAX_QUEUE];              // Waiting queue
Yacht docked[MAX_DOCKED];            // List of docked yachts
int queue_size = 0;                  // Current size of the queue
int docked_size = 0;                 // Current size of the docked list

// Mutex for thread safety
pthread_mutex_t port_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t queue_mutex = PTHREAD_MUTEX_INITIALIZER;

// Function prototypes
void init_ncurses();
void cleanup_ncurses();
void* yacht_thread(void* arg);
void* display_thread(void* arg);
void add_to_queue(Yacht* yacht);
void assign_to_port(Yacht* yacht);
void release_slot(Yacht* yacht);
void display_port();
void display_queue();
void display_docked_list();

// Initialize ncurses
void init_ncurses() {
    initscr();
    cbreak();
    noecho();
    curs_set(0);
    start_color();
    init_pair(1, COLOR_WHITE, COLOR_BLUE);   // Port slots
    init_pair(2, COLOR_WHITE, COLOR_RED);    // Waiting queue
    init_pair(3, COLOR_GREEN, COLOR_BLACK);  // Docked yachts
}

// Clean up ncurses
void cleanup_ncurses() {
    endwin();
}

// Main thread for each yacht
void* yacht_thread(void* arg) {
    Yacht* yacht = (Yacht*)arg;
    sleep(rand() % 3 + 1); // Simulate arrival delay

    pthread_mutex_lock(&queue_mutex);
    add_to_queue(yacht);
    pthread_mutex_unlock(&queue_mutex);

    while (atomic_load(&yacht->state) != 3) { // While not leaving
        assign_to_port(yacht);

        if (atomic_load(&yacht->state) == 2) {
            // Docked: Stay for a random duration, then leave
            sleep(rand() % 10 + 10); // Stay docked for 10–20 seconds
            release_slot(yacht);
            atomic_store(&yacht->state, 3); // Mark as leaving
        }

        sleep(1); // Retry after 1 second if waiting
    }

    pthread_exit(NULL);
}

// Add a yacht to the waiting queue
void add_to_queue(Yacht* yacht) {
    if (queue_size < MAX_QUEUE) {
        queue[queue_size++] = *yacht;
    }
}

// Assign a yacht to the port
void assign_to_port(Yacht* yacht) {
    pthread_mutex_lock(&port_mutex);

    // Check for available slots that can fit the yacht
    for (int r = 0; r < PORT_ROWS; r++) {
        for (int c = 0; c < PORT_COLS; c++) {
            if (!atomic_load(&port[r][c].occupied) &&
                port[r][c].length >= yacht->length &&
                port[r][c].width >= yacht->width) {
                // Dock the yacht in this slot
                atomic_store(&port[r][c].occupied, 1);
                atomic_store(&yacht->state, 2); // Docked

                // Remove from queue and add to docked list
                for (int i = 0; i < queue_size; i++) {
                    if (queue[i].id == yacht->id) {
                        // Shift queue to remove yacht
                        for (int j = i; j < queue_size - 1; j++) {
                            queue[j] = queue[j + 1];
                        }
                        queue_size--;
                        break;
                    }
                }

                if (docked_size < MAX_DOCKED) {
                    docked[docked_size++] = *yacht;
                }

                pthread_mutex_unlock(&port_mutex);
                return;
            }
        }
    }

    pthread_mutex_unlock(&port_mutex);
}

// Release a port slot when a yacht leaves
void release_slot(Yacht* yacht) {
    pthread_mutex_lock(&port_mutex);

    // Find the slot occupied by this yacht and release it
    for (int r = 0; r < PORT_ROWS; r++) {
        for (int c = 0; c < PORT_COLS; c++) {
            if (atomic_load(&port[r][c].occupied) &&
                port[r][c].length >= yacht->length &&
                port[r][c].width >= yacht->width) {
                atomic_store(&port[r][c].occupied, 0); // Free the slot

                // Remove from docked list
                for (int i = 0; i < docked_size; i++) {
                    if (docked[i].id == yacht->id) {
                        // Shift docked list to remove yacht
                        for (int j = i; j < docked_size - 1; j++) {
                            docked[j] = docked[j + 1];
                        }
                        docked_size--;
                        break;
                    }
                }

                pthread_mutex_unlock(&port_mutex);
                return;
            }
        }
    }

    pthread_mutex_unlock(&port_mutex);
}

// Display thread
void* display_thread(void* arg) {
    while (1) {
        pthread_mutex_lock(&port_mutex);
        pthread_mutex_lock(&queue_mutex);

        // Display the port, queue, and docked list
        clear();
        display_port();
        display_queue();
        display_docked_list();
        refresh();

        pthread_mutex_unlock(&queue_mutex);
        pthread_mutex_unlock(&port_mutex);

        usleep(500000); // Refresh every 500ms
    }

    pthread_exit(NULL);
}

// Display the port
void display_port() {
    attron(COLOR_PAIR(1));
    mvprintw(1, 10, "Port:");
    for (int r = 0; r < PORT_ROWS; r++) {
        for (int c = 0; c < PORT_COLS; c++) {
            if (atomic_load(&port[r][c].occupied)) {
                mvprintw(3 + r, 10 + c * 5, "[X]");
            } else {
                mvprintw(3 + r, 10 + c * 5, "[ ]");
            }
        }
    }
    attroff(COLOR_PAIR(1));
}

// Display the waiting queue
void display_queue() {
    attron(COLOR_PAIR(2));
    mvprintw(10, 10, "Waiting Queue:");
    for (int i = 0; i < queue_size; i++) {
        mvprintw(12 + i, 10, "Yacht ID:%d Size:%dm x %dm", queue[i].id, queue[i].length, queue[i].width);
    }
    attroff(COLOR_PAIR(2));
}

// Display the list of docked yachts
void display_docked_list() {
    attron(COLOR_PAIR(3));
    mvprintw(10, 40, "Docked Yachts:");
    for (int i = 0; i < docked_size; i++) {
        mvprintw(12 + i, 40, "Yacht ID:%d Size:%dm x %dm", docked[i].id, docked[i].length, docked[i].width);
    }
    attroff(COLOR_PAIR(3));
}

int main() {
    srand(time(NULL));
    init_ncurses();

    // Initialize the port slots
    for (int r = 0; r < PORT_ROWS; r++) {
        for (int c = 0; c < PORT_COLS; c++) {
            port[r][c].row = r;
            port[r][c].col = c;
            port[r][c].length = SLOT_LENGTH;
            port[r][c].width = SLOT_WIDTH;
            atomic_store(&port[r][c].occupied, 0);
        }
    }

    // Create the display thread
    pthread_t display_tid;
    pthread_create(&display_tid, NULL, display_thread, NULL);

    // Create yacht threads
    pthread_t yacht_tids[NUM_YACHTS];
    Yacht yachts[NUM_YACHTS];

    for (int i = 0; i < NUM_YACHTS; i++) {
        yachts[i].id = i + 1;
        yachts[i].length = rand() % 30 + 10; // Random length between 10m and 40m
        yachts[i].width = rand() % 5 + 5;    // Random width between 5m and 10m
        atomic_store(&yachts[i].state, 1);   // Initial state: waiting
        pthread_create(&yacht_tids[i], NULL, yacht_thread, &yachts[i]);
        usleep(500000); // Stagger yacht arrivals
    }

    // Wait for all yachts to finish
    for (int i = 0; i < NUM_YACHTS; i++) {
        pthread_join(yacht_tids[i], NULL);
    }

    // Wait for the display thread to finish
    pthread_cancel(display_tid);
    pthread_join(display_tid, NULL);

    cleanup_ncurses();
    return 0;
}
