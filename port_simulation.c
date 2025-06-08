#include <ncurses.h>
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <stdatomic.h>
#include <math.h>
#include <string.h>

#define PORT_ROWS 20       // Number of rows in the port
#define PORT_COLS 25       // Number of columns in the port
#define SLOT_SIZE 5        // Each slot represents 5 meters
#define MAX_QUEUE 10       // Max yachts in the waiting queue
#define MAX_DOCKED 20      // Max yachts in the docked list
#define QUAY_LENGTH 3      // Amount of columns in quay
#define MAX_CREWS 4        // 2 cleaning, 2 repair

#define YACHT_MIN_LENGTH 10
#define YACHT_MAX_LENGTH 50

#define YACHT_MIN_WIDTH 5
#define YACHT_MAX_WIDTH 30

// Structure for a yacht
typedef struct {
    int id;                       // Unique ID
    int length;                   // Length of the yacht in meters
    int width;                    // Width of the yacht in meters
    atomic_int state;             // State: 1=waiting, 2=docked, 3=leaving, 4=docked at fuel station
    atomic_int oil_level;         // Level of oil in tank in percents
    atomic_bool need_cleaning;    // Whether the yacht needs cleaning
    atomic_bool need_repair;      // Whether the yacht needs repair
    int waiting_time;             // Time spent waiting in seconds
} Yacht;

// Port slot structure
typedef struct {
    int row;                      // Row index of the slot
    int col;                      // Column index of the slot
    atomic_int occupied;          // ID of the occupying yacht, -1 if free, -2 if quay, -3 if oil pump
} PortSlot;

// Structure for a port crew
typedef struct {
    int id;                       // Unique ID of the crew
    int yacht_id;                 // Yacht assigned to the crew, -1 if no yacht assigned
    int crew_size;                // Number of crew members
    atomic_int state;             // State: 0=idle, 1=working, 2=waiting for yacht
    int job_id;                   // ID of the job assigned to the crew, 1 for cleaning, 2 for repairing
} PortCrew;

// Port and queue data
PortSlot port[PORT_ROWS][PORT_COLS]; // Port grid
Yacht queue[MAX_QUEUE];              // Waiting queue
Yacht docked[MAX_DOCKED];            // List of docked yachts
PortCrew crews[MAX_CREWS];           // Port crews
int queue_size = 0;                  // Current size of the queue
int docked_size = 0;                 // Current size of the docked list

// Mutexes for thread safety
pthread_mutex_t port_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t queue_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t docked_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t stats_mutex = PTHREAD_MUTEX_INITIALIZER;

// Statistics structure for the port
typedef struct {
    int total_yachts_serviced;   // Total number of yachts serviced
    long total_waiting_time;     // Total waiting time of all yachts
    int max_waiting_time;        // Maximum waiting time observed
    int total_cleanings;         // Total number of cleanings performed
    int total_repairs;           // Total number of repairs performed
    int total_refuels;           // Total number of refuels performed
} PortStats;
PortStats stats = {0};

// Function prototypes
void init_ncurses();
void cleanup_ncurses();
void* yacht_thread(void* arg);
void* display_thread(void* arg);
void* port_crew_thread(void* arg);
void add_to_queue(Yacht* yacht);
void assign_to_port(Yacht* yacht);
void release_slot(Yacht* yacht);
void display_port();
void display_queue();
void display_docked_list();
void display_port_crew_list();
void display_stats();

// Initialize ncurses
void init_ncurses() {
    initscr();
    cbreak();
    noecho();
    curs_set(0);
    start_color();

    // Default slot background
    init_pair(1, COLOR_WHITE, COLOR_BLUE);

    // Dynamic yacht colors (up to 6 unique colors for demonstration)
    init_pair(2, COLOR_WHITE, COLOR_RED);
    init_pair(3, COLOR_WHITE, COLOR_GREEN);
    init_pair(4, COLOR_WHITE, COLOR_YELLOW);
    init_pair(5, COLOR_WHITE, COLOR_MAGENTA);
    init_pair(6, COLOR_WHITE, COLOR_CYAN);
    init_pair(7, COLOR_BLACK, COLOR_WHITE);  // Pure white on black for quay
    init_pair(8, COLOR_BLACK, COLOR_YELLOW); // For oil pumps
    // You can add more if supported by terminal
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
            int extra_wait = 0;

            // If cleaning or repair is needed, add yacht to crew queue and wait
            if (yacht->need_cleaning) {
                int assigned = 0, crew_idx = -1;
                while (!assigned) {
                    for (int i = 0; i < MAX_CREWS / 2; i++) {
                        if (atomic_load(&crews[i].state) == 0) {
                            crews[i].yacht_id = yacht->id;
                            atomic_store(&crews[i].state, 1); // working
                            assigned = 1;
                            crew_idx = i;
                            pthread_mutex_lock(&stats_mutex);
                            stats.total_cleanings++;
                            pthread_mutex_unlock(&stats_mutex);
                            break;
                        }
                    }
                    if (!assigned) sleep(1);
                }
                // Czekaj aż ekipa skończy
                while (!(atomic_load(&crews[crew_idx].state) == 0 && crews[crew_idx].yacht_id == -1)) {
                    sleep(1);
                }
                extra_wait += 5; // Add 5 seconds for cleaning
            }
            if (yacht->need_repair) {
                int assigned = 0, crew_idx = -1;
                while (!assigned) {
                    for (int i = MAX_CREWS / 2; i < MAX_CREWS; i++) {
                        if (atomic_load(&crews[i].state) == 0) {
                            crews[i].yacht_id = yacht->id;
                            atomic_store(&crews[i].state, 1); // working
                            assigned = 1;
                            crew_idx = i;
                            pthread_mutex_lock(&stats_mutex);
                            stats.total_repairs++;
                            pthread_mutex_unlock(&stats_mutex);
                            break;
                        }
                    }
                    if (!assigned) sleep(1);
                }
                // Czekaj aż ekipa skończy
                while (!(atomic_load(&crews[crew_idx].state) == 0 && crews[crew_idx].yacht_id == -1)) {
                    sleep(1);
                }
                extra_wait += 5; // Add 5 seconds for repair
            }

            // Docked: Stay for a random duration, then leave
            sleep(rand() % 20 + 20 + extra_wait); // Stay docked for 20–40 seconds + extra
            release_slot(yacht);
            atomic_store(&yacht->state, 3); // Mark as leaving
        }
        if (atomic_load(&yacht->state) == 4) {
            // Docked at fuel station: refuel depending on oil level
            int oil = atomic_load(&yacht->oil_level);
            pthread_mutex_lock(&stats_mutex);
            stats.total_refuels++;
            pthread_mutex_unlock(&stats_mutex);
            while (oil < 100) {
                usleep(300000); // Wait 300 ms
                oil++;
                atomic_store(&yacht->oil_level, oil);

                // Update oil_level in docked list for display
                pthread_mutex_lock(&docked_mutex);
                for (int i = 0; i < docked_size; i++) {
                    if (docked[i].id == yacht->id) {
                        docked[i].oil_level = oil;
                        break;
                    }
                }
                pthread_mutex_unlock(&docked_mutex);
            }
            // When refueled, leave fuel station and try to dock again for services
            release_slot(yacht);
            if(yacht->need_cleaning == true || yacht->need_repair == true){
                atomic_store(&yacht->state, 1); // Set back to waiting (queue)
                yacht->waiting_time = 0; // Optionally reset waiting time
                pthread_mutex_lock(&queue_mutex);
                add_to_queue(yacht);
                pthread_mutex_unlock(&queue_mutex);
                continue; // Go back to main loop and try to dock again
            }
            else

            atomic_store(&yacht->state, 3); // Mark as leaving
        }

        sleep(1); // Retry after 1 second if waiting
        if (atomic_load(&yacht->state) == 1) {
            yacht->waiting_time++;
            // Aktualizuj waiting_time w queue[]
            pthread_mutex_lock(&queue_mutex);
            for (int i = 0; i < queue_size; i++) {
                if (queue[i].id == yacht->id) {
                    queue[i].waiting_time = yacht->waiting_time;
                    break;
                }
            }
            pthread_mutex_unlock(&queue_mutex);
        }
    }
    // Update statistics after yacht leaves
    pthread_mutex_lock(&stats_mutex);
    stats.total_yachts_serviced++;
    stats.total_waiting_time += yacht->waiting_time;
    if (yacht->waiting_time > stats.max_waiting_time)
        stats.max_waiting_time = yacht->waiting_time;
    pthread_mutex_unlock(&stats_mutex);

    free(yacht); // Free memory after yacht thread ends
    pthread_exit(NULL);
}

// Port crew thread function
void* port_crew_thread(void* arg) {
    PortCrew* crew = (PortCrew*)arg;
    while (1) {
        if (atomic_load(&crew->state) == 1) {
            // Simulate work for 10 seconds
            sleep(10);
            atomic_store(&crew->state, 0); // Go back to idle
            crew->yacht_id = -1;
        } else {
            sleep(1);
        }
    }
    pthread_exit(NULL);
}

// Add a yacht to the waiting queue
void add_to_queue(Yacht* yacht) {
    if (queue_size < MAX_QUEUE) {
        queue[queue_size++] = *yacht;
    }
}

// Check if a yacht can dock at a given position
int can_dock_here(int r, int c, int slots_length, int slots_width, int required_id) {
    for (int i = 0; i < slots_length; i++) {
        for (int j = 0; j < slots_width; j++) {
            if (r + i >= PORT_ROWS || c + j >= PORT_COLS)
                return 0;
            if (atomic_load(&port[r + i][c + j].occupied) != required_id)
                return 0;
        }
    }
    return 1;
}

// Find the best docking spot for a yacht
void find_best_docking_spot(int slots_length, int slots_width, int* best_r, int* best_c, int* best_quay_distance, int required_id) {
    *best_r = -1;
    *best_c = -1;
    *best_quay_distance = PORT_COLS * SLOT_SIZE;

    for (int r = 0; r <= PORT_ROWS - slots_length; r++) {
        for (int c = 0; c <= PORT_COLS - slots_width; c++) {
            if (can_dock_here(r, c, slots_length, slots_width, required_id)) {
                int min_distance = PORT_COLS * SLOT_SIZE;
                for (int j = c; j < c + slots_width; j++) {
                    int left = j, right = j;
                    int left_dist = PORT_COLS * SLOT_SIZE, right_dist = PORT_COLS * SLOT_SIZE;
                    while (left >= 0) {
                        if (atomic_load(&port[r][left].occupied) == -2) {
                            left_dist = j - left; break;
                        }
                        left--;
                    }
                    while (right < PORT_COLS) {
                        if (atomic_load(&port[r][right].occupied) == -2) {
                            right_dist = right - j; break;
                        }
                        right++;
                    }
                    int local_min = (left_dist < right_dist) ? left_dist : right_dist;
                    if (local_min < min_distance)
                        min_distance = local_min;
                }
                if (min_distance < *best_quay_distance) {
                    *best_quay_distance = min_distance;
                    *best_r = r;
                    *best_c = c;
                }
            }
        }
    }
}

// Assign a yacht to a port slot
void assign_to_port(Yacht* yacht) {
    pthread_mutex_lock(&port_mutex);

    int slots_length = ceil((double)yacht->length / SLOT_SIZE);
    int slots_width  = ceil((double)yacht->width / SLOT_SIZE);

    int best_r, best_c, best_quay_distance;
    int can_dock = 0, docked_on_fuel = 0;

    if (yacht->oil_level < 50) {
        // Only allow docking in slots with occupied == -3 (special low-oil dock)
        find_best_docking_spot(slots_length, slots_width, &best_r, &best_c, &best_quay_distance, -3);
        if (best_r != -1 && best_c != -1) {
            can_dock = 1; docked_on_fuel = 1;
        }
    } else {
        // Try normal docking first
        find_best_docking_spot(slots_length, slots_width, &best_r, &best_c, &best_quay_distance, -1);
        if (best_r != -1 && best_c != -1) {
            can_dock = 1;
        } else if (yacht->waiting_time >= 15) {
            // If waiting too long, allow docking at fuel station
            find_best_docking_spot(slots_length, slots_width, &best_r, &best_c, &best_quay_distance, -3);
            if (best_r != -1 && best_c != -1) can_dock = 1, docked_on_fuel = 1;
        }
    }

    if (can_dock) {
        for (int i = 0; i < slots_length; i++)
            for (int j = 0; j < slots_width; j++)
                atomic_store(&port[best_r + i][best_c + j].occupied, yacht->id);

        if (docked_on_fuel)
            atomic_store(&yacht->state, 4); // docked at fuel station
        else
            atomic_store(&yacht->state, 2); // docked

        // Remove yacht from the queue
        pthread_mutex_lock(&queue_mutex);
        for (int i = 0; i < queue_size; i++) {
            if (queue[i].id == yacht->id) {
                for (int j = i; j < queue_size - 1; j++)
                    queue[j] = queue[j + 1];
                queue_size--;
                break;
            }
        }
        pthread_mutex_unlock(&queue_mutex);

        // Add to docked list
        pthread_mutex_lock(&docked_mutex);
        if (docked_size < MAX_DOCKED)
            docked[docked_size++] = *yacht;
        pthread_mutex_unlock(&docked_mutex);
    }
    pthread_mutex_unlock(&port_mutex);
}

// Release a port slot when a yacht leaves
void release_slot(Yacht* yacht) {
    pthread_mutex_lock(&port_mutex);

    int slots_length = ceil((double)yacht->length / SLOT_SIZE);
    int slots_width = ceil((double)yacht->width / SLOT_SIZE);

    // Scan to find the top-left slot occupied by the yacht
    for (int r = 0; r <= PORT_ROWS - slots_length; r++) {
        for (int c = 0; c <= PORT_COLS - slots_width; c++) {
            int found = 1;
            for (int i = 0; i < slots_length; i++)
                for (int j = 0; j < slots_width; j++)
                    if (atomic_load(&port[r + i][c + j].occupied) != yacht->id) { found = 0; break; }
            if (found) {
                // Restore each slot according to the logic from main (oil pump or free)
                for (int i = 0; i < slots_length; i++)
                    for (int j = 0; j < slots_width; j++) {
                        int slot_r = r + i, slot_c = c + j;
                        int last_quay_col = -1, next_quay = 0, spacing = QUAY_LENGTH;
                        for (int qc = 0; qc <= slot_c; qc++)
                            if (qc == next_quay) { last_quay_col = qc; next_quay += spacing; spacing++; }
                        if (last_quay_col > floor(PORT_COLS / 2))
                            atomic_store(&port[slot_r][slot_c].occupied, -3); // oil pump
                        else
                            atomic_store(&port[slot_r][slot_c].occupied, -1); // free
                    }
                goto done;
            }
        }
    }
done:
    // Remove yacht from docked list safely
    pthread_mutex_lock(&docked_mutex);
    for (int i = 0; i < docked_size; i++) {
        if (docked[i].id == yacht->id) {
            for (int j = i; j < docked_size - 1; j++)
                docked[j] = docked[j + 1];
            docked_size--;
            break;
        }
    }
    pthread_mutex_unlock(&docked_mutex);

    pthread_mutex_unlock(&port_mutex);
}

// Display statistics for the port
void display_stats() {
    pthread_mutex_lock(&stats_mutex);
    mvprintw(0, 10, "Port statistics:");
    mvprintw(1, 10, "Yachts serviced: %d | Avg wait: %.2f s | Max wait: %d s | Cleanings: %d | Repairs: %d | Refuels: %d",
        stats.total_yachts_serviced,
        stats.total_yachts_serviced ? (double)stats.total_waiting_time / stats.total_yachts_serviced : 0.0,
        stats.max_waiting_time,
        stats.total_cleanings,
        stats.total_repairs,
        stats.total_refuels
    );
    pthread_mutex_unlock(&stats_mutex);
}

// Display thread for updating the port, queue, and docked list
void* display_thread(void* arg) {
    while (1) {
        pthread_mutex_lock(&port_mutex);
        pthread_mutex_lock(&queue_mutex);
        pthread_mutex_lock(&docked_mutex);
        clear();
        display_stats();
        display_port();
        display_queue();
        display_docked_list();
        display_port_crew_list();
        refresh();
        pthread_mutex_unlock(&docked_mutex);
        pthread_mutex_unlock(&queue_mutex);
        pthread_mutex_unlock(&port_mutex);
        usleep(1000000); // Refresh every 1 second
    }
    pthread_exit(NULL);
}

// Enhanced display of the port with color per yacht ID
void display_port() {
    mvprintw(3, 10, "Port:");
    for (int r = 0; r < PORT_ROWS; r++) {
        for (int c = 0; c < PORT_COLS; c++) {
            int yacht_id = atomic_load(&port[r][c].occupied);

            if (yacht_id == -2) {
                // Quay area displayed in strict white on black
                attron(COLOR_PAIR(7));
                mvprintw(5 + r, 10 + c * 6, "[||||]");
                attroff(COLOR_PAIR(7));
            }
            else if (yacht_id == -3) {
                // Oil pump slot
                attron(COLOR_PAIR(8));
                mvprintw(5 + r, 10 + c * 6, "[ OIL]");
                attroff(COLOR_PAIR(8));
            }
            else if (yacht_id != -1) {
                // Occupied by a yacht
                int color_pair = (yacht_id % 5) + 2;
                attron(COLOR_PAIR(color_pair));
                mvprintw(5 + r, 10 + c * 6, "[%4d]", yacht_id);
                attroff(COLOR_PAIR(color_pair));
            } else {
                // Free slot
                attron(COLOR_PAIR(1));
                mvprintw(5 + r, 10 + c * 6, "[    ]");
                attroff(COLOR_PAIR(1));
            }
        }
    }
}

// Display the waiting queue
void display_queue() {
    attron(COLOR_PAIR(2));
    mvprintw(27, 10, "Waiting Queue:");
    for (int i = 0; i < queue_size; i++) {
        char needs[32] = "";
        if (queue[i].need_cleaning && queue[i].need_repair)
            strcpy(needs, "Cleaning,Repair");
        else if (queue[i].need_cleaning)
            strcpy(needs, "Cleaning");
        else if (queue[i].need_repair)
            strcpy(needs, "Repair");
        else
            strcpy(needs, "None");
        mvprintw(29 + i, 10, "ID:%d Size:%dmx%dm Oil:%d%% Needs:%s Wait:%ds",
            queue[i].id, queue[i].length, queue[i].width, queue[i].oil_level, needs, queue[i].waiting_time);
    }
    attroff(COLOR_PAIR(2));
}

// Display the list of docked yachts
void display_docked_list() {
    attron(COLOR_PAIR(3));
    mvprintw(27, 60, "Docked Yachts:");
    for (int i = 0; i < docked_size; i++) {
        char needs[32] = "";
        if (docked[i].need_cleaning && docked[i].need_repair)
            strcpy(needs, "Cleaning,Repair");
        else if (docked[i].need_cleaning)
            strcpy(needs, "Cleaning");
        else if (docked[i].need_repair)
            strcpy(needs, "Repair");
        else
            strcpy(needs, "None");
        mvprintw(29 + i, 60, "ID:%d Size:%dmx%dm Oil:%d%% Needs:%s",
            docked[i].id, docked[i].length, docked[i].width, docked[i].oil_level, needs);
    }
    attroff(COLOR_PAIR(3));
}

// Display the port crew list
void display_port_crew_list() {
    attron(COLOR_PAIR(4));
    mvprintw(27, 110, "Port Crew:");
    for (int i = 0; i < MAX_CREWS; i++) {
        char* job = crews[i].job_id == 1 ? "Cleaning" : "Repair";
        char* state;
        if (atomic_load(&crews[i].state) == 0)
            state = "Idle";
        else if (atomic_load(&crews[i].state) == 1)
            state = "Working";
        else
            state = "Waiting";
        mvprintw(29 + i, 110, "CrewID:%d Type:%s State:%s YachtID:%d",
            i, job, state, crews[i].yacht_id >= 0 ? crews[i].yacht_id : -1);
    }
    attroff(COLOR_PAIR(4));
}

int main() {
    srand(time(NULL));
    init_ncurses();
    int spacing = QUAY_LENGTH; // initial spacing between quays

    // Initialize port slots with quay, oil pump, or free status
    for (int r = 0; r < PORT_ROWS; r++) {
        int next_quay = 0;
        int spacing = QUAY_LENGTH;
        int last_quay_col = PORT_COLS;

        for (int c = 0; c < PORT_COLS; c++) {
            port[r][c].row = r;
            port[r][c].col = c;

            if (c == next_quay) {
                atomic_store(&port[r][c].occupied, -2); // quay
                last_quay_col = c;
                next_quay += spacing;
                spacing++;
            } else {
                if(last_quay_col > floor(PORT_COLS/2)){
                    atomic_store(&port[r][c].occupied, -3); // oil pump
                }
                else{
                    atomic_store(&port[r][c].occupied, -1); // free
                }
            }
        }
    }

    // Initialize cleaning and repair crews BEFORE creating yachts
    for (int i = 0; i < MAX_CREWS; i++) {
        crews[i].id = i; // Crew identifier
        crews[i].yacht_id = -1; // No yacht assigned at start
        crews[i].crew_size = 3;
        atomic_store(&crews[i].state, 0); // 0=idle
        crews[i].job_id = (i < MAX_CREWS/2) ? 1 : 2; // first half cleaning, rest repair
        pthread_t crew_tid;
        pthread_create(&crew_tid, NULL, port_crew_thread, &crews[i]);
        pthread_detach(crew_tid); // Detach since we never join crew threads
    }

    // Create the display thread
    pthread_t display_tid;
    pthread_create(&display_tid, NULL, display_thread, NULL);

    // Dynamically create yacht threads
    int yacht_id = 1;
    while (1) {
        Yacht* yacht = (Yacht*)malloc(sizeof(Yacht));
        yacht->id = yacht_id++;
        yacht->length = rand() % (YACHT_MAX_LENGTH - YACHT_MIN_LENGTH + 1) + YACHT_MIN_LENGTH;
        yacht->width = rand() % (YACHT_MAX_WIDTH - YACHT_MIN_WIDTH + 1) + YACHT_MIN_WIDTH;
        yacht->oil_level = rand() % 99 + 1;
        yacht->waiting_time = 0;

        atomic_store(&yacht->state, 1);   // Initial state: waiting
        yacht->need_cleaning = (rand() % 10 == 0); // ~10%
        yacht->need_repair = (rand() % 10 == 0);   // ~10%

        pthread_t yacht_tid;
        pthread_create(&yacht_tid, NULL, yacht_thread, yacht);
        pthread_detach(yacht_tid); // Detach since we never join yacht threads

        // Allow to exit program by pressing 'q' or 'Q'
        timeout(0);
        int ch = getch();
        if (ch == 'q' || ch == 'Q') break;

        usleep(5000000); // 5 seconds between new yachts
    }

    cleanup_ncurses();
    return 0;
}