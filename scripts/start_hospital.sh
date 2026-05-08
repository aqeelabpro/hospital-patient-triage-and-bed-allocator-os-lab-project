#!/bin/bash
# Hospital Patient Triage & Bed Allocator - start_hospital.sh
# Group 22 | Members: Member1 (24P-0514), Member2 (24P-0666)
# Initialize IPC resources and launch admissions manager

STRATEGY="best"
if [ "$1" = "--strategy" ] && [ -n "$2" ]; then
    STRATEGY="$2"
fi

echo "  Hospital Triage System Starting"
echo "  Strategy: $STRATEGY"

# Remove System V Process Shared Memory
ipcrm -M 0xbedf00d 2>/dev/null

rm -f /tmp/discharge_fifo

mkdir -p logs

if [ ! -f admissions ]; then
    echo "ERROR: admissions binary not found. Run: make all"
    exit 1
fi

if [ ! -f patient_simulator ]; then
    echo "ERROR: patient_simulator binary not found. Run: make all"
    exit 1
fi

./admissions --strategy "$STRATEGY" &
PID=$!

echo $PID > /tmp/hospital_pid

echo "Admissions manager started (PID=$PID)"

# Wait for FIFO to be created
sleep 1

if [ -p /tmp/discharge_fifo ]; then
    echo "System ready. FIFO created."
else
    echo "WARNING: FIFO not ready yet"
fi

echo "Ward: 4 ICU | 4 Isolation | 12 General"
echo "Stop: ./scripts/stop_hospital.sh"
