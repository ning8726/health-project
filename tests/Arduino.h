#pragma once
// Host-only Arduino API substitute. It does not model physical sensor behaviour.
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <string>
#define LOW 0
#define HIGH 1
#define INPUT 0
#define OUTPUT 1
static constexpr uint8_t A0=14,A1=15,A2=16,A3=17,A4=18,A5=19;
extern uint64_t mockMicros;
inline uint32_t micros(){return uint32_t(mockMicros);}
inline uint32_t millis(){return uint32_t(mockMicros/1000);}
inline void delay(unsigned long ms){mockMicros+=ms*1000;}
inline void delayMicroseconds(unsigned int us){mockMicros+=us;}
inline void pinMode(uint8_t,int){}
inline void digitalWrite(uint8_t,int){}
inline int digitalRead(uint8_t){return LOW;}
inline int analogRead(uint8_t){return 2000;}
inline void analogReadResolution(int){}
struct MockSerial {
 bool connected=true;std::string output;
 void begin(unsigned long){}
 operator bool()const{return connected;}
 int available()const{return 0;}
 int read(){return -1;}
 int availableForWrite()const{return 64;}
 size_t write(const uint8_t *p,size_t n){output.append(reinterpret_cast<const char*>(p),n);return n;}
};
extern MockSerial Serial;
