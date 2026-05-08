#include "hospital.h"

static int shm_desc = -1;
static SharedWard *mem_block = NULL;
static int placement_policy = STRAT_BEST;

static PriorityQueue *triage_list = NULL;
static pthread_mutex_t triage_mtx = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t triage_cv = PTHREAD_COND_INITIALIZER;

static pthread_mutex_t room_mtx = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t room_cv = PTHREAD_COND_INITIALIZER;

static sem_t *icu_cap = SEM_FAILED;
static sem_t *iso_cap = SEM_FAILED;
static sem_t *intake_cap = SEM_FAILED;

static ScheduleEntry admission_log[MAX_PATIENTS];
static int log_sz = 0;
static int uid_gen = 1;
static volatile int terminate = 0;

static void coalesce_neighbors(int);
static void dump_fragmentation(void);

static void reap_children(int s) {
    (void)s;
    while (waitpid(-1, NULL, WNOHANG) > 0);
}

static void handle_shutdown(int s) {
    (void)s;
    terminate = 1;
    if (mem_block) mem_block->running = 0;
    pthread_cond_broadcast(&triage_cv);
    pthread_cond_broadcast(&room_cv);
}

static int scan_first(int needed, const char *zone) {
    for (int i = 0; i < TOTAL_BEDS; i++) {
        BedPartition *b = &mem_block->beds[i];
        if (b->is_free && strcmp(b->bed_type, zone) == 0 && b->size >= needed)
            return i;
    }
    return -1;
}

static int scan_best(int needed, const char *zone) {
    int chosen = -1, min_sz = 9999;
    for (int i = 0; i < TOTAL_BEDS; i++) {
        BedPartition *b = &mem_block->beds[i];
        if (b->is_free && strcmp(b->bed_type, zone) == 0 &&
            b->size >= needed && b->size < min_sz) {
            chosen = i;
            min_sz = b->size;
        }
    }
    return chosen;
}

static int scan_worst(int needed, const char *zone) {
    int chosen = -1, max_sz = -1;
    for (int i = 0; i < TOTAL_BEDS; i++) {
        BedPartition *b = &mem_block->beds[i];
        if (b->is_free && strcmp(b->bed_type, zone) == 0 &&
            b->size >= needed && b->size > max_sz) {
            chosen = i;
            max_sz = b->size;
        }
    }
    return chosen;
}

static int resolve_placement(int needed, const char *zone) {
    if (placement_policy == STRAT_FIRST) return scan_first(needed, zone);
    if (placement_policy == STRAT_WORST) return scan_worst(needed, zone);
    return scan_best(needed, zone);
}

static void mark_occupied(int idx, int pat) {
    BedPartition *b = &mem_block->beds[idx];
    b->is_free = 0;
    b->patient_id = pat;
    for (int u = b->start_unit; u < b->start_unit + b->size; u++)
        mem_block->ward[u] = pat;
}

static void coalesce_neighbors(int idx) {
    BedPartition *b = &mem_block->beds[idx];
    b->is_free = 1;
    b->patient_id = -1;
    for (int u = b->start_unit; u < b->start_unit + b->size; u++)
        mem_block->ward[u] = -1;

    if (idx > 0 && mem_block->beds[idx - 1].is_free &&
        strcmp(mem_block->beds[idx - 1].bed_type, b->bed_type) == 0) {
        mem_block->beds[idx - 1].size += b->size;
        b->size = 0;
        b = &mem_block->beds[idx - 1];
    }
    if (idx + 1 < TOTAL_BEDS && mem_block->beds[idx + 1].is_free &&
        strcmp(mem_block->beds[idx + 1].bed_type, b->bed_type) == 0 &&
        mem_block->beds[idx + 1].size > 0) {
        b->size += mem_block->beds[idx + 1].size;
        mem_block->beds[idx + 1].size = 0;
    }
    printf("[COALESCE] Slot %d released\n", idx);
}

static void dump_fragmentation(void) {
    int free_units = 0, longest = 0, streak = 0;
    for (int i = 0; i < TOTAL_UNITS; i++) {
        if (mem_block->ward[i] == -1) { streak++; free_units++; }
        else { if (streak > longest) longest = streak; streak = 0; }
    }
    if (streak > longest) longest = streak;

    double ef = (free_units > 0)
        ? (1.0 - (double)longest / free_units) * 100.0 : 0.0;
    printf("[FRAG] Free=%d Largest=%d ExtFrag=%.1f%%\n", free_units, longest, ef);

    FILE *f = fopen("logs/memory_log.txt", "a");
    if (f) { fprintf(f, "Free=%d Largest=%d ExtFrag=%.1f%%\n", free_units, longest, ef); fclose(f); }
}

static void dump_paging(int pat, int units) {
    int pg = (units + PAGE_SIZE - 1) / PAGE_SIZE;
    int waste = pg * PAGE_SIZE - units;
    printf("[PAGING] Patient %d | Pages=%d | InternalFrag=%d\n", pat, pg, waste);
}

static void push_triage(PatientRecord *rec) {
    PriorityQueue *node = malloc(sizeof(PriorityQueue));
    node->rec = *rec;
    node->next = NULL;

    pthread_mutex_lock(&triage_mtx);
    if (!triage_list || rec->priority < triage_list->rec.priority) {
        node->next = triage_list;
        triage_list = node;
    } else {
        PriorityQueue *cur = triage_list;
        while (cur->next && cur->next->rec.priority <= rec->priority)
            cur = cur->next;
        node->next = cur->next;
        cur->next = node;
    }
    pthread_cond_signal(&triage_cv);
    pthread_mutex_unlock(&triage_mtx);
}

static int pop_triage(PatientRecord *out) {
    if (!triage_list) return 0;
    PriorityQueue *node = triage_list;
    triage_list = triage_list->next;
    *out = node->rec;
    free(node);
    return 1;
}

static void flush_sched_log(void) {
    FILE *f = fopen("logs/schedule_log.txt", "w");
    if (!f) return;

    fprintf(f, "PID | Priority | Arrival | Start | Finish | Wait | Turnaround\n");
    double tw = 0, tt = 0;

    for (int i = 0; i < log_sz; i++) {
        double w = admission_log[i].start - admission_log[i].arrival;
        double ta = admission_log[i].finish - admission_log[i].arrival;
        tw += w; tt += ta;
        fprintf(f, "%d | %d | %.1f | %.1f | %.1f | %.1f | %.1f\n",
                admission_log[i].patient_id, admission_log[i].priority,
                admission_log[i].arrival, admission_log[i].start,
                admission_log[i].finish, w, ta);
    }
    if (log_sz > 0) {
        fprintf(f, "\nAvg Wait=%.2fs | Avg Turnaround=%.2fs\n", tw / log_sz, tt / log_sz);
        printf("[SCHED] Avg Wait=%.2fs | Avg TA=%.2fs\n", tw / log_sz, tt / log_sz);
    }
    fclose(f);
}

static const char *zone_for_priority(int prio, int *units) {
    if (prio <= 2) { *units = ICU_UNITS; return "ICU"; }
    if (prio == 3) { *units = ISOLATION_UNITS; return "ISOLATION"; }
    *units = GENERAL_UNITS;
    return "GENERAL";
}

static void bootstrap_ward(void) {
    memset(mem_block, 0, sizeof(SharedWard));
    mem_block->running = 1;
    for (int i = 0; i < TOTAL_UNITS; i++) mem_block->ward[i] = -1;

    int base = 0, b = 0;
    for (int i = 0; i < ICU_BEDS; i++, b++) {
        mem_block->beds[b] = (BedPartition){b, base, ICU_UNITS, 1, -1, "ICU"};
        base += ICU_UNITS;
    }
    for (int i = 0; i < ISOLATION_BEDS; i++, b++) {
        mem_block->beds[b] = (BedPartition){b, base, ISOLATION_UNITS, 1, -1, "ISOLATION"};
        base += ISOLATION_UNITS;
    }
    for (int i = 0; i < GENERAL_BEDS; i++, b++) {
        mem_block->beds[b] = (BedPartition){b, base, GENERAL_UNITS, 1, -1, "GENERAL"};
        base += GENERAL_UNITS;
    }
    printf("[WARD] Init: %d ICU + %d ISO + %d GEN = %d beds, %d units\n",
           ICU_BEDS, ISOLATION_BEDS, GENERAL_BEDS, TOTAL_BEDS, TOTAL_UNITS);
}

static void *intake_worker(void *arg) {
    (void)arg;
    printf("[RECEPT] Started\n");
    PatientRecord rec;

    while (!terminate) {
        if (read(STDIN_FILENO, &rec, sizeof(PatientRecord)) <= 0) { sleep(1); continue; }
        rec.patient_id = uid_gen++;
        rec.arrival_time = time(NULL);
        printf("[RECEPT] Patient %d (%s) Priority=%d\n", rec.patient_id, rec.name, rec.priority);
        sem_wait(intake_cap);
        push_triage(&rec);
    }
    return NULL;
}

static void *dispatch_worker(void *arg) {
    (void)arg;
    printf("[SCHED] Started\n");
    static double clock_sim = 0.0;

    while (!terminate) {
        pthread_mutex_lock(&triage_mtx);
        while (!triage_list && !terminate)
            pthread_cond_wait(&triage_cv, &triage_mtx);

        if (terminate) { pthread_mutex_unlock(&triage_mtx); break; }

        PatientRecord rec;
        if (!pop_triage(&rec)) { pthread_mutex_unlock(&triage_mtx); continue; }
        pthread_mutex_unlock(&triage_mtx);
        sem_post(intake_cap);

        int units;
        const char *zone = zone_for_priority(rec.priority, &units);
        rec.care_units = units;

        sem_t *cap = NULL;
        if (strcmp(zone, "ICU") == 0) cap = icu_cap;
        if (strcmp(zone, "ISOLATION") == 0) cap = iso_cap;
        if (cap) { printf("[SCHED] Patient %d waiting for %s\n", rec.patient_id, zone); sem_wait(cap); }

        pthread_mutex_lock(&room_mtx);
        int slot = -1;
        while ((slot = resolve_placement(units, zone)) == -1) {
            printf("[SCHED] No %s slot, waiting\n", zone);
            pthread_cond_wait(&room_cv, &room_mtx);
        }
        mark_occupied(slot, rec.patient_id);
        pthread_mutex_unlock(&room_mtx);

        double t_start = clock_sim++;
        dump_paging(rec.patient_id, units);
        dump_fragmentation();

        pid_t cpid = fork();
        if (cpid == 0) {
            char a[16], b[16], c[16];
            snprintf(a, 16, "%d", rec.patient_id);
            snprintf(b, 16, "%d", rec.priority);
            snprintf(c, 16, "%d", slot);
            char *av[] = {"./patient_simulator", a, b, c, (char *)zone, NULL};
            execv("./patient_simulator", av);
            exit(1);
        } else if (cpid > 0) {
            admission_log[log_sz].patient_id = rec.patient_id;
            admission_log[log_sz].priority   = rec.priority;
            admission_log[log_sz].arrival    = (double)rec.arrival_time;
            admission_log[log_sz].start      = t_start;
            admission_log[log_sz].finish     = t_start + 5.0;
            log_sz++;
        }
    }
    return NULL;
}

static void *ward_nurse(void *arg) {
    char *zone = (char *)arg;
    printf("[NURSE] Started for %s\n", zone);

    int fd = open(DISCHARGE_FIFO, O_RDONLY);
    if (fd < 0) return NULL;

    while (!terminate) {
        int pid, bid;
        if (read(fd, &pid, sizeof(int)) <= 0) { sleep(1); continue; }
        read(fd, &bid, sizeof(int));

        if (bid < 0 || bid >= TOTAL_BEDS) continue;
        if (strcmp(mem_block->beds[bid].bed_type, zone) != 0) continue;

        pthread_mutex_lock(&room_mtx);
        coalesce_neighbors(bid);
        mem_block->total_served++;
        dump_fragmentation();
        pthread_cond_broadcast(&room_cv);
        pthread_mutex_unlock(&room_mtx);

        if (strcmp(zone, "ICU") == 0) sem_post(icu_cap);
        else if (strcmp(zone, "ISOLATION") == 0) sem_post(iso_cap);

        printf("[NURSE] %s slot %d freed | Served: %d\n",
               zone, bid, mem_block->total_served);
    }
    close(fd);
    return NULL;
}

static void teardown_ipc(void) {
    if (mem_block) { shmdt(mem_block); shmctl(shm_desc, IPC_RMID, NULL); }
    unlink(DISCHARGE_FIFO);
    if (icu_cap    != SEM_FAILED) { sem_close(icu_cap);    sem_unlink(SEM_ICU); }
    if (iso_cap    != SEM_FAILED) { sem_close(iso_cap);    sem_unlink(SEM_ISO); }
    if (intake_cap != SEM_FAILED) { sem_close(intake_cap); sem_unlink(SEM_QUEUE); }
    printf("[IPC] Cleanup done\n");
}

static void init_ipc(void) {
    shm_desc = shmget(SHM_KEY, sizeof(SharedWard), IPC_CREAT | 0666);
    if (shm_desc < 0) { perror("shmget"); exit(1); }

    mem_block = shmat(shm_desc, NULL, 0);
    if (mem_block == (void *)-1) { perror("shmat"); exit(1); }
    bootstrap_ward();

    unlink(DISCHARGE_FIFO);
    if (mkfifo(DISCHARGE_FIFO, 0666) < 0) { perror("mkfifo"); exit(1); }

    sem_unlink(SEM_ICU); sem_unlink(SEM_ISO); sem_unlink(SEM_QUEUE);
    icu_cap    = sem_open(SEM_ICU,   O_CREAT, 0666, ICU_BEDS);
    iso_cap    = sem_open(SEM_ISO,   O_CREAT, 0666, ISOLATION_BEDS);
    intake_cap = sem_open(SEM_QUEUE, O_CREAT, 0666, MAX_QUEUE);

    if (icu_cap == SEM_FAILED || iso_cap == SEM_FAILED || intake_cap == SEM_FAILED) {
        perror("sem_open"); exit(1);
    }
    printf("[IPC] Ready (key=0x%X)\n", SHM_KEY);
}

static void parse_args(int argc, char *argv[]) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--strategy") == 0 && i + 1 < argc) {
            if (strcmp(argv[i + 1], "first") == 0) placement_policy = STRAT_FIRST;
            else if (strcmp(argv[i + 1], "worst") == 0) placement_policy = STRAT_WORST;
            i++;
        }
    }
}

int main(int argc, char *argv[]) {
    parse_args(argc, argv);
    const char *labels[] = {"Best-Fit", "First-Fit", "Worst-Fit"};
    printf("[ADMIT] Starting | Strategy: %s\n", labels[placement_policy]);

    signal(SIGCHLD, reap_children);
    signal(SIGTERM, handle_shutdown);
    signal(SIGINT,  handle_shutdown);

    mkdir("logs", 0755);
    init_ipc();

    pthread_t w1, w2, w3, w4, w5;
    pthread_create(&w1, NULL, intake_worker,  NULL);
    pthread_create(&w2, NULL, dispatch_worker, NULL);
    pthread_create(&w3, NULL, ward_nurse, "ICU");
    pthread_create(&w4, NULL, ward_nurse, "ISOLATION");
    pthread_create(&w5, NULL, ward_nurse, "GENERAL");

    pthread_join(w1, NULL);
    pthread_join(w2, NULL);
    pthread_join(w3, NULL);
    pthread_join(w4, NULL);
    pthread_join(w5, NULL);

    while (waitpid(-1, NULL, WNOHANG) > 0);

    flush_sched_log();
    printf("[ADMIT] Done | Total served: %d\n",
           mem_block ? mem_block->total_served : 0);

    teardown_ipc();
    return 0;
}