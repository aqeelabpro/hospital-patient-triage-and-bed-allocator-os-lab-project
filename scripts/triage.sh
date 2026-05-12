#!/bin/bash
# Hospital Patient Triage & Bed Allocator - triage.sh
# Group 22 | Members: Member1 (24P-0514), Member2 (24P-0666)
# Validates patient input and sends to admissions via intake FIFO

if [ $# -ne 3 ]; then
    echo "Usage: ./triage.sh <name> <age> <severity>"
    exit 1
fi

NAME=$1
AGE=$2
SEVERITY=$3

# Validate name not empty
if [ -z "$NAME" ]; then
    echo "ERROR: Name cannot be empty"
    exit 1
fi

# Validate age 0-120
if [ "$AGE" -lt 0 ] || [ "$AGE" -gt 120 ]; then
    echo "ERROR: Age must be 0-120"
    exit 1
fi

# Validate severity 1-10
if [ "$SEVERITY" -lt 1 ] || [ "$SEVERITY" -gt 10 ]; then
    echo "ERROR: Severity must be 1-10"
    exit 1
fi

# Map severity to priority: higher severity = lower number
if   [ "$SEVERITY" -ge 9 ]; then PRIORITY=1
elif [ "$SEVERITY" -ge 7 ]; then PRIORITY=2
elif [ "$SEVERITY" -ge 5 ]; then PRIORITY=3
elif [ "$SEVERITY" -ge 3 ]; then PRIORITY=4
else                              PRIORITY=5
fi

ARRIVAL=$(date +%s)

echo "Patient  : $NAME"
echo "Age      : $AGE"
echo "Severity : $SEVERITY / 10"
echo "Priority : $PRIORITY"
echo "Time     : $(date)"

# Check if hospital is running (intake FIFO must exist)
if [ ! -p /tmp/intake_fifo ]; then
    echo "ERROR: Hospital not running (intake FIFO not found)"
    exit 1
fi

python3 -c "
import struct, fcntl

name     = '$NAME'[:63].encode().ljust(64, b'\x00')
age      = $AGE
severity = $SEVERITY
priority = $PRIORITY

if priority <= 2:
    care = 3
elif priority == 3:
    care = 2
else:
    care = 1

# 'i64siiii4xq' matches C struct layout with padding:
# i    = patient_id (4)
# 64s  = name (64)
# i    = age (4)
# i    = severity (4)
# i    = priority (4)
# i    = care_units (4)
# 4x   = 4 padding bytes (compiler inserts before time_t)
# q    = arrival_time (8)
# Total = 96 bytes, matching sizeof(PatientRecord)
data = struct.pack('i64siiii4xq', 0, name, age, severity, priority, care, $ARRIVAL)

with open('/tmp/intake_fifo.lock', 'w') as lock:
    fcntl.flock(lock, fcntl.LOCK_EX)
    with open('/tmp/intake_fifo', 'wb') as f:
        f.write(data)
        f.flush()
    fcntl.flock(lock, fcntl.LOCK_UN)
"

echo "Patient record sent"