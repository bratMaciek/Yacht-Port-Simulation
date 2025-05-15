# 🚢 Yacht Port Simulation

A multithreaded C program that simulates a yacht port using **POSIX threads** and **Ncurses** for real-time visualization. Yachts arrive at the port, queue for docking, occupy available slots, stay docked for a period, and then leave.

---

## 🧠 Features

- Dynamic arrival and docking of yachts
- Circular queue implementation for waiting yachts
- Slot-based port with size constraints for docking
- Real-time terminal display using `ncurses`
- Thread-safe operations using mutexes and atomic operations

---

## 📦 Requirements

- GCC or any C compiler supporting POSIX threads
- Linux/macOS terminal
- `ncurses` library

Install `ncurses` on Ubuntu:

```bash
sudo apt install libncurses5-dev libncursesw5-dev
```
### Compilation
```bash
gcc -o port_simulation port_simulation.c -lm -lpthread -lncurses -g
