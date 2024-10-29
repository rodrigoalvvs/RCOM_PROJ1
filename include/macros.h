// MACROS for link layer

#ifndef _MACROS_H_
#define _MACROS_H_

// Flag
#define FLAG 0x7E

// Address
#define A_RX 0x01 // Address for receiver commands or transmitter replies
#define A_TX 0x03 // Address for transmitter commands or receiver replies

// Control

#define C_SET 0x03
#define C_UA 0x07

#define C_RR0 0xAA
#define C_RR1 0xAB

#define C_REJ0 0x54
#define C_REJ1 0x55

#define C_DISC 0x0B

// Information frames
#define C_I0 0
#define C_I1 0x80


// MISC

#define ESC 0x7D

#define BCC1(a,b) (a ^ b) // Get bcc result (just xor)

#define FRAME_SIZE 5


#endif

