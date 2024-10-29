// Application layer protocol implementation

#include "application_layer.h"
#include "link_layer.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <malloc.h>
#include <inttypes.h>
#include <unistd.h>

#define HEADER_SIZE 8
#define CTRL_HEADER_SIZE 7
#define CTRL_FILESIZE 0x0
#define CTRL_FILENAME 0x01
#define CTRL_START 0x01
#define CTRL_DATA 0x02
#define CTRL_END 0x03


void drawHeader(const char* header, LinkLayerRole role){
    
    #ifdef _WIN32
        system("cls");
    #endif
    #ifdef __linux__
        system("clear");
    #endif
    
    if(role == LlRx) printf("Receiving file %s\n", header);
    else printf("Transmiting file %s\n", header);
}

void drawProgress(float progress){
    float width = 20.0;

    printf("Progress [");
    for(int i = 1; i <= width; i++){
        if((i / width) <= progress) printf("#");
        else printf(" ");
    }   
    printf("] %.2f%%\n", progress * 100);
}

int saveToFile(char* filename, unsigned char* file, int size){
    FILE* fp = fopen(filename, "w");
    if (fp == NULL) {
        perror("Error opening file");
        return - 1;
    }
    size_t written = fwrite(file, 1, size, fp);

    if(written != size){
        perror("Error writing to file");
    }
    fclose(fp);
    return 0;
}

int sendInformationPacket(int sequence, int payload, unsigned char* frameBuf){
    frameBuf[0] = 2;
    frameBuf[1] = sequence % 256;
    frameBuf[2] = (payload >> 8) & 0xFF;
    frameBuf[3] = (payload & 0xFF);

    int ret = llwrite(frameBuf, payload);
    if(ret == -1){
        printf("Disconnected! Please reconnect the cable.\n");
        return -1;
    }
    return 0;
}


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

        // Get file size
        uint32_t nrBytes;
        fseek(file, 0, SEEK_END);
        nrBytes = ftell(file);
        fseek(file, 0, SEEK_SET);
        // File is opened, control packet must be sent to start transmission
        unsigned char filenameSize = strlen(filename);
        unsigned char ctrlBuf[CTRL_HEADER_SIZE + filenameSize];
        ctrlBuf[0] = CTRL_START;
        ctrlBuf[1] = CTRL_FILESIZE;
        ctrlBuf[2] = 4;
        ctrlBuf[3] = (nrBytes >> 24) & 0xFF;
        ctrlBuf[4] = (nrBytes >> 16) & 0xFF;
        ctrlBuf[5]  = (nrBytes >> 8) & 0xFF;
        ctrlBuf[6] = nrBytes & 0xFF;
        ctrlBuf[7] = CTRL_FILENAME;
        ctrlBuf[CTRL_HEADER_SIZE] = filenameSize;
        memcpy(&ctrlBuf[CTRL_HEADER_SIZE + 1], filename, filenameSize);


        // write control word
        llwrite(ctrlBuf, CTRL_HEADER_SIZE + filenameSize + 1);

        int maxPayload = MAX_PAYLOAD_SIZE - HEADER_SIZE;
        uint16_t nrFrames = (nrBytes / (maxPayload));
        uint8_t lastFrameSize = nrBytes % (maxPayload);
        
        unsigned char* frameBuff = (unsigned char*) malloc(MAX_PAYLOAD_SIZE);

        for(int i = 0; i < nrFrames ; i++){
            drawHeader(filename, LlTx);
            drawProgress(i / (nrFrames + 1.0));
            
            unsigned int bytesRead = fread(frameBuff + HEADER_SIZE, 1, maxPayload, file);
            if(bytesRead != maxPayload){
                printf("Error reading frame %d : %d\n", i, bytesRead);
                return;
            }

            int ret = sendInformationPacket(i, bytesRead + HEADER_SIZE, frameBuff);
            if (ret == -1) sleep(1);
        }
        if(lastFrameSize > 0){
            unsigned bytesRead = fread(&frameBuff[HEADER_SIZE], 1, lastFrameSize, file);
            if(bytesRead != lastFrameSize){
                printf("Error reading frame %d\n", nrFrames);
            }

            int ret = sendInformationPacket(nrFrames + 1, bytesRead + HEADER_SIZE, frameBuff);
            if(ret == -1) return;
        }
        
        free(frameBuff);
        drawHeader(filename, LlTx);
        drawProgress(1.0);
    
        fclose(file);

        // send end control
        ctrlBuf[0] = CTRL_END;
        llwrite(ctrlBuf, 5);

        // Start llclose
        llclose(1);

        printf("Closed\n");

    }else{
        // receiver
        // receive file and save it
        
        uint32_t fileSize;
        int idx = 0;
        unsigned char* file;
        char* filename = NULL;
        int done = FALSE;
        unsigned char packet[MAX_PAYLOAD_SIZE + 1];

        while(!done){
            int bytes = llread(&packet[0]);
            if (filename != NULL) drawHeader(filename, LlRx); 

            if(bytes == -1){
                llopen(linkLayerStruct);
                continue;
            }

            switch (packet[0])
            {
            case CTRL_START:

                fileSize = (packet[3] << 24) | (packet[4] << 16) | (packet[5] << 8) | packet[6];
                filename = (char*) malloc(packet[CTRL_HEADER_SIZE]);
                memcpy(filename, &packet[CTRL_HEADER_SIZE + 1], packet[CTRL_HEADER_SIZE]);
                file = (unsigned char*) malloc(fileSize);
                break;
            case CTRL_DATA:
                drawProgress((idx + 0.0) / (fileSize + 0.0));
                memcpy(&file[idx], &packet[HEADER_SIZE], bytes - HEADER_SIZE);
                idx += (bytes - HEADER_SIZE);
                break;
            case CTRL_END:
                saveToFile(filename, file, idx);
                done = TRUE;
                break;
            default:
                break;
            }
        }

        free(file);
        free(filename);
        llclose(0);
        printf("CLOSED!\n");
    }
}