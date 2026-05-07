#!/bin/bash
# Hospital Patient Triage & Bed Allocator - stress_test.sh
# Group 22 | Members: Member1 (24P-0514), Member2 (24P-0666)
# Automated stress test: 20 rapid patient arrivals

echo "Stress test: sending 20 patients..."

./scripts/triage.sh Alice  25 9  &
./scripts/triage.sh Bob    40 6  &
./scripts/triage.sh Carol  31 8  &
./scripts/triage.sh Dave   55 4  &
./scripts/triage.sh Eve    22 10 &
./scripts/triage.sh Frank  60 3  &
./scripts/triage.sh Grace  45 7  &
./scripts/triage.sh Hank   50 5  &
./scripts/triage.sh Iris   28 9  &
./scripts/triage.sh Jack   33 2  &
./scripts/triage.sh Kate   41 6  &
./scripts/triage.sh Leo    37 8  &
./scripts/triage.sh Mia    19 10 &
./scripts/triage.sh Nick   65 4  &
./scripts/triage.sh Olivia 30 7  &
./scripts/triage.sh Pat    52 5  &
./scripts/triage.sh Quinn  48 6  &
./scripts/triage.sh Rose   26 9  &
./scripts/triage.sh Sam    70 3  &
./scripts/triage.sh Tina   35 8  &

echo "20 patients dispatched"

# Wait for all background jobs to complete
wait

echo "Stress test complete"
