#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/printk.h>
#include <zephyr/posix/sys/socket.h>
#include <zephyr/posix/unistd.h>
#include "grbl.h"
#include "grbl_stepper_controller.h"
#include "tcp_rx.h"
#include "tcp_tx.h"

#ifndef UINT_MAX
#define UINT_MAX 4294967295U
#endif

#define GRBL_STACKSIZE 4096
#define GRBL_PRIORITY 5
#define TCP_STACKSIZE 4096
#define TCP_PRIORITY 4
#define TCP_RX_STACKSIZE 2048
#define TCP_TX_STACKSIZE 2048

K_SEM_DEFINE(grbl_ready_sem, 0, 1); // Binary semaphore to signal when GRBL thread can start

K_THREAD_STACK_DEFINE(tcp_rx_thread_stack, TCP_RX_STACKSIZE);
K_THREAD_STACK_DEFINE(tcp_tx_thread_stack, TCP_TX_STACKSIZE);
static struct k_thread tcp_rx_thread_data;
static struct k_thread tcp_tx_thread_data;


void tcp_session_run(int client)
{
    k_tid_t rx_tid;
    k_tid_t tx_tid;

    tcp_tx_begin();

    rx_tid = k_thread_create(&tcp_rx_thread_data, tcp_rx_thread_stack,
                             K_THREAD_STACK_SIZEOF(tcp_rx_thread_stack),
                             tcp_rx_session, (void *)(intptr_t)client,
                             NULL, NULL, 4, 0, K_NO_WAIT);

    tx_tid = k_thread_create(&tcp_tx_thread_data, tcp_tx_thread_stack,
                             K_THREAD_STACK_SIZEOF(tcp_tx_thread_stack),
                             tcp_tx_thread_entry, (void *)(intptr_t)client,
                             NULL, NULL, 5, 0, K_NO_WAIT);

    if (rx_tid) {
        k_thread_join(rx_tid, K_FOREVER);
    }

    tcp_tx_stop();
    shutdown(client, SHUT_RDWR);

    if (tx_tid) {
        k_thread_join(tx_tid, K_FOREVER);
    }

    close(client);
}

// Forward declaration of the TCP thread entry function
static void tcp_thread_entry(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    int ret = main_tcp();

}

K_THREAD_STACK_DEFINE(tcp_thread_stack, TCP_STACKSIZE);
static struct k_thread tcp_thread_data;

// Wrapper function for the GRBL thread entry point
void grbl_thread_entry(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);
    
    k_sem_take(&grbl_ready_sem, K_FOREVER);

    mainGRBL();
}

K_THREAD_DEFINE(grbl_thread_id, GRBL_STACKSIZE, grbl_thread_entry, NULL, NULL, NULL, GRBL_PRIORITY, 0, 0);

int main(void)
{

    const struct device *stepper_dev = DEVICE_DT_GET(DT_NODELABEL(stepper_controller));
    
    if (!device_is_ready(stepper_dev)) {
        while (1) {
            k_msleep(1000);
        }
    }

    k_thread_create(&tcp_thread_data, tcp_thread_stack,
                    K_THREAD_STACK_SIZEOF(tcp_thread_stack),
                    tcp_thread_entry, NULL, NULL, NULL,
                    TCP_PRIORITY, 0, K_NO_WAIT);
    
    k_sem_give(&grbl_ready_sem);
    
    while (1) {
        k_msleep(5000);
    }
    
    return 0;
}
