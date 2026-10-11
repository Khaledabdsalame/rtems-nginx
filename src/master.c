#include "master.h"
#include "worker.h"
#include "server.h"
#include <rtems.h>
#include <rtems/bsd/bsd.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>

#define WORKER_STACK_SIZE 8192
#define WORKER_PRIORITY   100

static MasterProcess* g_master;
static volatile int shutdown_requested = 0;
static rtems_id worker_tasks[MAX_WORKERS_COUNT];
static int worker_count_active = 0;
static int worker_indices[MAX_WORKERS_COUNT];  // Track worker indices globally

/* Worker argument structure */
typedef struct {
    TCPServer* server;
    int worker_index;
} WorkerArgs;

/* Signal handler for graceful shutdown */
/* RTEMS may not support POSIX signals, so this is optional */
#ifdef RTEMS_POSIX_API
#include <signal.h>
static void handle_sigterm(int sig) {
    (void) sig;
    shutdown_requested = 1;
}
#endif

/* Detect number of CPU cores */
static uint8_t detect_cpu_cores(void) {
    uint8_t num_cpus = 1;

#ifdef _SC_NPROCESSORS_ONLN
    long count = sysconf(_SC_NPROCESSORS_ONLN);
    if (count > 0 && count <= MAX_WORKERS_COUNT) {
        num_cpus = (uint8_t) count;
    }
#else
#ifdef _SC_NPROCESSORS_CONF
    long count = sysconf(_SC_NPROCESSORS_CONF);
    if (count > 0 && count <= MAX_WORKERS_COUNT) {
        num_cpus = (uint8_t) count;
    }
#endif
#endif

    if (num_cpus > MAX_WORKERS_COUNT) {
        num_cpus = MAX_WORKERS_COUNT;
    }
    if (num_cpus < 1) {
        num_cpus = 1;
    }
    return num_cpus;
}
static void nginx_log_notice(const char* fmt, ...) {
    time_t now = time(NULL);
    struct tm* tm_info = localtime(&now);
    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%Y/%m/%d %H:%M:%S", tm_info);
    
    printf("%s [notice] ", timestamp);
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    printf("\n");
    fflush(stdout);
}

static void nginx_log_error(const char* fmt, ...) {
    time_t now = time(NULL);
    struct tm* tm_info = localtime(&now);
    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%Y/%m/%d %H:%M:%S", tm_info);
    
    printf("%s [error] ", timestamp);
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    printf("\n");
    fflush(stdout);
}

// كل Worker عبارة عن Task على RTEMS
rtems_task worker_entry(rtems_task_argument arg) {
    WorkerArgs* args = (WorkerArgs*) arg;
    TCPServer* server = args->server;
    int worker_index = args->worker_index;
    
    // CPU affinity binding (RTEMS processor affinity)
    // RTEMS may not support full CPU affinity, but we can try
    #ifdef RTEMS_SMP
    rtems_id self = rtems_task_self();
    uint32_t cpu_set = 1 << (worker_index % rtems_get_processor_count());
    if (rtems_task_set_affinity(self, RTEMS_CPUSET_ALL, &cpu_set, sizeof(cpu_set)) == RTEMS_SUCCESSFUL) {
        nginx_log_notice("worker #%d attached to CPU core %d", worker_index + 1, worker_index);
    }
    #endif
    
    run_worker_process(server);
    free(args);
    rtems_task_exit();
}

// تهيئة Master
MasterProcess* master_process_init(uint16_t port) {
    MasterProcess* master = (MasterProcess*) malloc(sizeof(MasterProcess));
    if (!master) {
        nginx_log_error("cannot allocate memory for master process");
        return NULL;
    }

    nginx_log_notice("master process starting");
    rtems_bsd_initialize(); // تهيئة الشبكة
    master->server = tcp_server_init();
    tcp_server_start(master->server, port);

    // Auto-detect CPU cores
    uint8_t num_cpus = detect_cpu_cores();
    master->workers_count = num_cpus;
    
    // Setup signal handler for graceful shutdown (if available)
    #ifdef RTEMS_POSIX_API
    signal(SIGTERM, handle_sigterm);
    #endif
    
    nginx_log_notice("detected %d CPU core%s, spawning %d worker%s",
                     num_cpus, num_cpus > 1 ? "s" : "",
                     num_cpus, num_cpus > 1 ? "s" : "");
    nginx_log_notice("listening on port %u", port);
    return master;
}

// تشغيل Master وإنشاء Workers
void run_master_process(MasterProcess* master) {
    g_master = master;

    for (int i = 0; i < master->workers_count; i++) {
        rtems_id tid;
        rtems_status_code sc;

        sc = rtems_task_create(
            rtems_build_name('W','K',i+1,0),
            WORKER_PRIORITY,
            WORKER_STACK_SIZE,
            RTEMS_PREEMPT | RTEMS_TIMESLICE | RTEMS_NO_ASR,
            RTEMS_LOCAL,
            &tid
        );
        if (sc != RTEMS_SUCCESSFUL) {
            nginx_log_error("failed to create worker #%d task", i+1);
            continue;
        }

        // Create worker arguments
        WorkerArgs* args = (WorkerArgs*) malloc(sizeof(WorkerArgs));
        if (!args) {
            nginx_log_error("failed to allocate memory for worker #%d args", i+1);
            rtems_task_delete(tid);
            continue;
        }
        args->server = master->server;
        args->worker_index = i;
        
        sc = rtems_task_start(tid, worker_entry, (rtems_task_argument) args);
        if (sc != RTEMS_SUCCESSFUL) {
            nginx_log_error("failed to start worker #%d task", i+1);
            free(args);
            rtems_task_delete(tid);
            continue;
        }
        
        worker_tasks[worker_count_active] = tid;
        worker_count_active++;
    }

    nginx_log_notice("%d worker process%s started",
                     master->workers_count,
                     master->workers_count > 1 ? "es" : "");

    // Master loop with graceful shutdown support
    while (!shutdown_requested) {
        rtems_task_wake_after(RTEMS_MILLISECONDS_TO_TICKS(1000));
    }
    
    // Graceful shutdown
    nginx_log_notice("shutting down gracefully");
    
    // Terminate all workers
    for (int i = 0; i < worker_count_active; i++) {
        rtems_task_delete(worker_tasks[i]);
    }
    
    tcp_server_stop(master->server);
    nginx_log_notice("master process terminated");
}

// تحرير الموارد عند إنهاء Master
void free_master_process(MasterProcess* master) {
    if (!master) return;
    
    // Trigger graceful shutdown
    shutdown_requested = 1;
    
    // Terminate workers
    for (int i = 0; i < worker_count_active; i++) {
        rtems_task_delete(worker_tasks[i]);
    }
    
    tcp_server_stop(master->server);
    free(master->server);
    free(master);
    
    nginx_log_notice("resources freed");
}