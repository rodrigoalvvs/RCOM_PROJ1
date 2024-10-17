// Link layer protocol implementation

#include "link_layer.h"
#include "serial_port.h"
#include <signal.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

// MISC
#define _POSIX_SOURCE 1 // POSIX compliant source
#define FALSE 0
#define TRUE 1

int alarmEnabled = FALSE;
int alarmCount = 0;
unsigned char frameCounter = 0x0;

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

typedef struct{
    unsigned char flag;
    unsigned char address;
    unsigned char control;
    unsigned char* data;
    unsigned char bcc1;
    unsigned char bcc2;

} IFrame;


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
            printf("RECEIVED UA FRAME\n");
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
    // TODO
    IFrame iframe;
    iframe.flag = 0x7E;
    iframe.address = 0x03;
    iframe.control = frameCounter;
    iframe.bcc1 = iframe.address ^ iframe.control;
    iframe.data = malloc(bufSize);

    
    // when RR is received, module-2 the framCounter
    frameCounter ^= 0x1;
    return 0;
}

////////////////////////////////////////////////
// LLREAD
////////////////////////////////////////////////
int llread(unsigned char *packet)
{
    // TODO

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
