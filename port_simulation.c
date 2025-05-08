#include <ncurses.h>
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <stdatomic.h>

#define PORT_ROWS 20       // Number of rows in the port
#define PORT_COLS 20       // Number of columns in the port
#define SLOT_SIZE 5        // Each slot represents 5 meters
#define MAX_QUEUE 10       // Max yachts in the waiting queue
#define MAX_DOCKED 20      // Max yachts in the docked list

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
    atomic_int occupied;  // ID of the occupying yacht, -1 if free
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

    // Calculate the number of slots required by the yacht
    int slots_length = yacht->length / SLOT_SIZE;
    int slots_width = yacht->width / SLOT_SIZE;

    // Look for available space in the port
    for (int r = 0; r <= PORT_ROWS - slots_length; r++) {
        for (int c = 0; c <= PORT_COLS - slots_width; c++) {
            // Check if the required slots are free
            int can_dock = 1;
            for (int i = 0; i < slots_length; i++) {
                for (int j = 0; j < slots_width; j++) {
                    if (atomic_load(&port[r + i][c + j].occupied) != -1) {
                        can_dock = 0;
                        break;
                    }
                }
                if (!can_dock) break;
            }

            // Dock the yacht if space is available
            if (can_dock) {
                for (int i = 0; i < slots_length; i++) {
                    for (int j = 0; j < slots_width; j++) {
                        atomic_store(&port[r + i][c + j].occupied, yacht->id);
                    }
                }

                atomic_store(&yacht->state, 2); // Mark as docked

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

    // Calculate the number of slots occupied by the yacht
    int slots_length = yacht->length / SLOT_SIZE;
    int slots_width = yacht->width / SLOT_SIZE;

    // Free the slots
    for (int r = 0; r < PORT_ROWS; r++) {
        for (int c = 0; c < PORT_COLS; c++) {
            if (atomic_load(&port[r][c].occupied) == yacht->id) {
                for (int i = 0; i < slots_length; i++) {
                    for (int j = 0; j < slots_width; j++) {
                        atomic_store(&port[r + i][c + j].occupied, -1);
                    }
                }
                break;
            }
        }
    }

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
            if (atomic_load(&port[r][c].occupied) != -1) {
                mvprintw(3 + r, 10 + c * 6, " [%3d] ", atomic_load(&port[r][c].occupied));
            } else {
                mvprintw(3 + r, 10 + c * 6, " [   ] ");
            }
        }
    }
    attroff(COLOR_PAIR(1));
}

// Display the waiting queue
void display_queue() {
    attron(COLOR_PAIR(2));
    mvprintw(25, 10, "Waiting Queue:"); // Place queue at the bottom
    for (int i = 0; i < queue_size; i++) {
        mvprintw(27, 10 + i * 15, "ID:%d Size:%dmx%dm", queue[i].id, queue[i].length, queue[i].width);
    }
    attroff(COLOR_PAIR(2));
}

// Display the list of docked yachts
void display_docked_list() {
    attron(COLOR_PAIR(3));
    mvprintw(25, 80, "Docked Yachts:"); // Place docked list on the right
    for (int i = 0; i < docked_size; i++) {
        mvprintw(27 + i, 80, "ID:%d Size:%dmx%dm", docked[i].id, docked[i].length, docked[i].width);
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
            atomic_store(&port[r][c].occupied, -1); // Mark as free
        }
    }

    // Create the display thread
    pthread_t display_tid;
    pthread_create(&display_tid, NULL, display_thread, NULL);

    // Dynamically create yacht threads
    int yacht_id = 1;
    while (1) {
        Yacht* yacht = (Yacht*)malloc(sizeof(Yacht));
        yacht->id = yacht_id++;
        yacht->length = rand() % 50 + 10; // Random length between 10m and 40m
        yacht->width = rand() % 45 + 5;    // Random width between 5m and 10m
        atomic_store(&yacht->state, 1);   // Initial state: waiting

        pthread_t yacht_tid;
        pthread_create(&yacht_tid, NULL, yacht_thread, yacht);
        usleep(2000000); // Stagger yacht arrivals
    }

    // Wait for the display thread to finish
    pthread_cancel(display_tid);
    pthread_join(display_tid, NULL);

    cleanup_ncurses();
    return 0;
}