/*
 * ============================================================
 * Project : Hospital Patient Triage & Bed Allocator
 * File    : patient_simulator.c
 * Group 22 | Members: Member1 (24P-0514), Member2 (24P-0666)
 * Date    : 2026-05-03
 * Purpose : Simulates a single patient receiving treatment.
 *           Spawned by admissions via fork() + execv().
 *           After treatment, sends a discharge notification
 *           to admissions through the named FIFO.
 * Compile : gcc -Wall -Wextra -o patient_simulator patient_simulator.c
 * Usage   : ./patient_simulator <patient_id> <priority> <bed_id> <bed_type>
 * ============================================================
 */

#include "hospital.h"

int main(int argc, char *argv[])
{
    if (argc < 5)
    {
        fprintf(stderr, "Usage: %s <patient_id> <priority> <bed_id> <bed_type>\n", argv[0]);
        return 1;
    }

    int patientId = atoi(argv[1]);
    int triagePriority = atoi(argv[2]);
    int assignedBedId = atoi(argv[3]);
    char *assignedBedType = argv[4];

    srand((unsigned)(time(NULL) ^ getpid()));

    int minDuration, maxDuration;

    if (strcmp(assignedBedType, "ICU") == 0)
    {
        minDuration = 5;
        maxDuration = 15;
    }
    else if (strcmp(assignedBedType, "ISOLATION") == 0)
    {
        minDuration = 3;
        maxDuration = 10;
    }
    else
    {
        minDuration = 2;
        maxDuration = 8;
    }

    int treatmentDuration = minDuration + rand() % (maxDuration - minDuration + 1);

    printf("[PATIENT %d] Arrived    | Priority=%d | Bed=%d (%s)\n",
           patientId, triagePriority, assignedBedId, assignedBedType);
    fflush(stdout);

    printf("[PATIENT %d] Treatment  | Duration=%d seconds\n",
           patientId, treatmentDuration);
    fflush(stdout);

    sleep(treatmentDuration);

    printf("[PATIENT %d] Discharged | Bed=%d now free\n",
           patientId, assignedBedId);
    fflush(stdout);

    int fifoDescriptor = open(DISCHARGE_FIFO, O_WRONLY);
    if (fifoDescriptor < 0)
    {
        perror("[PATIENT] open FIFO failed");
        return 1;
    }

    write(fifoDescriptor, &patientId, sizeof(int));
    write(fifoDescriptor, &assignedBedId, sizeof(int));
    close(fifoDescriptor);

    printf("[PATIENT %d] Discharge notification sent\n", patientId);
    fflush(stdout);

    return 0;
}