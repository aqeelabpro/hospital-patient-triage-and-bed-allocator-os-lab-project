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

    int uid         = atoi(argv[1]);
    int severity    = atoi(argv[2]);
    int slot_num    = atoi(argv[3]);
    char *ward_zone = argv[4];

    srand((unsigned)(time(NULL) ^ getpid()));

    int min_recovery, max_recovery;

    if (strcmp(ward_zone, "ICU") == 0)
    {
        min_recovery = 5;
        max_recovery = 15;
    }
    else if (strcmp(ward_zone, "ISOLATION") == 0)
    {
        min_recovery = 3;
        max_recovery = 10;
    }
    else
    {
        min_recovery = 2;
        max_recovery = 8;
    }

    int recovery_secs = min_recovery + rand() % (max_recovery - min_recovery + 1);

    printf("[PATIENT %d] Arrived    | Severity=%d | Slot=%d (%s)\n",
           uid, severity, slot_num, ward_zone);
    fflush(stdout);

    printf("[PATIENT %d] Treatment  | Duration=%d seconds\n",
           uid, recovery_secs);
    fflush(stdout);

    sleep(recovery_secs);

    printf("[PATIENT %d] Discharged | Slot=%d now free\n",
           uid, slot_num);
    fflush(stdout);

    int fifo_fd = open(DISCHARGE_FIFO, O_WRONLY);
    if (fifo_fd < 0)
    {
        perror("[PATIENT] open FIFO failed");
        return 1;
    }

    write(fifo_fd, &uid,      sizeof(int));
    write(fifo_fd, &slot_num, sizeof(int));
    close(fifo_fd);

    printf("[PATIENT %d] Discharge notification sent\n", uid);
    fflush(stdout);

    return 0;
}
