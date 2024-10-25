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


int nrFrames = 0;
int nrRetransmissions = 0;
int nrTimeouts = 0;

LinkLayer curLL;

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

////////////////////////////////////////////////
// alarmHandler
////////////////////////////////////////////////

void alarmHandler(int signal){
    alarmEnabled = FALSE;
    alarmCount++;
}

////////////////////////////////////////////////
// sendSupervisionMessage
////////////////////////////////////////////////

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
// stuffData
////////////////////////////////////////////////

int stuffData(const unsigned char *buf, int bufSize, unsigned char** stuffedBuf){
    unsigned char bcc2 = 0;

    // allocate memory for worst case scenario
    unsigned char* stuffedBuffer = (unsigned char*) malloc(sizeof(unsigned char) * bufSize * 2);
    if(!stuffedBuffer){
        printf("Error allocating space for stuffed buffer!\n");
        return -1;
    }

    // Get bcc2 from singular bytes from payload and generate stuffedPayload
    int bytesInserted = 0;
    for(int i = 0; i < bufSize; i++){
        bcc2 ^= buf[i];
        // Data that needs to be escaped
        if(buf[i] == FLAG || buf[i] == ESC){
            stuffedBuffer[bytesInserted++] = ESC;
            stuffedBuffer[bytesInserted++] = buf[i] ^ 0x20;
        }else{
            stuffedBuffer[bytesInserted++] = buf[i];
        }
    }
    
    // insert bcc2 in stuffedBuf
    if(bcc2 == FLAG){
        // stuff bcc2
        stuffedBuffer[bytesInserted++] = ESC;
        stuffedBuffer[bytesInserted++] = bcc2 ^ 0x20;
    }else{
        stuffedBuffer[bytesInserted++] = bcc2;
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
    curLL = connectionParameters;
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

////////////////////////////////////////////////
// LLWRITE
////////////////////////////////////////////////
int llwrite(const unsigned char *buf, int bufSize)
{   
    printf("SENDING frame %d\n", frameNr);
    if(buf == NULL){
        printf("Payload Pointer is NULL!\n");
        return -1;
    }
    if(bufSize > MAX_PAYLOAD_SIZE){
        printf("Payload exceeds max payload size (1000 bytes)\n");
        return -1;
    }

    (void) signal(SIGALRM, alarmHandler);

    unsigned char* stuffedBuf;
    int stuffedSize = stuffData(buf, bufSize, &stuffedBuf);
    
    if(stuffedSize == -1){
        printf("Couldn't allocate memory for stuffed data!\n");
        return -1;
    }

    // Information frame is ready for shipment
    int messageSize = FRAME_SIZE + stuffedSize;
    unsigned char* message = (unsigned char*) malloc(messageSize);
    message[0] = FLAG;
    message[1] = A_TX;
    message[2] = (frameNr << 7);
    message[3] = BCC1(message[1], message[2]);
    memcpy(&message[4], stuffedBuf, stuffedSize);
    message[messageSize - 1] = FLAG;

    // Initialize alarm
    alarmEnabled = FALSE;
    alarmCount = 0;
    
    LinkLayerState llState = START;
    Message received;
    
    while(alarmCount <= retransmissions){
        
        // enable alarm and send
        if(alarmEnabled == FALSE){
            alarm(alarmTimeout);
            // logic to send message again
            writeBytesSerialPort(message, messageSize);
            alarmEnabled = TRUE;
        }

        // llwrite should receive either a RR(frameNr ^ 0x1) or REJ(frameNr) 
        unsigned char byteRCV;
        int ret = readByteSerialPort(&byteRCV);
        if(ret != 1 && llState != STOP) continue;

        switch (llState)
        {
        case START:
            //printf("START->0x%x\n", byteRCV);
            memset(&received, 0, sizeof(Message));
            // Got flag
            if(byteRCV == FLAG){
                llState = FLAG_RCV;
                received.flag = FLAG;
            }
            break;
        // FLAG_RCV state that should receive a valid address : 0x03
        case FLAG_RCV:
            //printf("FLAG_RCV->0x%x\n", byteRCV);
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
            //printf("A_RCV->0x%x\n", byteRCV);
            //printf("FRAME NUMBER : %x\n", frameNr );
            if(byteRCV == FLAG){llState = FLAG_RCV; break;}
            // received response
            else if(byteRCV == (C_RR0 + (frameNr ^ 0x1)) || byteRCV == (C_REJ0 + frameNr)){
                // frame was accepted or rejected
                llState = C_RCV;
                received.control = byteRCV;
                break;
            }
            else{llState = START;}
            break;
        // C_RCV state that should receive a valid BCC
        case C_RCV:
            //printf("C_RCV->0x%x\n", byteRCV);
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
                // check if frame was rejected
                if(received.control == (C_REJ0 + frameNr)){
                    llState = START;
                    alarmEnabled = FALSE;
                    alarmCount = 0;
                    printf("FRAME WAS REJECTED!\n");
                    break;
                }
                llState = STOP;
                break;
            }
            else{llState = START;}
            break;
        case STOP:
            printf("FRAME GOT RR!\n");
            frameNr ^= 0x1;
            return bufSize;
        default:
            break;
        }
    }
    printf("Max retransmissions reached!\n");
    return -1;
}

////////////////////////////////////////////////
// LLREAD
////////////////////////////////////////////////
int llread(unsigned char *packet)
{
    LinkLayerState llState = START;
    Message message;
    int bytesReceived = 0;
    unsigned char bcc2_input = 0x0;
    printf("RECEIVING frame %d\n", frameNr);
    // receives a packet, that could be a SET frame(return to llopen) or a I frame containing data
    while(TRUE){

        unsigned char byteRCV;
        // receive bytes
        int ret = readByteSerialPort(&byteRCV);
        if(ret != 1 && llState != STOP) continue;

        switch (llState)
        {
        // this should receive a valid flag
        case START:
            //printf("START->0x%x\n", byteRCV);
            if(byteRCV == FLAG){
                bytesReceived = 0;
                memset(&message, 0, sizeof(Message));
                llState = FLAG_RCV;
                message.flag = FLAG;
            } 
            break;
        // this should receive a valid address 0x03 (from transmitter)
        case FLAG_RCV:
            //printf("FLAG_RCV->0x%x\n", byteRCV);
            bytesReceived = 0;
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
            //printf("A_RCV->0x%x\n", byteRCV);
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
            // printf("C_RCV->0x%x\n", byteRCV);
            if(byteRCV == BCC1(message.control, message.address)){
                if(message.control == C_SET){llState = BCC1_OK; break;}
                bcc2_input = 0;
                llState = READING_DATA;
                break;
            }
            else if(byteRCV == FLAG){llState = FLAG_RCV; break;}
            else llState = START;
            break;
        // BCC is ok (this state only reaches when control = SET frame)
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
                llState = STOP;
            }else{
                packet[bytesReceived++] = byteRCV;
                bcc2_input ^= byteRCV;
                break;
            }
            break;
        // ESCAPED DATA (byteRCV is not a special character)
        case ESCAPED_DATA:
            packet[bytesReceived++] = byteRCV ^ 0x20;
            bcc2_input ^= (byteRCV ^ 0x20);
            llState = READING_DATA;
            break;
        case STOP:
            // overwrite bcc2 that was inputed with 0
            packet[bytesReceived--] = 0;
            // should be 0, because the bcc2_input was xor'd with bcc2
            if(bcc2_input == 0 && message.control == (frameNr << 7)){
                // acknowledge frame
                printf("Accepting frame!\n");
                sendSupervisionMessage(A_TX, C_RR0 + ((message.control >> 7) ^ 0x1));
                frameNr = frameNr ^ 0x1;
                return bytesReceived;
            }
            else{
                // reject frame
                printf("Rejecting frame!\n");
                sendSupervisionMessage(A_TX, C_REJ0 + (message.control >> 7));
                memset(packet, 0, bytesReceived);
                llState = START;
                break;
            }

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
    alarmEnabled = FALSE;
    alarmCount = 0;
    (void) signal(SIGALRM, alarmHandler);   

    LinkLayerState llState = START;
    Message received;
    
    if(curLL.role == LlTx){
        // transmitter
        while(alarmCount <= retransmissions && llState != STOP){

            if(alarmEnabled == FALSE){
                alarm(alarmTimeout);
                sendSupervisionMessage(A_TX, C_DISC);
                alarmEnabled = TRUE;
            }

            unsigned char byteRCV;
            int ret = readByteSerialPort(&byteRCV);
            if(ret != 1) continue;

            switch (llState)
            {
            case START:
                printf("START: 0x%x\n", byteRCV);
                memset(&received, 0, sizeof(Message));
                if(byteRCV == FLAG){
                    llState = FLAG_RCV;
                    received.flag = FLAG;
                }
                break;
            case FLAG_RCV:
                printf("FLAG_RCV: 0x%x\n", byteRCV);
                if(byteRCV == FLAG)break;
                else if(byteRCV == A_RX) {
                    llState = A_RCV;
                    received.address = byteRCV;
                    break;
                }
                llState = START;
                break;
            case A_RCV:
                printf("A_RCV: 0x%x\n", byteRCV);
                if(byteRCV == FLAG){llState = FLAG_RCV; break;}
                else if(byteRCV == C_DISC){
                    llState = C_RCV;
                    received.control = byteRCV;
                    break;
                }
                llState = START;
                break;
            case C_RCV:
                printf("C_RCV: 0x%x\n", byteRCV);
                if(byteRCV == FLAG){llState = FLAG_RCV; break;}
                else if(byteRCV == BCC1(received.control, received.address)){
                    llState = BCC1_OK;
                    break;
                }
                llState = START;
                break;
            case BCC1_OK:
                if(byteRCV == FLAG){
                    // received correct DISC frame and send UA frame
                    printf("RECEIVED DISC!\n");
                    sendSupervisionMessage(A_RX, C_UA);   
                    llState = STOP;
                    break;  
                }
                break;
            default:
                break;
            }

        }


        
    }
    else{
        // receiver
        // block alarm until i have sent a DISC frame
        alarmEnabled = TRUE;

        while(alarmCount <= retransmissions && llState != STOP){
            // receive the DISC FRAME
            if(alarmEnabled == FALSE){
                alarm(alarmTimeout);
                sendSupervisionMessage(A_RX, C_DISC);
                alarmEnabled = TRUE;
            }

            unsigned char byteRCV;
            int ret = readByteSerialPort(&byteRCV);
            if(ret != 1) continue;
            
            switch (llState)
            {
            case START:
                printf("START: 0x%x\n", byteRCV);
                memset(&received, 0, sizeof(Message));
                if(byteRCV == FLAG){llState = FLAG_RCV;break;}
                break;
            case FLAG_RCV:
                printf("FLAG_RCV: 0x%x\n", byteRCV);
                if(byteRCV == FLAG){break;}
                // address could be tx (if it is DISC) or rx (if it is UA)
                else if(byteRCV == A_TX || byteRCV == A_RX){
                    received.address = byteRCV;
                    llState = A_RCV;
                    break;
                }
                llState = START;
                break;
            case A_RCV:
                printf("A_RCV: 0x%x\n", byteRCV);
                if(byteRCV == FLAG){llState = FLAG_RCV;break;}
                else if(byteRCV == C_DISC || byteRCV == C_UA){
                    received.control = byteRCV;
                    llState = C_RCV;
                    break;
                }
                llState = START;
                break;
            case C_RCV:
                printf("C_RCV: 0x%x\n", byteRCV); 
                if(byteRCV == FLAG){llState = FLAG_RCV;break;}
                else if(byteRCV == BCC1(received.control, received.address)){
                    llState = BCC1_OK;
                    break;
                }
                llState = START;
                break;
            case BCC1_OK:
                if(byteRCV == FLAG){
            
                    if(received.control == C_UA){
                        llState = STOP;
                        printf("GOT C_UA\n");
                        break;
                    }
                    if(received.control == C_DISC){
                        // send C_DISC
                        printf("GOT C_DISC\n");
                        sendSupervisionMessage(A_RX, C_DISC);
                        alarmEnabled = FALSE;
                        llState = START;
                        break;
                    }
                }
                llState = START;
                break;
            default:
                break;
            }
        }
    }
    if(llState != STOP){
        printf("Couldn't close!\n");
        return -1;
    }

    if(showStatistics > 0){
        // print statistics
        printf("# Frames: %d\n", 1);
        printf("# Retransmissions: %d\n", 1);
        printf("# Timeouts: %d\n", 1);
    }
    int clstat = closeSerialPort();
    return clstat;
}
