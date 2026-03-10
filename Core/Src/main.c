#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/printk.h>
#include "grbl.h"
#include "grbl_stepper_controller.h"

#ifndef UINT_MAX
#define UINT_MAX 4294967295U
#endif

#define GRBL_STACKSIZE 4096
#define GRBL_PRIORITY 5

K_SEM_DEFINE(grbl_ready_sem, 0, 1); // Binary semaphore to signal when GRBL thread can start

// Wrapper function for the GRBL thread entry point
void grbl_thread_entry(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);
    
    k_sem_take(&grbl_ready_sem, K_FOREVER);
    printk("GRBL thread starting main loop\n"); 
    
    mainGRBL();
}

K_THREAD_DEFINE(grbl_thread_id, GRBL_STACKSIZE, grbl_thread_entry, NULL, NULL, NULL, GRBL_PRIORITY, 0, 0);

int main(void)
{
    printk("GRBL Zephyr Application Starting\n");
    
    const struct device *stepper_dev = DEVICE_DT_GET(DT_NODELABEL(stepper_controller));
    
    if (!device_is_ready(stepper_dev)) {
        printk("ERROR: Stepper controller not ready!\n");
        while (1) {
            k_msleep(1000);
        }
    }
    
    printk("Stepper controller ready\n");
    
    k_sem_give(&grbl_ready_sem);
    printk("GRBL thread released\n");
    
    uint32_t count = 0;
    while (1) {
        k_msleep(5000);
        printk("System running... (%u)\n", count++);
    }
    
    return 0;
}
