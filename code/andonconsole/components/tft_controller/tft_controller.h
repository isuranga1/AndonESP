#ifndef _TFT_CONTROLLER_H_
#define _TFT_CONTROLLER_H_

#include "tftspi.h"
#include "tft.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>

#define SPI_BUS TFT_HSPI_HOST

void _init_TFT();

#endif
