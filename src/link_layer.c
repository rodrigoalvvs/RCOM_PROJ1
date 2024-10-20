// Link layer protocol implementation

#include "link_layer.h"
#include "serial_port.h"
#include "macros.h"
#include <signal.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

// MISC
#define _POSIX_SOURCE 1 // POSIX compliant source
#define FALSE 0
#define TRUE 1

int alarmTimeout = 0;

int alarmCount = 0;
int alarmEnabled = FALSE;
int retransmissions = 0;

unsigned char frameNr = 0;


typedef enum{
    START,
    FLAG_RCV,
    A_RCV,
    C_RCV,
    BCC1_OK,
    READING_DATA,
    ESCAPED_DATA,
    STOP
} LinkLayerState;

typedef struct{
    unsigned char flag;
    unsigned char address;
    unsigned char control;
    unsigned char bcc;
} Message;


void alarmHandler(int signal){
    alarmEnabled = FALSE;
    alarmCount++;
}


int sendSupervisionMessage(unsigned char address, unsigned char control){
    unsigned char s_frame[5];
    s_frame[0] = FLAG;
    s_frame[1] = address;
    s_frame[2] = control;  
    s_frame[3] = BCC1(s_frame[1], s_frame[2]);
    s_frame[4] = FLAG;
    return writeBytesSerialPort(s_frame, 5);
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

    alarmTimeout = connectionParameters.timeout;
    retransmissions = connectionParameters.nRetransmissions;


    LinkLayerState llState = START;
    Message message;

    // Handle logic for transmitter side
    if(connectionParameters.role == LlTx){
        
        // retransmission logic (send 3 + 1 messages)
        while((alarmCount - 1) < retransmissions && llState != STOP){

            if(alarmEnabled == FALSE){
                alarm(alarmTimeout);
                sendSupervisionMessage(A_TX, C_SET);
                alarmEnabled = TRUE;
            }

            // wait (0.1s according to serial_port.c) for response
            unsigned char byteRCV;
            int ret = readByteSerialPort(&byteRCV);
            if(ret != 1) continue;

            switch(llState){
                // START state that should receive a valid flag (FLAG)
                case START:
                    if(byteRCV == FLAG){
                        llState = FLAG_RCV;
                        message.flag = FLAG;
                    }
                    break;
                // FLAG_RCV state that should receive a valid Address (0x03)
                case FLAG_RCV:
                    if(byteRCV == FLAG){break;}
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
                    if(byteRCV == FLAG){llState = FLAG_RCV;break;}
                    else if(byteRCV == 0x07){
                        llState = C_RCV;
                        message.control = 0x07;
                        break;
                        }
                    else{llState = START;}
                    break;
                // C_RCV state that should receive a valid BCC (A ^ C)
                case C_RCV:
                    if(byteRCV == FLAG){llState = FLAG_RCV;break;}
                    else if(byteRCV == (message.control ^ message.address)){llState = BCC1_OK;break;}
                    else{llState = START;}
                    break;
                // BCC_OK state that should receive a valid flag (FLAG)
                case BCC1_OK:
                    if(byteRCV == FLAG){llState = STOP;break;}
                    else{llState = START;}
                    break;
                default:
                    break;
            }
        }
        if(llState == STOP){
            // returns data link identifier ????
            return 0;
        }
        return -1;
    }
    // Handle logic for receiving side
    else{
        // state machine for each byte
        while(llState != STOP){
            unsigned char byteRCV;
            int ret = readByteSerialPort(&byteRCV);
            if(ret != 1) continue;

            switch(llState){
                // START state that should receive a valid flag (FLAG)
                case START:
                    if(byteRCV == FLAG){
                        llState = FLAG_RCV;
                        message.flag = FLAG;
                    }
                    break;
                // FLAG_RCV state that should receive a valid Address (0x03)
                case FLAG_RCV:
                    if(byteRCV == FLAG){break;}
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
                    if(byteRCV == FLAG){llState = FLAG_RCV;break;}
                    else if(byteRCV == 0x03){
                        llState = C_RCV;
                        message.control = 0x03;
                        break;
                        }
                    else{llState = START;}
                    break;
                // C_RCV state that should receive a valid BCC (A ^ C)
                case C_RCV:
                    if(byteRCV == FLAG){llState = FLAG_RCV;break;}
                    else if(byteRCV == (message.control ^ message.address)){llState = BCC1_OK;break;}
                    else{llState = START;}
                    break;
                // BCC_OK state that should receive a valid flag (FLAG)
                case BCC1_OK:
                    if(byteRCV == FLAG){llState = STOP;break;}
                    else{llState = START;}
                    break;
                default:
                    break;
            }
        }
        // transmitin UA
        int ret = sendSupervisionMessage(A_TX, C_UA);
        return ret;
    }

    return 0;
}

int stuffData(const unsigned char *buf, int bufSize, unsigned char** stuffedBuf, unsigned char* bcc2){
    *bcc2 = 0;
    // allocate memory for worst case scenario
    unsigned char* stuffedBuffer = (unsigned char*) malloc(sizeof(unsigned char) * bufSize * 2);
    if(!stuffedBuffer){
        printf("Error allocating space for stuffed buffer!\n");
        return -1;
    }

    // Get bcc2 from singular bytes from payload and generate stuffedPayload
    int bytesInserted = 0;
    for(int i = 0; i < bufSize; i++){
        *bcc2 ^= buf[i];
        // Data that needs to be escaped
        if(buf[i] == FLAG || buf[i] == 0x7d){
            stuffedBuffer[bytesInserted++] = 0x7d;
            stuffedBuffer[bytesInserted++] = buf[i] ^ 0x20;
        }else{
            stuffedBuffer[bytesInserted++] = buf[i];
        }
    }


    // deallocate not used bytes
    unsigned char* temp = (unsigned char*) realloc(stuffedBuffer, sizeof(unsigned char) * bytesInserted);
    if(!temp){
        free(temp);
        free(stuffedBuf);
        printf("Couldn't reallocate memory!\n");
        return -1;
    }
    *stuffedBuf = temp;
    return bytesInserted;
}

////////////////////////////////////////////////
// LLWRITE
////////////////////////////////////////////////
int llwrite(const unsigned char *buf, int bufSize)
{   
    if(buf == NULL){
        printf("Payload Pointer is NULL!\n");
        return -1;
    }
    if(bufSize > MAX_PAYLOAD_SIZE){
        printf("Payload exceeds max payload size (1000 bytes)\n");
        return -1;
    }
    unsigned char* stuffedBuf;
    unsigned char bcc2;

    int stuffedSize = stuffData(buf, bufSize, &stuffedBuf, &bcc2);
    if(stuffedSize == -1){
        printf("Couldn't allocate memory for stuffed data!\n");
        return -1;
    }
    printf("BCC2 is %x\n", bcc2);

    // Information frame is ready for shipment
    int messageSize = 6 + stuffedSize;
    unsigned char* message = (unsigned char*) malloc(messageSize);
    message[0] = FLAG;
    message[1] = A_TX;
    message[2] = (frameNr << 7);
    message[3] = BCC1(message[1], message[2]);
    memcpy(&message[4], stuffedBuf, stuffedSize);
    message[messageSize - 2] = bcc2;
    message[messageSize - 1] = FLAG;

    
    

    // retransmission logic
    // assign alarHandler
    (void) signal(SIGALRM, alarmHandler);
    alarmEnabled = FALSE;
    alarmCount = 0;
    LinkLayerState llState;
    llState = START;

    Message received;
    
    while(alarmCount <= retransmissions && llState != STOP){

        if(alarmEnabled == FALSE){
            alarm(alarmTimeout);
            // logic to send message again
            writeBytesSerialPort(message, messageSize);
            alarmEnabled = TRUE;
        }

        unsigned char byteRCV;
        int ret = readByteSerialPort(&byteRCV);
        if(ret != 1) continue;

        switch (llState)
        {
        case START:
            printf("START->0x%x\n", byteRCV);
            memset(&received, 0, sizeof(Message));
            // Got flag
            if(byteRCV == FLAG){
                llState = FLAG_RCV;
                received.flag = FLAG;
            }
            break;
        // FLAG_RCV state that should receive a valid address : 0x03
        case FLAG_RCV:
            printf("FLAG_RCV->0x%x\n", byteRCV);
            if(byteRCV == FLAG){break;}
            else if(byteRCV == A_TX){
                llState = A_RCV;
                received.address = A_TX;
            }else{
                llState = START;
            }
            break;
        // A_RCV state that should receive a valid control byte : (RR0) -> 0xAA (RR1) -> 0xAB (REJ0) -> 0x54 (REJ1) -> 0x55
        case A_RCV:
            printf("A_RCV->0x%x\n", byteRCV);
            printf("FRAME NUMBER : %x\n", frameNr );
            if(byteRCV == FLAG){llState = FLAG_RCV; break;}
            // received response
            else if(byteRCV == (C_RR0 + (frameNr ^ 0x1)) || byteRCV == (C_REJ0 + frameNr)){
                // frame was accepted
                llState = C_RCV;
                received.control = byteRCV;
                break;
            }
            else{llState = START;}
            break;
        // C_RCV state that should receive a valid BCC
        case C_RCV:
            printf("C_RCV->0x%x\n", byteRCV);
            if(byteRCV == FLAG){llState = FLAG_RCV; break;}
            else if(byteRCV == BCC1(received.control, received.address)){
                llState = BCC1_OK;
                break;
            }else{
                llState = START;
            }
            break;
        case BCC1_OK:
            if(byteRCV == FLAG){
                // check if frame was reject frame
                if(received.control == (C_REJ0 + frameNr)){
                    llState = START;
                    writeBytesSerialPort(message, messageSize);
                    break;
                }
                llState = STOP;
                break;
            }
            else{llState = START;}
            break;
        default:
            break;
        }

    }
    if(llState == STOP){
        frameNr ^= 1;
        return 0;
    }
    return 1;
}

////////////////////////////////////////////////
// LLREAD
////////////////////////////////////////////////
int llread(unsigned char *packet)
{
    LinkLayerState llState = START;
    Message message;
    int idx = 0;

    unsigned char bcc2_input = 0x0;

    // receives a packet, that could be a SET frame or a I frame containing data
    while(llState != STOP){

        unsigned char byteRCV;
        // receive bytes
        int ret = readByteSerialPort(&byteRCV);
        if(ret != 1) continue;

        switch (llState)
        {
        // this should receive a valid flag
        case START:
            printf("START->0x%x\n", byteRCV);
            if(byteRCV == FLAG){
                llState = FLAG_RCV;
                message.flag = FLAG;
            } 
            break;
        // this should receive a valid address 0x03 (from transmitter)
        case FLAG_RCV:
            idx = 0;
            printf("FLAG_RCV->0x%x\n", byteRCV);
            if(byteRCV == FLAG) break;
            else if(byteRCV == A_TX){
                llState = A_RCV;
                message.address = byteRCV;
                break;
            }
            llState = START;
            break;
        // this should receive a valid control (0x03 if it is a set frame, 0 or 0x80 for a info frame)
        case A_RCV:
            printf("A_RCV->0x%x\n", byteRCV);
            // received a set frame (llopen did not go through)
            if(byteRCV == C_SET){
                llState = C_RCV;
                message.control = C_SET;
                break;
            }
            // received data
            else if(byteRCV == 0 || byteRCV == 0x80){
                llState = C_RCV;
                message.control = byteRCV;
                break;
            }
            else if(byteRCV == FLAG){llState = FLAG_RCV; break;}
            llState = START;
            break;
        // this should receive a valid BCC1 
        // if it receives a valid BCC1 then proceeds to read data
        case C_RCV: 
            printf("C_RCV->0x%x\n", byteRCV);
            if(byteRCV == BCC1(message.control, message.address)){
                if(message.control == C_SET){llState = BCC1_OK; break;}
                bcc2_input = 0;
                llState = READING_DATA;
                break;
            }
            else if(byteRCV == FLAG){llState = FLAG_RCV; break;}
            else llState = START;
            break;
        // BCC is ok 
        case BCC1_OK:
            // error llread cant continue because llopen did not go through
            if(byteRCV == FLAG){return -1;}
            llState = START;
            break;
        // READING DATA
        case READING_DATA:
            // if its escaped data
            if(byteRCV == ESC){
                llState = ESCAPED_DATA;
                break;
            }
            // if flag is received, last byte was BCC2 (end of frame)
            else if(byteRCV == FLAG){
                packet[idx--] = 0;
                if(bcc2_input == 0){
                    // acknowledge frame
                    sendSupervisionMessage(A_TX, C_RR0 + ((message.control >> 7) ^ 0x1));
                    frameNr = message.control ^ 0x1;
                    return idx;
                }else{
                    // reject frame
                    sendSupervisionMessage(A_TX, C_REJ0 + (message.control >> 7));
                    memset(packet, 0, idx);
                    llState = START;
                    break;
                }
            }else{
                packet[idx++] = byteRCV;
                bcc2_input ^= byteRCV;
                break;
            }
            break;
        // ESCAPED DATA (byteRCV is not a special character)
        case ESCAPED_DATA:
            packet[idx++] = (byteRCV ^ 0x20);
            bcc2_input ^= (byteRCV ^ 0x20);
            llState = READING_DATA;
            break;
        default:
            break;
        }
    }

    
    return 0;
}

////////////////////////////////////////////////
// LLCLOSE
////////////////////////////////////////////////
int llclose(int showStatistics)
{
    int clstat = closeSerialPort();
    return clstat;
}
