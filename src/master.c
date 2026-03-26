#include "master.h"
#include "worker.h"
#include "server.h"
#include <rtems.h>
#include <rtems/bsd/bsd.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <time.h>

#define WORKER_STACK_SIZE 8192
#define WORKER_PRIORITY   100

static MasterProcess* g_master;

/* Logging functions similar to nginx */
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
    TCPServer* server = (TCPServer*) arg;
    run_worker_process(server);
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

    master->workers_count = 1; // Just 1 worker for testing
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

        sc = rtems_task_start(tid, worker_entry, (rtems_task_argument) master->server);
        if (sc != RTEMS_SUCCESSFUL) {
            nginx_log_error("failed to start worker #%d task", i+1);
            continue;
        }
    }

    nginx_log_notice("%d worker process%s started",
                     master->workers_count,
                     master->workers_count > 1 ? "es" : "");

    // Master يبقى هنا للحفاظ على استمرار الخادم
    while (1) {
        rtems_task_wake_after(RTEMS_MILLISECONDS_TO_TICKS(1000));
    }
}

// تحرير الموارد عند إنهاء Master
void free_master_process(MasterProcess* master) {
    tcp_server_stop(master->server);
    free(master->server);
    free(master);
}