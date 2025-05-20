//
// Created by fogoz on 04/05/2025.
//

#include "TCPTeensyUpdater.h"

DMAMEM char tcp_uploader_buffer[SIZE_TCP_BUFFER];
uint16_t c_index = 0;
DMAMEM char data[HEX_DATA_MAX_SIZE] __attribute__((aligned(8)));
