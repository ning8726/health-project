#include <Arduino.h>

class AnalogI2C {
 public:
  AnalogI2C(uint8_t dataPin, uint8_t clockPin):sda(dataPin),scl(clockPin){}
  uint16_t idleSda=0,idleScl=0;

  bool begin() {
    release(sda);release(scl);delay(3);
    uint32_t a=0,b=0;
    for(uint8_t i=0;i<8;++i){a+=analogRead(sda);b+=analogRead(scl);}
    idleSda=a/8;idleScl=b/8;
    online=idleSda>=400&&idleScl>=400&&idleSda<3200&&idleScl<3200;
    highSda=idleSda/2;highScl=idleScl/2;
    return online;
  }
  void beginTransmission(uint8_t address){target=address;txCount=0;}
  void write(uint8_t value){if(txCount<sizeof(tx))tx[txCount++]=value;}
  uint8_t endTransmission(bool sendStop=true) {
    if(!online)return 4;
    if(!start()||!writeByte(uint8_t(target<<1))){stop();return 2;}
    for(uint8_t i=0;i<txCount;++i)if(!writeByte(tx[i])){stop();return 3;}
    if(sendStop)stop();
    return 0;
  }
  uint8_t requestFrom(uint8_t address,uint8_t n) {
    rxCount=rxPos=0;
    if(n>sizeof(rx)||!start()||!writeByte(uint8_t((address<<1)|1))){stop();return 0;}
    for(uint8_t i=0;i<n;++i){
      uint8_t value=0;
      if(!readByte(value,i+1<n)){stop();return 0;}
      rx[rxCount++]=value;
    }
    stop();return rxCount;
  }
  int available(){return rxCount-rxPos;}
  int read(){return rxPos<rxCount?rx[rxPos++]:-1;}

 private:
  uint8_t sda,scl,target=0,tx[4]={},rx[8]={},txCount=0,rxCount=0,rxPos=0;
  uint16_t highSda=700,highScl=700;
  bool online=false;
  static void low(uint8_t pin){digitalWrite(pin,LOW);pinMode(pin,OUTPUT);}
  static void release(uint8_t pin){digitalWrite(pin,LOW);pinMode(pin,INPUT);}
  static void half(){delayMicroseconds(4);}
  bool clockHigh(){
    release(scl);uint32_t began=micros();
    do{if(analogRead(scl)>highScl)return true;}
    while(uint32_t(micros()-began)<1500);
    return false;
  }
  bool start(){
    release(sda);half();if(!clockHigh())return false;
    if(analogRead(sda)<=highSda)return false;
    half();low(sda);half();low(scl);return true;
  }
  void stop(){low(sda);half();clockHigh();half();release(sda);half();}
  bool writeByte(uint8_t value){
    for(uint8_t i=0;i<8;++i){
      if(value&0x80)release(sda);else low(sda);
      half();if(!clockHigh())return false;half();low(scl);value<<=1;
    }
    release(sda);half();if(!clockHigh())return false;
    bool ack=analogRead(sda)<=highSda;half();low(scl);return ack;
  }
  bool readByte(uint8_t &value,bool ack){
    release(sda);value=0;
    for(uint8_t i=0;i<8;++i){
      half();if(!clockHigh())return false;
      value=uint8_t((value<<1)|(analogRead(sda)>highSda?1:0));
      half();low(scl);
    }
    if(ack)low(sda);else release(sda);
    half();if(!clockHigh())return false;half();low(scl);release(sda);return true;
  }
};

AnalogI2C bus(A2,A3);

bool readRegister(uint8_t address,uint8_t reg,uint8_t &value){
  bus.beginTransmission(address);bus.write(uint8_t(reg<<1));
  if(bus.endTransmission(true)!=0)return false;
  if(bus.requestFrom(address,1)!=1)return false;
  value=uint8_t(bus.read());return true;
}

void scanBus(){
  bool online=bus.begin();
  Serial.print("A2/A3 online=");Serial.print(online?"yes":"no");
  Serial.print(" idleA2=");Serial.print(bus.idleSda);
  Serial.print(" idleA3=");Serial.println(bus.idleScl);
  if(online){
    Serial.print("ACK addresses:");
    for(uint8_t address=1;address<127;++address){
      bus.beginTransmission(address);
      if(bus.endTransmission()==0){
        Serial.print(" 0x");
        if(address<16)Serial.print('0');
        Serial.print(address,HEX);
      }
    }
    Serial.println();
    uint8_t id=0;
    if(readRegister(0x68,0x00,id)){
      Serial.print("Chip ID at 0x68: 0x");
      if(id<16)Serial.print('0');
      Serial.println(id,HEX);
    }else{
      Serial.println("No readable register at 0x68");
    }
  }
}

void setup(){
  Serial.begin(115200);
  analogReadResolution(12);
}

void loop(){
  delay(1200);
  scanBus();
  delay(2000);
}
