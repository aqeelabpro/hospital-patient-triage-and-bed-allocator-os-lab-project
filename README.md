# Hospital Patient Triage & Bed Allocator

**Project:** CL2006 Operating Systems Lab Semester Project  
**Group:** Group 22  
**Members:** Member1 (24P-0514), Member2 (24P-0666)  
**Date:** Spring 2026

## Overview

A C-based system-level simulation of a hospital emergency room that integrates core OS concepts:
- **Process Management**: fork() + execv() per patient arrival
- **IPC**: pipes, named FIFO, shared memory
- **Synchronization**: mutexes, condition variables, semaphores
- **Scheduling**: priority queue with multiple allocation strategies
- **Memory Management**: Best-Fit, First-Fit, Worst-Fit allocation with coalescing

## Dependencies

- GCC compiler with pthread support
- Linux/Unix environment (Debian, Ubuntu recommended)
- Bash shell
- Python 3 (for patient record serialization in triage.sh)

## Build Instructions

```bash
# Compile all binaries
make all

# Or individually:
gcc -Wall -Wextra -pthread -I./src -o admissions src/admissions.c -lpthread
gcc -Wall -Wextra -I./src -o patient_simulator src/patient_simulator.c
```

## Running the System

### Start the hospital:
```bash
./scripts/start_hospital.sh
# Optional: specify allocation strategy
./scripts/start_hospital.sh --strategy best    # default: Best-Fit
./scripts/start_hospital.sh --strategy first   # First-Fit
./scripts/start_hospital.sh --strategy worst   # Worst-Fit
```

### Admit a patient:
```bash
./scripts/triage.sh <name> <age> <severity>
# Example:
./scripts/triage.sh Alice 25 9     # Priority 1 (ICU)
./scripts/triage.sh Bob 40 6       # Priority 2 (General)
./scripts/triage.sh Carol 31 3     # Priority 5 (General)
```

### Run automated stress test (20 patients):
```bash
make test
# Or manually:
./scripts/stress_test.sh
```

### Stop the hospital and print summary:
```bash
./scripts/stop_hospital.sh
```

## Output Files

After running:
- `logs/schedule_log.txt` - Scheduling statistics (wait time, turnaround time, averages)
- `logs/memory_log.txt` - Fragmentation statistics after each allocation/deallocation

## Project Structure

```
.
├── src/
│   ├── admissions.c          (457 lines) - Central manager, threads, memory
│   ├── patient_simulator.c   (82 lines)  - Patient lifecycle simulation
│   └── hospital.h            (122 lines) - Shared structs & constants
├── scripts/
│   ├── triage.sh             (75 lines)  - Patient triage & validation
│   ├── start_hospital.sh     (51 lines)  - IPC setup & launch
│   ├── stop_hospital.sh      (50 lines)  - Graceful shutdown
│   └── stress_test.sh        (34 lines)  - 20 rapid arrivals
├── Makefile
└── README.md
```

## System Architecture

### Ward Configuration
- **ICU Beds**: 4 beds × 3 care units = 12 units (priority 1-2)
- **Isolation Beds**: 4 beds × 2 care units = 8 units (priority 3)
- **General Ward**: 12 beds × 1 care unit = 12 units (priority 4-5)
- **Total**: 20 beds, 32 care units

### Triage Priority Mapping
- **1 (Critical)**: Severity 9-10 → ICU
- **2 (Urgent)**: Severity 7-8 → ICU
- **3 (Moderate)**: Severity 5-6 → Isolation
- **4 (Minor)**: Severity 3-4 → General
- **5 (Low)**: Severity 1-2 → General

### Thread Roles
- **Receptionist**: Reads patients from stdin, enqueues to priority queue (producer)
- **Scheduler**: Dequeues patients, allocates beds, forks patient_simulator (consumer)
- **Nurse (3×)**: One per ward type; frees beds, signals scheduler, releases capacity

## OS Concepts Demonstrated

| Concept | Implementation |
|---------|-----------------|
| fork() / execv() | Patient spawning (line 284-294 in admissions.c) |
| SIGCHLD | Zombie reaping (line 188) |
| Pipe (stdin) | triage.sh → admissions (line 242) |
| Named FIFO | Discharge notifications (line 271-278) |
| Shared Memory | Ward bitmap (shmget/shmat) (line 318-320) |
| Mutex | Bed bitmap protection (line 298, 300, 306) |
| Condition Variable | bed_freed signal (line 303, 309) |
| Semaphores | ICU/Isolation limits, bounded queue (line 327-331) |
| Priority Queue | Sorted linked list (line 172-190) |
| Memory Allocation | Best/First/Worst-Fit (line 74, 85, 98) |
| Coalescing | Left + right merge (line 128-145) |
| Fragmentation | External & internal reporting (line 148-172) |

## Testing

Run the stress test to verify:
- 20 concurrent patient arrivals
- Priority-based scheduling
- Bed allocation under load
- Coalescing after discharge
- Fragmentation tracking

```bash
make test
# Check logs/schedule_log.txt and logs/memory_log.txt for results
```

## Compilation Flags

- `-Wall -Wextra`: All warnings enabled
- `-pthread`: Thread support
- `-I./src`: Include path for headers

## Known Limitations

- Paging is simulated (page size = 2 care units)
- Treatment duration is randomized per bed type
- Scheduling log uses a simple simulated clock

## Future Extensions

- mmap-based patient record persistence
- Multi-ward support
- Pharmacy module integration
- Advanced scheduling (Round Robin, EDF)
