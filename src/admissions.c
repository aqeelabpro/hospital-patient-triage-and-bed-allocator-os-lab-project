/*
 * Hospital Patient Triage & Bed Allocator - admissions.c
 * Group 22 | Members: Member1 (24P-0514), Member2 (24P-0666)
 * Central admissions manager: IPC, threads, scheduling, memory
 */

#include "hospital.h"

static int shm_id = -1;
static SharedWard *ward_shared_memory = NULL;
static int strategy = STRAT_BEST;

static PriorityQueue *priority_queue_head = NULL;
static pthread_mutex_t priority_queue_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t priority_queue_cond = PTHREAD_COND_INITIALIZER;

static pthread_mutex_t bed_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t bed_freed = PTHREAD_COND_INITIALIZER;

static sem_t *sem_icu = SEM_FAILED;
static sem_t *sem_iso = SEM_FAILED;
static sem_t *sem_queue = SEM_FAILED;

static ScheduleEntry schedule_log[MAX_PATIENTS];
static int schedule_count = 0;
static int next_patient_id = 1;
static volatile int shutdown_flag = 0;

void init_ward(void) {
    memset(ward_shared_memory, 0, sizeof(SharedWard));
    ward_shared_memory->running = 1;
    for (int i = 0; i < TOTAL_UNITS; i++) ward_shared_memory->ward[i] = -1;

    int unit = 0, bid = 0;
    for (int i = 0; i < ICU_BEDS; i++, bid++) {
        ward_shared_memory->beds[bid] = (BedPartition){bid, unit, ICU_UNITS, 1, -1, "ICU"};
        unit += ICU_UNITS;
    }
    for (int i = 0; i < ISOLATION_BEDS; i++, bid++) {
        ward_shared_memory->beds[bid] = (BedPartition){bid, unit, ISOLATION_UNITS, 1, -1, "ISOLATION"};
        unit += ISOLATION_UNITS;
    }
    for (int i = 0; i < GENERAL_BEDS; i++, bid++) {
        ward_shared_memory->beds[bid] = (BedPartition){bid, unit, GENERAL_UNITS, 1, -1, "GENERAL"};
        unit += GENERAL_UNITS;
    }
    printf("[WARD] Init: %d ICU + %d ISO + %d GEN = %d beds, %d units\n",
           ICU_BEDS, ISOLATION_BEDS, GENERAL_BEDS, TOTAL_BEDS, TOTAL_UNITS);
}

int find_bed_best_strategy(int care, const char *type) {
    int best = -1, best_size = 9999;
    for (int i = 0; i < TOTAL_BEDS; i++) {
        BedPartition *b = &ward_shared_memory->beds[i];
        if (b->is_free && strcmp(b->bed_type, type) == 0 && 
            b->size >= care && b->size < best_size) {
            best = i; 
            best_size = b->size;
        }
    }
    return best;
}

int find_bed_first_strategy(int care, const char *type) {
    for (int i = 0; i < TOTAL_BEDS; i++) {
        BedPartition *b = &ward_shared_memory->beds[i];
        if (b->is_free && strcmp(b->bed_type, type) == 0 && b->size >= care)
            return i;
    }
    return -1;
}

int find_bed_worst_strategy(int care, const char *type) {
    int worst = -1, worst_size = -1;
    for (int i = 0; i < TOTAL_BEDS; i++) {
        BedPartition *b = &ward_shared_memory->beds[i];
        if (b->is_free && strcmp(b->bed_type, type) == 0 && 
            b->size >= care && b->size > worst_size) {
            worst = i; worst_size = b->size;
        }
    }
    return worst;
}

int allocate_bed(int care, const char *type) {
    if (strategy == STRAT_FIRST) return find_bed_first_strategy(care, type);
    if (strategy == STRAT_WORST) return find_bed_worst_strategy(care, type);
    return find_bed_best_strategy(care, type);
}

void occupy_bed(int bed_idx, int patient_id) {
    BedPartition *b = &ward_shared_memory->beds[bed_idx];
    b->is_free = 0;
    b->patient_id = patient_id;
    for (int u = b->start_unit; u < b->start_unit + b->size; u++)
        ward_shared_memory->ward[u] = patient_id;
}

void free_bed(int bed_idx) {
    BedPartition *b = &ward_shared_memory->beds[bed_idx];
    b->is_free = 1;
    b->patient_id = -1;
    for (int u = b->start_unit; u < b->start_unit + b->size; u++)
        ward_shared_memory->ward[u] = -1;
    
    /* Coalesce left */
    if (bed_idx > 0 && ward_shared_memory->beds[bed_idx-1].is_free && 
        strcmp(ward_shared_memory->beds[bed_idx-1].bed_type, b->bed_type) == 0) {
        ward_shared_memory->beds[bed_idx-1].size += b->size;
        b->size = 0;
        b = &ward_shared_memory->beds[bed_idx-1];
    }
    /* Coalesce right */
    if (bed_idx + 1 < TOTAL_BEDS && ward_shared_memory->beds[bed_idx+1].is_free && 
        strcmp(ward_shared_memory->beds[bed_idx+1].bed_type, b->bed_type) == 0 && 
        ward_shared_memory->beds[bed_idx+1].size > 0) {
        b->size += ward_shared_memory->beds[bed_idx+1].size;
        ward_shared_memory->beds[bed_idx+1].size = 0;
    }
    printf("[COALESCE] Bed %d freed\n", bed_idx);
}

void report_fragmentation(void) {
    int total = 0, largest = 0, cur = 0;
    for (int i = 0; i < TOTAL_UNITS; i++) {
        if (ward_shared_memory->ward[i] == -1) { cur++; total++; }
        else { if (cur > largest) largest = cur; cur = 0; }
    }
    if (cur > largest) largest = cur;
    
    double frag = (total > 0) ? (1.0 - (double)largest / total) * 100.0 : 0.0;
    printf("[FRAG] Free=%d Largest=%d ExtFrag=%.1f%%\n", total, largest, frag);
    
    FILE *fp = fopen("logs/memory_log.txt", "a");
    if (fp) {
        fprintf(fp, "Free=%d Largest=%d ExtFrag=%.1f%%\n", total, largest, frag);
        fclose(fp);
    }
}

void report_paging(int patient_id, int care_units) {
    int pages = (care_units + PAGE_SIZE - 1) / PAGE_SIZE;
    int wasted = pages * PAGE_SIZE - care_units;
    printf("[PAGING] Patient %d | Pages=%d | InternalFrag=%d\n",
           patient_id, pages, wasted);
}

void pq_enqueue(PatientRecord *rec) {
    PriorityQueue *node = malloc(sizeof(PriorityQueue));
    node->rec = *rec;
    node->next = NULL;
    
    pthread_mutex_lock(&priority_queue_mutex);
    if (!priority_queue_head || rec->priority < priority_queue_head->rec.priority) {
        node->next = priority_queue_head;
        priority_queue_head = node;
    } else {
        PriorityQueue *cur = priority_queue_head;
        while (cur->next && cur->next->rec.priority <= rec->priority)
            cur = cur->next;
        node->next = cur->next;
        cur->next = node;
    }
    pthread_cond_signal(&priority_queue_cond);
    pthread_mutex_unlock(&priority_queue_mutex);
}

int pq_dequeue(PatientRecord *out) {
    if (!priority_queue_head) return 0;
    PriorityQueue *node = priority_queue_head;
    priority_queue_head = priority_queue_head->next;
    *out = node->rec;
    free(node);
    return 1;
}

void sigchld_handler(int sig) {
    (void)sig;
    while (waitpid(-1, NULL, WNOHANG) > 0);
}

void sigterm_handler(int sig) {
    (void)sig;
    shutdown_flag = 1;
    if (ward_shared_memory) ward_shared_memory->running = 0;
    pthread_cond_broadcast(&priority_queue_cond);
    pthread_cond_broadcast(&bed_freed);
}

void write_schedule_log(void) {
    FILE *fp = fopen("logs/schedule_log.txt", "w");
    if (!fp) return;
    
    fprintf(fp, "PID | Priority | Arrival | Start | Finish | Wait | Turnaround\n");
    double total_wait = 0, total_ta = 0;
    
    for (int i = 0; i < schedule_count; i++) {
        double wait = schedule_log[i].start - schedule_log[i].arrival;
        double ta = schedule_log[i].finish - schedule_log[i].arrival;
        total_wait += wait;
        total_ta += ta;
        fprintf(fp, "%d | %d | %.1f | %.1f | %.1f | %.1f | %.1f\n",
                schedule_log[i].patient_id, schedule_log[i].priority,
                schedule_log[i].arrival, schedule_log[i].start, schedule_log[i].finish,
                wait, ta);
    }
    
    if (schedule_count > 0) {
        fprintf(fp, "\nAvg Wait=%.2fs | Avg Turnaround=%.2fs\n",
                total_wait / schedule_count, total_ta / schedule_count);
        printf("[SCHED] Avg Wait=%.2fs | Avg TA=%.2fs\n",
               total_wait / schedule_count, total_ta / schedule_count);
    }
    fclose(fp);
}

const char *bed_type_for(int prio, int *care) {
    if (prio <= 2) { *care = ICU_UNITS; return "ICU"; }
    if (prio == 3) { *care = ISOLATION_UNITS; return "ISOLATION"; }
    *care = GENERAL_UNITS;
    return "GENERAL";
}

void *receptionist_thread(void *arg) {
    (void)arg;
    printf("[RECEPT] Started\n");
    PatientRecord rec;
    
    while (!shutdown_flag) {
        if (read(STDIN_FILENO, &rec, sizeof(PatientRecord)) <= 0) {
            sleep(1);
            continue;
        }
        rec.patient_id = next_patient_id++;
        rec.arrival_time = time(NULL);
        
        printf("[RECEPT] Patient %d (%s) Priority=%d\n",
               rec.patient_id, rec.name, rec.priority);
        
        sem_wait(sem_queue);
        pq_enqueue(&rec);
    }
    return NULL;
}

void *scheduler_thread(void *arg) {
    (void)arg;
    printf("[SCHED] Started\n");
    static double sim_time = 0.0;
    
    while (!shutdown_flag) {
        pthread_mutex_lock(&priority_queue_mutex);
        while (!priority_queue_head && !shutdown_flag)
            pthread_cond_wait(&priority_queue_cond, &priority_queue_mutex);
        
        if (shutdown_flag) {
            pthread_mutex_unlock(&priority_queue_mutex);
            break;
        }
        
        PatientRecord rec;
        if (!pq_dequeue(&rec)) {
            pthread_mutex_unlock(&priority_queue_mutex);
            continue;
        }
        pthread_mutex_unlock(&priority_queue_mutex);
        sem_post(sem_queue);

        int care;
        const char *btype = bed_type_for(rec.priority, &care);
        rec.care_units = care;

        sem_t *cap = NULL;
        if (strcmp(btype, "ICU") == 0) cap = sem_icu;
        if (strcmp(btype, "ISOLATION") == 0) cap = sem_iso;
        
        if (cap) {
            printf("[SCHED] Patient %d waiting for %s capacity\n", rec.patient_id, btype);
            sem_wait(cap);
        }

        pthread_mutex_lock(&bed_mutex);
        int bed_idx = -1;
        while ((bed_idx = allocate_bed(care, btype)) == -1) {
            printf("[SCHED] No %s bed available, waiting\n", btype);
            pthread_cond_wait(&bed_freed, &bed_mutex);
        }
        occupy_bed(bed_idx, rec.patient_id);
        pthread_mutex_unlock(&bed_mutex);

        double start = sim_time++;
        report_paging(rec.patient_id, care);
        report_fragmentation();

        pid_t pid = fork();
        if (pid == 0) {
            char id[16], pri[16], bed[16];
            snprintf(id, 16, "%d", rec.patient_id);
            snprintf(pri, 16, "%d", rec.priority);
            snprintf(bed, 16, "%d", bed_idx);
            char *argv[] = {"./patient_simulator", id, pri, bed, (char*)btype, NULL};
            execv("./patient_simulator", argv);
            exit(1);
        } else if (pid > 0) {
            schedule_log[schedule_count].patient_id = rec.patient_id;
            schedule_log[schedule_count].priority = rec.priority;
            schedule_log[schedule_count].arrival = (double)rec.arrival_time;
            schedule_log[schedule_count].start = start;
            schedule_log[schedule_count].finish = start + 5.0;
            schedule_count++;
        }
    }
    return NULL;
}

void *nurse_thread(void *arg) {
    char *type = (char *)arg;
    printf("[NURSE] Started for %s\n", type);
    
    int fd = open(DISCHARGE_FIFO, O_RDONLY);
    if (fd < 0) return NULL;
    
    while (!shutdown_flag) {
        int pid, bid;
        if (read(fd, &pid, sizeof(int)) <= 0) {
            sleep(1);
            continue;
        }
        read(fd, &bid, sizeof(int));
        
        if (bid < 0 || bid >= TOTAL_BEDS) continue;
        if (strcmp(ward_shared_memory->beds[bid].bed_type, type) != 0) continue;

        pthread_mutex_lock(&bed_mutex);
        free_bed(bid);
        ward_shared_memory->total_served++;
        report_fragmentation();
        pthread_cond_broadcast(&bed_freed);
        pthread_mutex_unlock(&bed_mutex);

        if (strcmp(type, "ICU") == 0) sem_post(sem_icu);
        else if (strcmp(type, "ISOLATION") == 0) sem_post(sem_iso);
        
        printf("[NURSE] %s bed %d freed | Total served: %d\n",
               type, bid, ward_shared_memory->total_served);
    }
    close(fd);
    return NULL;
}

void setup_ipc(void) {
    shm_id = shmget(SHM_KEY, sizeof(SharedWard), IPC_CREAT | 0666);
    if (shm_id < 0) { perror("shmget"); exit(1); }
    
    ward_shared_memory = shmat(shm_id, NULL, 0);
    if (ward_shared_memory == (void*)-1) { perror("shmat"); exit(1); }
    init_ward();

    unlink(DISCHARGE_FIFO);
    if (mkfifo(DISCHARGE_FIFO, 0666) < 0) { perror("mkfifo"); exit(1); }

    sem_unlink(SEM_ICU);
    sem_unlink(SEM_ISO);
    sem_unlink(SEM_QUEUE);
    
    sem_icu = sem_open(SEM_ICU, O_CREAT, 0666, ICU_BEDS);
    sem_iso = sem_open(SEM_ISO, O_CREAT, 0666, ISOLATION_BEDS);
    sem_queue = sem_open(SEM_QUEUE, O_CREAT, 0666, MAX_QUEUE);
    
    if (sem_icu == SEM_FAILED || sem_iso == SEM_FAILED || sem_queue == SEM_FAILED) {
        perror("sem_open");
        exit(1);
    }
    printf("[IPC] Ready (key=0x%X)\n", SHM_KEY);
}

/* Cleanup IPC resources */
void cleanup_ipc(void) {
    if (ward_shared_memory) {
        shmdt(ward_shared_memory);
        shmctl(shm_id, IPC_RMID, NULL);
    }
    unlink(DISCHARGE_FIFO);
    
    if (sem_icu != SEM_FAILED) {
        sem_close(sem_icu);
        sem_unlink(SEM_ICU);
    }
    if (sem_iso != SEM_FAILED) {
        sem_close(sem_iso);
        sem_unlink(SEM_ISO);
    }
    if (sem_queue != SEM_FAILED) {
        sem_close(sem_queue);
        sem_unlink(SEM_QUEUE);
    }
    printf("[IPC] Cleanup complete\n");
}

void setStrategy(int argc, char *argv[]) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--strategy") == 0 && i + 1 < argc) {
            if (strcmp(argv[i+1], "first") == 0) strategy = STRAT_FIRST;
            else if (strcmp(argv[i+1], "worst") == 0) strategy = STRAT_WORST;
            i++;
        }
    }
}

int main(int argc, char *argv[]) {
    setStrategy(argc, argv);
    const char *names[] = {"Best-Fit", "First-Fit", "Worst-Fit"};
    printf("[ADMIT] Starting | Strategy: %s\n", names[strategy]);

    signal(SIGCHLD, sigchld_handler);
    signal(SIGTERM, sigterm_handler);
    signal(SIGINT, sigterm_handler);

    mkdir("logs", 0755);
    setup_ipc();

    pthread_t t1, t2, t3, t4, t5;
    pthread_create(&t1, NULL, receptionist_thread, NULL);
    pthread_create(&t2, NULL, scheduler_thread, NULL);
    pthread_create(&t3, NULL, nurse_thread, "ICU");
    pthread_create(&t4, NULL, nurse_thread, "ISOLATION");
    pthread_create(&t5, NULL, nurse_thread, "GENERAL");

    pthread_join(t1, NULL);
    pthread_join(t2, NULL);
    pthread_join(t3, NULL);
    pthread_join(t4, NULL);
    pthread_join(t5, NULL);

    while (waitpid(-1, NULL, WNOHANG) > 0);

    write_schedule_log();
    printf("[ADMIT] Done | Total served: %d\n", ward_shared_memory ? ward_shared_memory->total_served : 0);
    
    cleanup_ipc();
    return 0;
}