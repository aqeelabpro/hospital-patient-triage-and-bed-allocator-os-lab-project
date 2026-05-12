/*
 * ============================================================
 * Project : Hospital Patient Triage & Bed Allocator
 * File    : hospital.h
 * Group 22 | Members: Member1 (24P-0514), Member2 (24P-0666)
 * Date    : 2026-05-05
 * Purpose : Shared constants, structs, and includes used
 *           by both admissions.c and patient_simulator.c
 * ============================================================
 */

#ifndef HOSPITAL_H
#define HOSPITAL_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <time.h>
#include <pthread.h>
#include <semaphore.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <errno.h>

#define ICU_BEDS        4
#define ISOLATION_BEDS  4
#define GENERAL_BEDS    12
#define TOTAL_BEDS      (ICU_BEDS + ISOLATION_BEDS + GENERAL_BEDS)

#define ICU_UNITS       3
#define ISOLATION_UNITS 2
#define GENERAL_UNITS   1
#define TOTAL_UNITS     (ICU_BEDS * ICU_UNITS + ISOLATION_BEDS * ISOLATION_UNITS + GENERAL_BEDS * GENERAL_UNITS)

#define PAGE_SIZE       2

#define MAX_PATIENTS    64
#define MAX_QUEUE       32

#define SHM_KEY         0xBEDF00D
#define DISCHARGE_FIFO  "/tmp/discharge_fifo"
#define SEM_ICU         "/sem_icu_limit"
#define SEM_ISO         "/sem_iso_limit"
#define SEM_QUEUE       "/sem_queue_bound"
#define INTAKE_FIFO     "/tmp/intake_fifo"

#define STRAT_BEST  0
#define STRAT_FIRST 1
#define STRAT_WORST 2

typedef struct {
    int patient_id;
    char name[64];
    int age;
    int severity;
    int priority;
    int care_units;
    time_t arrival_time;
} PatientRecord;

typedef struct {
    int partition_id;
    int start_unit;
    int size;
    int is_free;
    int patient_id;
    char bed_type[16];
} BedPartition;

typedef struct {
    BedPartition beds[TOTAL_BEDS];
    int ward[TOTAL_UNITS];
    int total_served;
    int running;
} SharedWard;

typedef struct PriorityQueue {
    PatientRecord rec;
    struct PriorityQueue *next;
} PriorityQueue;

typedef struct {
    int patient_id;
    int priority;
    double arrival;
    double start;
    double finish;
} ScheduleEntry;

#endif