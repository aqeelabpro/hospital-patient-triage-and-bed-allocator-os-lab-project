#!/bin/bash
# Hospital Patient Triage & Bed Allocator - stop_hospital.sh
# Group 22 | Members: Member1 (24P-0514), Member2 (24P-0666)
# Gracefully stop hospital and clean up all IPC resources

echo "Shutting down Hospital System..."

# Stop the admissions manager
if [ -f /tmp/hospital_pid ]; then
    PID=$(cat /tmp/hospital_pid)
    kill $PID 2>/dev/null
    echo "Stopped admissions (PID=$PID)"
    rm -f /tmp/hospital_pid
else
    echo "No PID file found"
fi

# Stop any patient processes
pkill -f patient_simulator 2>/dev/null
echo "Patient processes stopped"

# Clean up shared memory
ipcrm -M 0xbedf00d 2>/dev/null
echo "Shared memory cleaned"

# Clean up FIFOs
rm -f /tmp/discharge_fifo
rm -f /tmp/intake_fifo
rm -f /tmp/intake_fifo.lock
echo "FIFOs cleaned"

# Clean up semaphores
rm -f /dev/shm/sem_icu_limit
rm -f /dev/shm/sem_iso_limit
rm -f /dev/shm/sem_queue_bound
echo "Semaphores cleaned"

# Print summary
echo ""
echo "====== Final Summary ======"

if [ -f logs/schedule_log.txt ]; then
    echo "--- Schedule Log ---"
    tail -3 logs/schedule_log.txt
fi

if [ -f logs/memory_log.txt ]; then
    echo "--- Memory Log ---"
    tail -1 logs/memory_log.txt
fi

echo "Hospital stopped."