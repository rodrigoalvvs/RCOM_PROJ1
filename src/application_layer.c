// Application layer protocol implementation

#include "application_layer.h"
#include "link_layer.h"

#include <stdio.h>
#include <string.h>

void applicationLayer(const char *serialPort, const char *role, int baudRate,
                      int nTries, int timeout, const char *filename)
{
    // TODO
    LinkLayer linkLayerStruct;

    strncpy(linkLayerStruct.serialPort, serialPort, sizeof(linkLayerStruct.serialPort) - 1);
    linkLayerStruct.serialPort[sizeof(linkLayerStruct.serialPort) - 1] = '\0';

    linkLayerStruct.role = (strcmp(role, "tx") == 0) ? LlTx : LlRx;
    linkLayerStruct.baudRate = baudRate;
    linkLayerStruct.nRetransmissions = nTries;
    linkLayerStruct.timeout = timeout;

    llopen(linkLayerStruct);
}

