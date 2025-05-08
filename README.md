🚢 Yacht Port Simulation
A multithreaded C program that simulates a yacht port using POSIX threads and Ncurses for real-time visualization. Yachts arrive at the port, queue for docking, occupy available slots, stay docked for a period, and then leave.

🧠 Features
Dynamic arrival and docking of yachts

Circular queue implementation for waiting yachts

Slot-based port with size constraints for docking

Real-time terminal display using ncurses

Thread-safe operations using mutexes and atomic operations

📦 Requirements
GCC or any C compiler supporting POSIX threads

Linux/macOS terminal

ncurses library

Install ncurses on Ubuntu:

bash
Kopiuj
Edytuj
sudo apt install libncurses5-dev libncursesw5-dev
🔧 Compilation
bash
Kopiuj
Edytuj
gcc -o yacht_port_simulation yacht_port_simulation.c -lpthread -lncurses
▶️ Running
bash
Kopiuj
Edytuj
./yacht_port_simulation
🏗️ Project Structure

Component	Description
Yacht	Struct with yacht ID, size, and state (waiting, docked, leaving)
PortSlot	Represents one docking slot in the port grid
queue[]	Fixed-size waiting queue for yachts
docked[]	Fixed-size list of currently docked yachts
port[][]	2D grid representing the physical port layout
pthread_mutex_t	Ensures safe access to shared data
ncurses	Updates real-time port, queue, and docked yacht views
📊 Display (Ncurses)
The terminal display updates every 0.5 seconds:

Port Grid: Shows free [ ] and occupied [X] slots

Waiting Queue: List of yachts waiting to dock

Docked Yachts: List of currently docked yachts

🔁 Yacht Lifecycle
Each yacht runs in a separate thread:

Arrives at the port after a random delay

Enters the waiting queue

Waits for a slot with sufficient size

Docks and stays for 10–20 seconds

Leaves and frees the slot

⚙️ Configurable Constants
You can modify these constants in the code to tune simulation behavior:

c
Kopiuj
Edytuj
#define NUM_YACHTS 20      // Total yachts to simulate
#define PORT_ROWS 5        // Port grid rows
#define PORT_COLS 8        // Port grid columns
#define MAX_QUEUE 10       // Max yachts in waiting queue
#define SLOT_LENGTH 50     // Port slot length (meters)
#define SLOT_WIDTH 10      // Port slot width (meters)
💡 Ideas for Extension
Introduce priority queuing (e.g., VIP yachts)

Track total wait/dock times per yacht

Use semaphores or condition variables instead of polling

Add GUI visualization (e.g., SDL or OpenGL)

🧹 Clean Exit
The program automatically cleans up:

All yacht threads are joined

Display thread is cancelled and joined

ncurses interface is shut down

📸 Screenshot (Optional)
You can add a screenshot here if you want:

csharp
Kopiuj
Edytuj
[PORT DISPLAY]
[WAITING QUEUE]
[DOCKED YACHTS]
