

#define _GNU_SOURCE
#define ARGC(x) RTEMS_BSD_ARGC(x)
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <rtems.h>
#include <rtems/bsd/bsd.h>
#include <sys/stat.h>
#include <machine/rtems-bsd-commands.h>



#include <machine/rtems-bsd-rc-conf.h>
#include <machine/rtems-bsd-rc-conf-services.h>
#include "master.h"
#include "dbg.h"



rtems_task Init(
  rtems_task_argument ignored
)
{
    char *status[] = {
		"ifconfig",
		"cgem0",
		NULL
	};


    char *active[] = {
		"ifconfig",
		"cgem0",
        "inet",
        "169.254.1.2","netmask","255.255.0.0",
		NULL
	};


    

    printf("Starting RTEMS HTTP Server...\n");

    rtems_bsd_initialize();
    rtems_bsd_run_etc_rc_conf(20,true);

    rtems_bsd_command_ifconfig(ARGC(active), active);
    rtems_bsd_command_ifconfig(ARGC(status), status);


    int mkret = mkdir("/www", 0777);
    printf("mkdir /www result: %d\n", mkret);

    FILE* f = fopen("/www/index.html", "w");
    if (!f) {
        printf("ERROR: Failed to open /www/index.html for writing\n");
    } else {
        int nbytes = fprintf(f, "<h1>Hello from RTEMS</h1>");
        printf("Wrote %d bytes to index.html\n", nbytes);
        fclose(f);
        printf("Closed index.html\n");
    }

    
    MasterProcess* master = master_process_init(8080);
    run_master_process(master);

    rtems_task_exit();
    

   
}