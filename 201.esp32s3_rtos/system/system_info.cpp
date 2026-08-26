#include "system_info.h"
#include "app/app.h"

/* ---- minimum-free trackers -------------------------------------------- */
static uint32_t s_min_heap  = 0xFFFFFFFF;
static uint32_t s_min_psram = 0xFFFFFFFF;

void mem_update(void) {
    uint32_t fh = ESP.getFreeHeap();
    uint32_t fp = ESP.getFreePsram();
    if (fh < s_min_heap)  s_min_heap  = fh;
    if (fp < s_min_psram) s_min_psram = fp;
}

uint32_t mem_min_heap(void)  { return s_min_heap; }
uint32_t mem_min_psram(void) { return s_min_psram; }

/* ---- task state names ------------------------------------------------- */
const char* task_state_name(int state) {
    switch (state) {
        case 0: return "Running";
        case 1: return "Ready";
        case 2: return "Block";
        case 3: return "Susp";
        case 4: return "Del";
        default: return "Inv";
    }
}

/* ---- startup banner --------------------------------------------------- */
void print_banner(void) {
    console_lock();
    Serial.printf("========================================\r\n");
    Serial.printf(" ESP32-S3 FreeRTOS Monitor\r\n");
    Serial.printf("========================================\r\n");
    Serial.printf("\r\n");
    Serial.printf("Chip:\r\n");
    Serial.printf("  Model: %s\r\n",          ESP.getChipModel());
    Serial.printf("  CPU Cores: %d\r\n",      ESP.getChipCores());
    Serial.printf("  CPU Frequency: %d MHz\r\n", ESP.getCpuFreqMHz());
    Serial.printf("\r\n");
    Serial.printf("Memory:\r\n");
    Serial.printf("  Flash: %u MB\r\n",        (unsigned)(ESP.getFlashChipSize() / (1024 * 1024)));
    if (psramFound()) {
        Serial.printf("  PSRAM: %u MB\r\n",   (unsigned)(ESP.getPsramSize() / (1024 * 1024)));
    } else {
        Serial.printf("  PSRAM: not available\r\n");
    }
    Serial.printf("  Free Heap: %u KB\r\n",   (unsigned)(ESP.getFreeHeap()  / 1024));
    Serial.printf("  Free PSRAM: %u KB\r\n",  (unsigned)(ESP.getFreePsram() / 1024));
    Serial.printf("\r\n");
    Serial.printf("FreeRTOS:\r\n");
    Serial.printf("  Scheduler: running\r\n");
    Serial.printf("\r\n");
    Serial.printf("Tasks:\r\n");
    Serial.printf("  LED     : started\r\n");
    Serial.printf("  Button  : started\r\n");
    Serial.printf("  Monitor : started\r\n");
    Serial.printf("  UART    : started\r\n");
    Serial.printf("\r\n");
    Serial.printf("System ready.\r\n");
    Serial.printf("========================================\r\n");
    console_unlock();
}

/* ---- startup memory snapshot ----------------------------------------- */
void print_mem_info(void) {
    mem_update();
    console_lock();
    Serial.printf("[MEM] Free Heap: %u KB\r\n",        (unsigned)(ESP.getFreeHeap()  / 1024));
    Serial.printf("[MEM] Minimum Free Heap: %u KB\r\n",(unsigned)(mem_min_heap()     / 1024));
    if (psramFound()) {
        Serial.printf("[MEM] PSRAM Total: %u KB\r\n",  (unsigned)(ESP.getPsramSize() / 1024));
        Serial.printf("[MEM] PSRAM Free: %u KB\r\n",   (unsigned)(ESP.getFreePsram() / 1024));
        Serial.printf("[MEM] PSRAM Minimum Free: %u KB\r\n", (unsigned)(mem_min_psram() / 1024));
    } else {
        Serial.printf("[MEM] PSRAM: not found\r\n");
    }
    console_unlock();
}

/* ---- FreeRTOS task table --------------------------------------------- */
void print_task_list(void) {
    UBaseType_t n = uxTaskGetNumberOfTasks();
    if (n == 0) {
        return;
    }
    TaskStatus_t* st = (TaskStatus_t*) malloc(n * sizeof(TaskStatus_t));
    if (st == NULL) {
        console_printf("[ERR] print_task_list: malloc failed\r\n");
        return;
    }
    UBaseType_t got = uxTaskGetSystemState(st, n, NULL);

    console_lock();
    Serial.printf("[TASK] count=%u\r\n", (unsigned)got);
    Serial.printf("Name        State   Prio  StackMin  Core\r\n");
    for (UBaseType_t i = 0; i < got; i++) {
        Serial.printf("%-11s %-7s %-5u %-8u  %d\r\n",
            st[i].pcTaskName,
            task_state_name((int)st[i].eCurrentState),
            (unsigned)st[i].uxCurrentPriority,
            (unsigned)st[i].usStackHighWaterMark,
            (int)st[i].xCoreID);
    }
    console_unlock();

    free(st);
}
