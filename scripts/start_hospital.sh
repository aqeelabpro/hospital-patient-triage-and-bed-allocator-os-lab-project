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

# Remove any leftover shared memory and FIFOs
ipcrm -M 0xbedf00d 2>/dev/null
rm -f /tmp/discharge_fifo
rm -f /tmp/intake_fifo
rm -f /tmp/intake_fifo.lock

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

# Wait for both FIFOs to be created by admissions
sleep 1

READY=1
if [ ! -p /tmp/discharge_fifo ]; then
    echo "WARNING: discharge FIFO not ready"
    READY=0
fi
if [ ! -p /tmp/intake_fifo ]; then
    echo "WARNING: intake FIFO not ready"
    READY=0
fi

if [ "$READY" -eq 1 ]; then
    echo "System ready. Both FIFOs created."
fi

echo "Ward: 4 ICU | 4 Isolation | 12 General"
echo "Stop: ./scripts/stop_hospital.sh"