// Application layer protocol implementation

#include "application_layer.h"
#include "link_layer.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <malloc.h>

#define HEADER_SIZE 4
#define CTRL_START 0x01
#define CTRL_DATA 0x02
#define CTRL_END 0x03



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

    int ret = llopen(linkLayerStruct);
    if(ret == -1){
        printf("Couldn't establish connection!\n");
        return;
    }

    
    
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

        ctrlBuf[0] = CTRL_START;
        ctrlBuf[1] = 0x00;
        ctrlBuf[2] = 0x02;
        ctrlBuf[3] = nrBytes & 0xFF;
        ctrlBuf[4] = (nrBytes >> 8) & 0xFF;

        
        // write control word
        llwrite(ctrlBuf, 5);
        
        unsigned int maxPayload = MAX_PAYLOAD_SIZE - 4;
        unsigned char nrFrames = ((nrBytes / (maxPayload)) - 0.5);
        unsigned char lastFrameSize = nrBytes % (maxPayload);
        
        unsigned char frameBuff[MAX_PAYLOAD_SIZE];

        printf("NR FRAMEs: %d\n", nrFrames);
        printf("LAST FRAME: %d\n" , lastFrameSize);

        for(int i = 0; i < nrFrames ; i++){
            
            unsigned int bytesRead = fread(&frameBuff[4], 1, maxPayload, file);
            
            if(bytesRead != maxPayload){
                
                printf("Error reading frame %d : %d\n", i, bytesRead);
                return;
            }

            frameBuff[0] = 2;
            frameBuff[1] = i;
            frameBuff[2] = (maxPayload >> 8) & 0xFF;
            frameBuff[3] = (maxPayload & 0xFF);
            llwrite(frameBuff, MAX_PAYLOAD_SIZE);
            printf("SENT %d frame\n", i);
        }
        if(lastFrameSize > 0){
            unsigned bytesRead = fread(&frameBuff[4], 1, lastFrameSize, file);
            if(bytesRead != lastFrameSize){
                printf("Error reading frame %d\n", nrFrames);
            }
            frameBuff[0] = 2;
            frameBuff[1] = nrFrames;
            frameBuff[2] = (lastFrameSize >> 8) & 0xFF;
            frameBuff[3] = (lastFrameSize & 0xFF);
            llwrite(frameBuff, lastFrameSize);
        }
    
        fclose(file);

        // send end control
        ctrlBuf[0] = CTRL_END;
        llwrite(ctrlBuf, 5);

    }else{
        // receiver
        // receive file and save it
        
        unsigned int fileSize;
        FILE* fp = fopen("penguin.gif", "w");
        fclose(fp);

        fp = fopen("penguin.gif", "ab");
        if(fp == NULL){
            perror("Error opening file");
            return;
        }

        while(TRUE){
            unsigned char packet[MAX_PAYLOAD_SIZE];
            int bytes = llread(&packet[0]);

            if(bytes == -1){
                llopen(linkLayerStruct);
                continue;
            }

            if(packet[0] == CTRL_START){
                // start packet
                fileSize = packet[3] | (packet[4] << 8);
                printf("RECEIVED STARTER PACKET!\n");
                continue;
            }
            else if(packet[0] == CTRL_DATA){
                // data packet
                fwrite(&packet[4], 1, bytes - HEADER_SIZE, fp);
                continue;
            }
            else if(packet[0] == CTRL_END){
                // end packet
                printf("REACHED END!\n");
                break;
            }

        }

        fclose(fp);
    }
    
}