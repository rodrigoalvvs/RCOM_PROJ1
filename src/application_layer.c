// Application layer protocol implementation

#include "application_layer.h"
#include "link_layer.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

void applicationLayer(const char *serialPort, const char *role, int baudRate,
                      int nTries, int timeout, const char *filename)
{
    
    LinkLayer linkLayerStruct;

    strncpy(linkLayerStruct.serialPort, serialPort, sizeof(linkLayerStruct.serialPort) - 1);
    linkLayerStruct.serialPort[sizeof(linkLayerStruct.serialPort) - 1] = '\0';

    linkLayerStruct.role = (strcmp(role, "tx") == 0) ? LlTx : LlRx;
    linkLayerStruct.baudRate = baudRate;
    linkLayerStruct.nRetransmissions = nTries;
    linkLayerStruct.timeout = timeout;

    llopen(linkLayerStruct);
    

    /* BYTE STUFFING TEST
    
    
    unsigned char* data = (unsigned char*) malloc(10);

    for(int i = 0; i < 10; i++){
        data[i] = 0x7d;
    }
    llwrite(data, 10);
    */

    /*
    
    if(strcmp(role, "tx") == 0){
        // transmitter
        // try to open file
        FILE* file;
        char* filePath = malloc(sizeof(filename) + 10);
        sprintf(filePath, "%s%s", "../", filename);

        file = fopen(filePath, "rb");
        free(filePath);

        if(file == NULL){
            printf("%s file does not exist!", filename);
            return ;
        } 


        int nrBytes;
        fseek(file, 0, SEEK_END);
        nrBytes = ftell(file);
        fseek(file, 0, SEEK_SET);

        // File is opened, control packet must be sent to start transmission
        unsigned char ctrlBuf[5];

        ctrlBuf[0] = 0x01;
        ctrlBuf[1] = 0x00;
        ctrlBuf[2] = 0x02;
        ctrlBuf[3] = nrBytes & 0xFF;
        ctrlBuf[4] = (nrBytes >> 8) & 0xFF;

        llwrite(ctrlBuf, 5);

        

        // each packet will transport at max 256 octets of data


    }else{
        // receiver
        // receive file and save it

    }
    */
}

