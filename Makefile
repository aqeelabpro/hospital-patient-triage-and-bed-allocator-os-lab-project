CC      = gcc
CFLAGS  = -Wall -Wextra -pthread -I./src
LDFLAGS = -lpthread

SRC_DIR = src

.PHONY: all clean run test

# Build both binaries
all: admissions patient_simulator
	@echo "Build complete."

admissions: $(SRC_DIR)/admissions.c $(SRC_DIR)/hospital.h
	$(CC) $(CFLAGS) -o admissions $(SRC_DIR)/admissions.c $(LDFLAGS)

patient_simulator: $(SRC_DIR)/patient_simulator.c $(SRC_DIR)/hospital.h
	$(CC) $(CFLAGS) -o patient_simulator $(SRC_DIR)/patient_simulator.c

# Start the hospital
run: all
	chmod +x scripts/*.sh
	./scripts/start_hospital.sh

# Run the stress test
test: all
	chmod +x scripts/*.sh
	@echo "--- Running stress test ---"
	./scripts/stress_test.sh

# Remove compiled binaries and log files
clean:
	rm -f admissions patient_simulator
	rm -f logs/schedule_log.txt logs/memory_log.txt
	rm -f /tmp/discharge_fifo /tmp/hospital_pid
	@echo "Clean complete."
