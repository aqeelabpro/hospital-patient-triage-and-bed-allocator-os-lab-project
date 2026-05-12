#!/bin/bash
# Hospital Patient Triage & Bed Allocator - stress_test.sh
# Group 22 | Members: Member1 (24P-0514), Member2 (24P-0666)
# Automated stress test: 20 rapid patient arrivals

echo "Stress test: sending 20 patients..."

send() {
    sleep "$4"
    ./scripts/triage.sh "$1" "$2" "$3"
}

send Alice  25 9  0.0  &
send Bob    40 6  0.1  &
send Carol  31 8  0.2  &
send Dave   55 4  0.3  &
send Eve    22 10 0.4  &
send Frank  60 3  0.5  &
send Grace  45 7  0.6  &
send Hank   50 5  0.7  &
send Iris   28 9  0.8  &
send Jack   33 2  0.9  &
send Kate   41 6  1.0  &
send Leo    37 8  1.1  &
send Mia    19 10 1.2  &
send Nick   65 4  1.3  &
send Olivia 30 7  1.4  &
send Pat    52 5  1.5  &
send Quinn  48 6  1.6  &
send Rose   26 9  1.7  &
send Sam    70 3  1.8  &
send Tina   35 8  1.9  &

echo "20 patients dispatched"
wait
echo "Stress test complete"