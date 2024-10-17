// Link layer protocol implementation

#include "link_layer.h"
#include "serial_port.h"
#include <signal.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

// MISC
#define _POSIX_SOURCE 1 // POSIX compliant source
#define FALSE 0
#define TRUE 1

int alarmEnabled = FALSE;
int alarmCount = 0;


typedef enum{
    START,
    FLAG_RCV,
    A_RCV,
    C_RCV,
    BCC_OK,
    STOP
} LinkLayerState;

typedef struct{
    char flag;
    char address;
    char control;
    char bcc;
} Message;


void alarmHandler(int signal){
    alarmEnabled = FALSE;
    alarmCount++;
}


////////////////////////////////////////////////
// LLOPEN
////////////////////////////////////////////////

int llopen(LinkLayer connectionParameters)
{
    if (openSerialPort(connectionParameters.serialPort,
                       connectionParameters.baudRate) < 0)
    {
        return -1;
    }

    // assign alarHandler
    (void) signal(SIGALRM, alarmHandler);

    int timeout = connectionParameters.timeout;
    int messagesToSend = connectionParameters.nRetransmissions;


    LinkLayerState llState = START;
    Message message;

    // Handle logic for transmitter side
    if(connectionParameters.role == LlTx){
        unsigned char set_frame[5];
        set_frame[0] = 0x7e;
        set_frame[1] = 0x03;
        set_frame[2] = 0x03;
        set_frame[3] = set_frame[1] ^ set_frame[2];
        set_frame[4] = 0x7e;

        // retransmission logic (send 3 + 1 messages)
        while((alarmCount - 1) < messagesToSend && llState != STOP ){

            if(alarmEnabled == FALSE){
                alarm(timeout);
                // logic to send SET FRAME
                writeBytesSerialPort(set_frame, 5);
                alarmEnabled = TRUE;
            }

            // wait (0.1s according to serial_port.c) for response
            unsigned char byteRCV;
            int ret = readByteSerialPort(&byteRCV);
            if(ret != 1) continue;

            switch(llState){
                // START state that should receive a valid flag (0x7E)
                case START:
                    if(byteRCV == 0x7E){
                        llState = FLAG_RCV;
                        message.flag = 0x7E;
                    }
                    break;
                // FLAG_RCV state that should receive a valid Address (0x03)
                case FLAG_RCV:
                    if(byteRCV == 0x7E){break;}
                    else if(byteRCV == 0x03){
                        llState = A_RCV;
                        message.address = 0x03;
                    }
                    else{
                        llState = START;
                    }
                    break;
                // A_RCV state that should receive a valid Control (0x07)
                case A_RCV:
                    if(byteRCV == 0x7E){llState = FLAG_RCV;break;}
                    else if(byteRCV == 0x07){
                        llState = C_RCV;
                        message.control = 0x07;
                        break;
                        }
                    else{llState = START;}
                    break;
                // C_RCV state that should receive a valid BCC (A ^ C)
                case C_RCV:
                    if(byteRCV == 0x7E){llState = FLAG_RCV;break;}
                    else if(byteRCV == (message.control ^ message.address)){llState = BCC_OK;break;}
                    else{llState = START;}
                    break;
                // BCC_OK state that should receive a valid flag (0x7E)
                case BCC_OK:
                    if(byteRCV == 0x7E){llState = STOP;break;}
                    else{llState = START;}
                    break;
                default:
                    break;
            }
        }
        if(llState == STOP){
            return 0;
        }
    }
    // Handle logic for receiving side
    else{
        // state machine for each byte
        unsigned char ua_frame[5];

        while(llState != STOP){
            unsigned char byteRCV;
            int ret = readByteSerialPort(&byteRCV);
            if(ret != 1) continue;

            switch(llState){
                // START state that should receive a valid flag (0x7E)
                case START:
                    if(byteRCV == 0x7E){
                        llState = FLAG_RCV;
                        message.flag = 0x7E;
                    }
                    break;
                // FLAG_RCV state that should receive a valid Address (0x03)
                case FLAG_RCV:
                    if(byteRCV == 0x7E){break;}
                    else if(byteRCV == 0x03){
                        llState = A_RCV;
                        message.address = 0x03;
                    }
                    else{
                        llState = START;
                    }
                    break;
                // A_RCV state that should receive a valid Control (0x03)
                case A_RCV:
                    if(byteRCV == 0x7E){llState = FLAG_RCV;break;}
                    else if(byteRCV == 0x03){
                        llState = C_RCV;
                        message.control = 0x03;
                        break;
                        }
                    else{llState = START;}
                    break;
                // C_RCV state that should receive a valid BCC (A ^ C)
                case C_RCV:
                    if(byteRCV == 0x7E){llState = FLAG_RCV;break;}
                    else if(byteRCV == (message.control ^ message.address)){llState = BCC_OK;break;}
                    else{llState = START;}
                    break;
                // BCC_OK state that should receive a valid flag (0x7E)
                case BCC_OK:
                    if(byteRCV == 0x7E){llState = STOP;break;}
                    else{llState = START;}
                    break;
                default:
                    break;
            }
        }
        // transmiting UA
        ua_frame[0] = 0x7e;
        ua_frame[1] = 0x03;
        ua_frame[2] = 0x07;
        ua_frame[3] = ua_frame[1] ^ ua_frame[2];
        ua_frame[4] = 0x7e;
        writeBytesSerialPort(ua_frame, 5);
    }

    return 1;
}

////////////////////////////////////////////////
// LLWRITE
////////////////////////////////////////////////
int llwrite(const unsigned char *buf, int bufSize)
{
    if(buf == NULL){
        printf("Payload Pointer is NULL!\n");
        return 1;
    }

    unsigned char flag = 0x7E;
    unsigned char address = 0x03;
    unsigned char control = frameCounter;
    unsigned char bcc1 = address ^ control;
    unsigned char bcc2 = 0;

    // allocate memory for worst case scenario
    unsigned char* stuffedBuf = (unsigned char*) malloc(sizeof(unsigned char) * bufSize * 2);
    if(!stuffedBuf){
        printf("Couldn't allocate memory for stuffedBuffer!\n");
        return 1;
    } 


    // Get bcc2 from singular bytes from payload and generate stuffedPayload
    int bytesInserted = 0;
    for(int i = 0; i < bufSize; i++){
        bcc2 ^= buf[i];
        if(buf[i] == 0x7E || buf[i] == 0x7d){
            stuffedBuf[bytesInserted++] = 0x7d;
            stuffedBuf[bytesInserted++] = buf[i] ^ 0x20;
        }else{
            stuffedBuf[bytesInserted++] = buf[i];
        }
    }

    // deallocate not used bytes
    unsigned char* temp = (unsigned char*) realloc(stuffedBuf, sizeof(unsigned char) * bytesInserted);
    if(!temp){
        free(temp);
        free(stuffedBuf);
        printf("Couldn't reallocate memory!\n");
        return 1;
    }


    // We got specific size of payload with byte stuffing!!!!!!
    stuffedBuf = temp;

    /* DEBUG STUFFED BYTES
    
    for(int i = 0; i < bytesInserted; i++){
        printf("%x\\", stuffedBuf[i]);
    }
    
    */

    // Information frame is ready for shipment
    int messageSize = 5 + bytesInserted;
    unsigned char* message = (unsigned char*) malloc(messageSize);

    message[0] = flag;
    message[1] = address;
    message[2] = control;
    message[3] = bcc1;
    memcpy(&message[4], stuffedBuf, bytesInserted);
    message[messageSize - 2] = bcc2;
    message[messageSize - 1] = flag;

    // Send message
    int ret = writeBytesSerialPort(message, messageSize);
    if(ret == -1){
        printf("Couldn't write frame!\n");
        return 1;
    }

    // retransmission logic



    return 0;
}

////////////////////////////////////////////////
// LLREAD
////////////////////////////////////////////////
int llread(unsigned char *packet)
{
    // byte destuffing
    

    return 0;
}

////////////////////////////////////////////////
// LLCLOSE
////////////////////////////////////////////////
int llclose(int showStatistics)
{
    // TODO

    int clstat = closeSerialPort();
    return clstat;
}
