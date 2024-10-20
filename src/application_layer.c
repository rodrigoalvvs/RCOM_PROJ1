// Application layer protocol implementation

#include "application_layer.h"
#include "link_layer.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <malloc.h>

#define HEADER_SIZE 4
#define CTRL_HEADER_SIZE 7
#define CTRL_FILESIZE 0x0
#define CTRL_FILENAME 0x01
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
        unsigned char filenameSize = strlen(filename);
        unsigned char ctrlBuf[CTRL_HEADER_SIZE + filenameSize];

        ctrlBuf[0] = CTRL_START;

        ctrlBuf[1] = CTRL_FILESIZE;
        ctrlBuf[2] = 0x02;
        ctrlBuf[3] = nrBytes & 0xFF;
        ctrlBuf[4] = (nrBytes >> 8) & 0xFF;

        ctrlBuf[5] = CTRL_FILENAME;
        ctrlBuf[6] = filenameSize;
        memcpy(&ctrlBuf[7], filename, filenameSize);

        // write control word
        llwrite(ctrlBuf, CTRL_HEADER_SIZE + filenameSize);
        

        unsigned int maxPayload = MAX_PAYLOAD_SIZE - HEADER_SIZE;
        unsigned char nrFrames = (nrBytes / (maxPayload));
        unsigned char lastFrameSize = nrBytes % (maxPayload);
        
        unsigned char* frameBuff = (unsigned char*) malloc(MAX_PAYLOAD_SIZE);

        printf("FILE SIZE: %d\n", nrBytes);
        printf("NR FRAMEs: %d\n", nrFrames);
        printf("LAST FRAME: %d\n" , lastFrameSize);

        for(int i = 0; i < nrFrames ; i++){
            
            unsigned int bytesRead = fread(frameBuff + HEADER_SIZE, 1, maxPayload, file);
            
            if(bytesRead != maxPayload){
                
                printf("Error reading frame %d : %d\n", i, bytesRead);
                return;
            }

            frameBuff[0] = 2;
            frameBuff[1] = i;
            frameBuff[2] = (maxPayload >> 8) & 0xFF;
            frameBuff[3] = (maxPayload & 0xFF);

            int ret = llwrite(frameBuff, MAX_PAYLOAD_SIZE);
            if(ret == -1){
                perror("Couldn't send frame\n");
                return;
            }
        }
        if(lastFrameSize > 0){
            printf("SEND LAST FRAME\n");

            unsigned bytesRead = fread(&frameBuff[HEADER_SIZE], 1, lastFrameSize, file);
            if(bytesRead != lastFrameSize){
                printf("Error reading frame %d\n", nrFrames);
            }
            frameBuff[0] = 2;
            frameBuff[1] = nrFrames;
            frameBuff[2] = (lastFrameSize >> 8) & 0xFF;
            frameBuff[3] = (lastFrameSize & 0xFF);

            int ret = llwrite(frameBuff, lastFrameSize + HEADER_SIZE);
            if(ret == -1){
                perror("Couldn't send last frame\n");
                return;
            }
            free(frameBuff);
        }
    
        fclose(file);

        // send end control
        ctrlBuf[0] = CTRL_END;
        llwrite(ctrlBuf, 5);

    }else{
        // receiver
        // receive file and save it
        
        
        int fileSize;
        int idx = 0;
        unsigned char* file;
        char* filename;
        unsigned char packet[MAX_PAYLOAD_SIZE + 1];

        while(TRUE){
            int bytes = llread(&packet[0]);

            if(bytes == -1){
                llopen(linkLayerStruct);
                continue;
            }
            if(packet[0] == CTRL_START){
                // start packet
                fileSize = packet[3] | (packet[4] << 8);

                filename = (char*) malloc(packet[CTRL_HEADER_SIZE - 1]);
                memcpy(filename, &packet[CTRL_HEADER_SIZE], packet[CTRL_HEADER_SIZE - 1]);

                printf("FILENAME IS: %s\n", filename);
                printf("RECEIVED STARTER PACKET!\n");
                printf("FILE SIZE IS : %d\n", fileSize);    
                file = (unsigned char*) malloc(fileSize);
                continue;
            }
            else if(packet[0] == CTRL_DATA){
                // data packet
                memcpy(&file[idx], &packet[HEADER_SIZE], bytes - HEADER_SIZE);
                idx += (bytes - HEADER_SIZE);
                printf("RECEIVED %d bytes\n", bytes);
                continue;;
            }
            else if(packet[0] == CTRL_END){
                // end packet
                printf("REACHED END!\n");
                break;
            }
            

        }

        printf("IDX: %d\n", idx);

        FILE* fp = fopen(filename, "w");
        if (fp == NULL) {
            perror("Error opening file");
            return;
        }

        size_t written = fwrite(file, 1, idx, fp);

        if(written != idx){
            perror("Error writing to file");
        }
        fclose(fp);
        free(file);
        free(filename);
    }

    
}