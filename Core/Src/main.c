#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <stdio.h>

#include "grbl.h"

int main(void)

{
  mainGRBL();

  while (1)

  {
    k_msleep(1000);
  }

  return 0;
}
