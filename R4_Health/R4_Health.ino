// Health / exercise prototype: UNO R4 Minima + Dragino Yun + sensor shield.
// One OLED rotates HR -> reps -> movement every 4 seconds. No ultrasonic sensor.
// Install SparkFun MAX3010x Pulse and Proximity Sensor Library for heartRate.h.
// Original custom drivers retained and updated; read START_HERE.md and wiring.png.


// ===== Pose-based repetition counter; independently host-tested =====
#include <stdint.h>
#include <math.h>
namespace workout {
enum State:uint8_t {IDLE,CAL_START,CAL_TARGET,READY,RUNNING,PAUSED,DONE};
enum Phase:uint8_t {SEEK_START,SEEK_TARGET,SEEK_RETURN};
enum Feedback:uint8_t {NONE,FIND_START,REACH_TARGET,RETURN_START,NO_ACCEL,TOO_FAST,
                       CAL_TOO_SMALL,HOLD_STILL,MOVE_TIMEOUT};
struct Engine {
 State state=IDLE; Phase phase=SEEK_START; uint8_t reps=0,goal=10;
 uint32_t accumulatedMs=0,activeSince=0,cycleSince=0,zoneSince=0,lastValidMs=0,noticeSince=0;
 uint8_t heldZone=0; Feedback notice=NONE; bool sensorValid=false;
 static constexpr uint16_t ENDPOINT_HOLD_MS=600,MIN_CYCLE_MS=1000;
 static constexpr uint32_t MAX_CYCLE_MS=30000;
 static constexpr float MIN_SPAN_DEG=25.0f;
 void reset(){uint8_t g=goal;*this=Engine();goal=g;}
 uint32_t elapsedMs(uint32_t now)const{return accumulatedMs+(state==RUNNING?uint32_t(now-activeSince):0);}
 void start(uint32_t now){reps=0;accumulatedMs=0;activeSince=now;lastValidMs=now;
   state=RUNNING;phase=SEEK_START;heldZone=0;notice=NONE;}
 void pause(uint32_t now){if(state!=RUNNING)return;accumulatedMs+=uint32_t(now-activeSince);
   state=PAUSED;phase=SEEK_START;heldZone=0;}
 void resume(uint32_t now){if(state!=PAUSED)return;state=RUNNING;activeSince=now;
   lastValidMs=now;phase=SEEK_START;heldZone=0;notice=NONE;}
 void notify(Feedback f,uint32_t now){notice=f;noticeSince=now;}
 bool hold(uint8_t zone,bool inside,uint32_t now){
   if(!inside){heldZone=0;return false;}
   if(heldZone!=zone){heldZone=zone;zoneSince=now;return false;}
   return uint32_t(now-zoneSince)>=ENDPOINT_HOLD_MS;
 }
 void update(uint32_t now,bool valid,bool stable,float homeError,float targetError,float span){
   sensorValid=valid;
   if(state!=RUNNING)return;
   if(!valid){heldZone=0;if(uint32_t(now-lastValidMs)>600)phase=SEEK_START;return;}
   lastValidMs=now;
   if(phase!=SEEK_START && uint32_t(now-cycleSince)>MAX_CYCLE_MS){
     phase=SEEK_START;heldZone=0;notify(MOVE_TIMEOUT,now);return;}
   if(!isfinite(homeError)||!isfinite(targetError)||!isfinite(span)||span<MIN_SPAN_DEG){
     heldZone=0;phase=SEEK_START;return;}
   float tolerance=span/5; if(tolerance<6)tolerance=6;if(tolerance>12)tolerance=12;
   bool home=stable&&homeError<=tolerance;
   bool target=stable&&targetError<=tolerance;
   if(phase==SEEK_START&&hold(1,home,now)){
     phase=SEEK_TARGET;cycleSince=now;heldZone=0;
   }else if(phase==SEEK_TARGET&&hold(2,target,now)){
     phase=SEEK_RETURN;heldZone=0;
   }else if(phase==SEEK_RETURN&&hold(1,home,now)){
     if(uint32_t(now-cycleSince)>=MIN_CYCLE_MS){
       ++reps;if(reps>=goal){accumulatedMs+=uint32_t(now-activeSince);state=DONE;}
     }else notify(TOO_FAST,now);
     phase=SEEK_TARGET;cycleSince=now;heldZone=0;
   }
 }
 Feedback feedback(uint32_t now)const{
   if(state==RUNNING&&!sensorValid)return NO_ACCEL;
   if(notice!=NONE&&uint32_t(now-noticeSince)<2500)return notice;
   if(state!=RUNNING)return NONE;
   return phase==SEEK_START?FIND_START:phase==SEEK_TARGET?REACH_TARGET:RETURN_START;
 }
};
struct PageCycle {
 uint8_t page=0;uint32_t since=0;
 static constexpr uint32_t PERIOD=4000;
 void update(uint32_t now){uint32_t n=uint32_t(now-since)/PERIOD;
   if(n){page=uint8_t((page+n)%3);since+=n*PERIOD;}}
 void next(uint32_t now){page=uint8_t((page+1)%3);since=now;}
};
}
// ===== AnalogI2C.h =====
#include <Arduino.h>

// R4-specific prototype bus for separate low-voltage sensor buses. The board's own pull-ups
// provide HIGH. These pins never drive HIGH and never enable 5 V pull-ups.
// ADC sensing avoids assuming a 1.8/3.3 V HIGH meets the R4's 5 V digital VIH.
class AnalogI2C {
 public:
  AnalogI2C(uint8_t dataPin,uint8_t clockPin):sda(dataPin),scl(clockPin){}
  uint16_t idleSda=0,idleScl=0;
  bool begin() {
    release(sda);release(scl);delay(3);
    uint32_t a=0,b=0;
    for(uint8_t i=0;i<8;++i){a+=analogRead(sda);b+=analogRead(scl);}
    idleSda=a/8;idleScl=b/8;
    // With 12-bit ADC and default 5 V reference, a sensor bus should idle
    // above noise but below approximately 3.9 V. Do not accept a 5 V bus.
    online=idleSda>=400 && idleScl>=400 && idleSda<3200 && idleScl<3200;
    highSda=idleSda/2;highScl=idleScl/2;
    return online;
  }
  void beginTransmission(uint8_t address){target=address;txCount=0;txOverflow=false;}
  void write(uint8_t b){if(txCount<sizeof(tx))tx[txCount++]=b;else txOverflow=true;}
  uint8_t endTransmission(bool sendStop=true) {
    if(!online||txOverflow){txOverflow=false;stop();return 4;}
    if(!start()||!writeByte(uint8_t(target<<1))){stop();return 2;}
    for(uint8_t i=0;i<txCount;++i)if(!writeByte(tx[i])){stop();return 3;}
    if(sendStop)stop();
    return 0;
  }
  uint8_t requestFrom(uint8_t address,uint8_t n) {
    rxCount=0;rxPos=0;
    if(n>sizeof(rx)||!online||!start()||!writeByte(uint8_t((address<<1)|1))){stop();return 0;}
    for(uint8_t i=0;i<n;++i) {
      uint8_t b=0;
      if(!readByte(b,i+1<n)){stop();return 0;}
      rx[rxCount++]=b;
    }
    stop();return rxCount;
  }
  int available(){return rxCount-rxPos;}
  int read(){return rxPos<rxCount?rx[rxPos++]:-1;}
 private:
  uint8_t sda,scl,target=0,tx[32]={},rx[32]={},txCount=0,rxCount=0,rxPos=0;
  uint16_t highSda=700,highScl=700;
  bool online=false,txOverflow=false;
  static void low(uint8_t p){digitalWrite(p,LOW);pinMode(p,OUTPUT);}
  static void release(uint8_t p){digitalWrite(p,LOW);pinMode(p,INPUT);}
  static void half(){delayMicroseconds(4);}
  bool clockHigh() {
    release(scl);uint32_t began=micros();
    do {if(analogRead(scl)>highScl)return true;}while(uint32_t(micros()-began)<1500);
    return false;
  }
  bool start() {
    release(sda);half();
    if(!clockHigh())return false;
    if(analogRead(sda)<=highSda)return false;
    half();low(sda);half();low(scl);return true;
  }
  void stop(){low(sda);half();clockHigh();half();release(sda);half();}
  bool writeByte(uint8_t value) {
    for(uint8_t i=0;i<8;++i) {
      if(value&0x80)release(sda);else low(sda);
      half();if(!clockHigh())return false;half();low(scl);value<<=1;
    }
    release(sda);half();if(!clockHigh())return false;
    bool ack=analogRead(sda)<=highSda;half();low(scl);return ack;
  }
  bool readByte(uint8_t &value,bool ack) {
    release(sda);value=0;
    for(uint8_t i=0;i<8;++i) {
      half();if(!clockHigh())return false;
      value=uint8_t((value<<1)|(analogRead(sda)>highSda?1:0));half();low(scl);
    }
    if(ack)low(sda);else release(sda);
    half();if(!clockHigh())return false;half();low(scl);release(sda);return true;
  }
};


// ===== HeartSensor.h =====
#include <Arduino.h>
#include <heartRate.h> // SparkFun MAX3010x library: software beat detector only

// Direct FIFO readers for the two common purple-board chips. Both are sampled
// at 50 Hz; samples are interpolated to the detector's 100 Hz time base.
// Part ID is checked before chip-specific register writes.
template<typename Bus>
class HeartSensorBase {
 public:
  explicit HeartSensorBase(Bus &b): bus(b) {}
  uint8_t partId=0;
  uint32_t rawIR=0,overflows=0;
  bool ready=false;
  static constexpr uint32_t MIN_IR_100=4000,MIN_IR_102=10000;

  bool begin() {
    ready=false;clearRate();contactSamples=0;sampleNumber=0;rawIR=0;havePrevious=false;
    if(!read8(0xff,partId)) { partId=0;return false; }
    if(partId!=0x11 && partId!=0x15)return false;
    bool oldChip=partId==0x11;
    uint8_t modeReg=oldChip?0x06:0x09;
    if(!write8(modeReg,0x40))return false;
    uint32_t started=millis();uint8_t mode=0x40;
    do {
      delay(5);
      if(!read8(modeReg,mode))return false;
    } while((mode&0x40) && uint32_t(millis()-started)<200);
    if(mode&0x40)return false;
    bool ok=true;
    if(oldChip) {
      ok=write8(0x01,0) && write8(0x02,0) && write8(0x03,0) && write8(0x04,0)
          && write8(0x07,0x43) // high-resolution, 50 samples/sec, 1600 us pulse
          && write8(0x09,0x44) // red + IR current, 14.2 mA each
          && write8(0x06,0x03); // red + IR mode
    } else {
      ok=write8(0x02,0) && write8(0x03,0) && write8(0x04,0) && write8(0x05,0)
          && write8(0x06,0) && write8(0x08,0) // no averaging or rollover
          && write8(0x0a,0x23) // ADC range 4096, 50 samples/sec, 18-bit
          && write8(0x0c,0x1f) && write8(0x0d,0x1f) // red + IR LED drive
          && write8(0x09,0x03); // red + IR mode
    }
    uint8_t ignored;read8(0x00,ignored);
    ready=ok;lastSampleWall=millis();return ready;
  }
  void poll(uint32_t now) {
    if(!ready || uint32_t(now-lastPoll)<5)return;
    lastPoll=now;
    bool oldChip=partId==0x11;
    uint8_t p[3];
    if(!readBytes(oldChip?0x02:0x04,p,3)){fail();return;}
    uint8_t mask=oldChip?0x0f:0x1f;
    if(p[1]!=0) {
      ++overflows;clearRate();contactSamples=0;havePrevious=false;
      uint8_t first=oldChip?0x02:0x04;
      if(!write8(first,0)||!write8(first+1,0)||!write8(first+2,0))fail();
      return;
    }
    uint8_t available=(p[0]-p[2])&mask;
    for(uint8_t i=0;i<available;++i) {
      uint8_t data[6];
      if(!readBytes(oldChip?0x05:0x07,data,oldChip?4:6)){fail();return;}
      uint32_t ir=oldChip ? (uint32_t(data[0])<<8)|data[1] :
        ((uint32_t(data[3])<<16)|(uint32_t(data[4])<<8)|data[5])&0x3ffff;
      // Linear interpolation halves software-bus traffic while retaining the
      // beat detector's 100 Hz filter/time base. These remain prototype estimates.
      if(!havePrevious){previousIR=ir;havePrevious=true;}
      process((previousIR+ir)/2,now);process(ir,now);previousIR=ir;
    }
    if(uint32_t(now-lastSampleWall)>1000)fail();
  }
  float bpm(uint32_t now) const {
    return ready && contactSamples>=300 && intervalCount>=3 &&
      uint32_t(now-lastBeatWall)<2500 && uint32_t(now-lastSampleWall)<1000 ? rate : 0;
  }
  const char *status(uint32_t now) const {
    if(!ready)return partId && partId!=0x11 && partId!=0x15 ? "Unsupported chip ID" : "Sensor not ready";
    if(contactSamples==0)return "Place finger on sensor";
    return bpm(now)>0 ? "Estimated pulse acquired" : "Hold finger still; acquiring";
  }
  const char *chipName() const {
    return partId==0x11?"MAX30100":partId==0x15?"MAX30102/05 family":"Unknown";
  }
 private:
  Bus &bus;
  uint32_t previousIR=0;
  bool havePrevious=false;
  uint32_t sampleNumber=0,lastBeatSample=0,lastBeatWall=0,lastPoll=0,lastSampleWall=0;
  uint16_t contactSamples=0,intervals[4]={},lastPeriod=0;
  uint8_t intervalCount=0,intervalIndex=0;
  float rate=0;
  void clearRate(){rate=0;intervalCount=0;intervalIndex=0;lastPeriod=0;lastBeatSample=0;lastBeatWall=0;}
  void fail(){ready=false;clearRate();contactSamples=0;}
  bool write8(uint8_t reg,uint8_t value) {
    bus.beginTransmission(0x57);bus.write(reg);bus.write(value);return bus.endTransmission()==0;
  }
  bool read8(uint8_t reg,uint8_t &value){return readBytes(reg,&value,1);}
  bool readBytes(uint8_t reg,uint8_t *data,uint8_t n) {
    bus.beginTransmission(0x57);bus.write(reg);
    if(bus.endTransmission(false)!=0)return false;
    if(bus.requestFrom(uint8_t(0x57),n)!=n){while(bus.available())bus.read();return false;}
    for(uint8_t i=0;i<n;++i)data[i]=uint8_t(bus.read());
    return true;
  }
  void process(uint32_t ir,uint32_t now) {
    ++sampleNumber;lastSampleWall=now;rawIR=ir;
    bool oldChip=partId==0x11;
    bool beat=checkForBeat(int32_t(oldChip?ir:ir/4)); // fit 18-bit stream to detector's 16-bit range
    uint32_t floor=oldChip?MIN_IR_100:MIN_IR_102;
    uint32_t ceiling=oldChip?65527UL:262135UL;
    if(ir<floor || ir>=ceiling){contactSamples=0;clearRate();return;}
    if(contactSamples<1000)++contactSamples;
    if(contactSamples<300 || !beat)return; // settle after contact changes
    if(lastBeatSample==0){lastBeatSample=sampleNumber;return;}
    uint32_t period=sampleNumber-lastBeatSample;
    if(period<27)return; // ignore close duplicate peaks
    lastBeatSample=sampleNumber;
    if(period>200){clearRate();lastBeatSample=sampleNumber;return;}
    if(lastPeriod && (period>lastPeriod*13UL/10 || period<lastPeriod*7UL/10)) {
      intervalCount=0;intervalIndex=0;rate=0;
    }
    lastPeriod=uint16_t(period);
    intervals[intervalIndex]=uint16_t(period);intervalIndex=(intervalIndex+1)%4;
    if(intervalCount<4)++intervalCount;
    uint32_t total=0;for(uint8_t i=0;i<intervalCount;++i)total+=intervals[i];
    rate=6000.0f*intervalCount/total;lastBeatWall=now;
  }
};
using HeartSensor = HeartSensorBase<AnalogI2C>;


// ===== OledDisplay.h =====
#include <Arduino.h>


// ADC-sensed, open-drain, chunked driver for common 0.96-inch 128x64 I2C OLED modules
// using an SSD1306/SSD1315-compatible command set. It probes 0x3C and 0x3D.
class OledDisplay {
public:
  explicit OledDisplay(AnalogI2C &bus) : wire(bus) {}

  bool begin() {
    if(!wire.begin()) { address=0; return false; }
    const uint8_t candidates[] = {0x3C, 0x3D};
    address = 0;
    for (uint8_t candidate : candidates) {
      wire.beginTransmission(candidate);
      if (wire.endTransmission() == 0) {
        address = candidate;
        break;
      }
    }
    if (!address) return false;

    const uint8_t init[] = {
      0xAE,             // display off
      0xD5, 0x80,      // clock divide
      0xA8, 0x3F,      // multiplex: 64 rows
      0xD3, 0x00,      // display offset
      0x40,             // display start line
      0x8D, 0x14,      // internal charge pump
      0x20, 0x00,      // horizontal addressing
      0xA1,             // segment remap
      0xC8,             // COM scan direction
      0xDA, 0x12,      // COM pins for 128x64
      0x81, 0x7F,      // contrast
      0xD9, 0xF1,      // pre-charge
      0xDB, 0x40,      // VCOM detect
      0xA4,             // use RAM contents
      0xA6,             // normal display
      0x2E,             // stop scrolling
      0xAF              // display on
    };
    if (!commands(init, sizeof(init))) {
      address = 0;
      return false;
    }
    clear();
    return startUpdate();
  }

  bool found() const { return address != 0; }
  uint8_t i2cAddress() const { return address; }

  void clear() { memset(frame, 0, sizeof(frame)); }

  void line(uint8_t row, const char *text) {
    if (row >= 8) return;
    memset(frame + row * 128, 0, 128);
    uint8_t x = 0;
    while (*text && x <= 122) {
      drawCharacter(x, row, *text++);
      x += 6;
    }
  }


  bool busy()const{return updating;}
  bool startUpdate(){
    if(!address)return false;
    const uint8_t window[]={0x21,0,0x7f,0x22,0,7};
    if(!commands(window,sizeof(window))){address=0;return false;}
    transferOffset=0;updating=true;return true;
  }
  // At most one 16-byte chunk per loop: heart and ADXL polling runs between chunks.
  void tick(){
    if(!updating||!address)return;
    wire.beginTransmission(address);wire.write(0x40);
    for(uint8_t i=0;i<16;++i)wire.write(frame[transferOffset+i]);
    if(wire.endTransmission()!=0){address=0;updating=false;return;}
    transferOffset+=16;if(transferOffset>=sizeof(frame))updating=false;
  }
  void bigLine(uint8_t row,const char *text){
    if(row>6)return;
    memset(frame+uint16_t(row)*128,0,256);
    uint8_t x=0;
    while(*text&&x<=116){
      const uint8_t *g=glyph(*text++);
      for(uint8_t c=0;c<5;++c)for(uint8_t bit=0;bit<8;++bit)if(g[c]&(1<<bit)){
        pixel(x+c*2,row*8+bit*2);pixel(x+c*2+1,row*8+bit*2);
        pixel(x+c*2,row*8+bit*2+1);pixel(x+c*2+1,row*8+bit*2+1);
      }
      x+=12;
    }
  }

private:
  AnalogI2C &wire;
  bool updating=false;uint16_t transferOffset=0;
  void pixel(uint8_t x,uint8_t y){if(x<128&&y<64)frame[uint16_t(y/8)*128+x]|=uint8_t(1<<(y%8));}
  uint8_t address = 0;
  uint8_t frame[1024] = {0};

  bool commands(const uint8_t *data, size_t length) {
    size_t offset = 0;
    while (offset < length) {
      wire.beginTransmission(address);
      wire.write(0x00);
      uint8_t count = 0;
      while (offset < length && count < 24) {
        wire.write(data[offset++]);
        ++count;
      }
      if (wire.endTransmission() != 0) return false;
    }
    return true;
  }

  static const uint8_t *glyph(char c) {
    // Space, 0-9, then A-Z. Each glyph is five vertical columns.
    static const uint8_t font[][5] = {
      {0x00,0x00,0x00,0x00,0x00},
      {0x3E,0x51,0x49,0x45,0x3E},{0x00,0x42,0x7F,0x40,0x00},
      {0x42,0x61,0x51,0x49,0x46},{0x21,0x41,0x45,0x4B,0x31},
      {0x18,0x14,0x12,0x7F,0x10},{0x27,0x45,0x45,0x45,0x39},
      {0x3C,0x4A,0x49,0x49,0x30},{0x01,0x71,0x09,0x05,0x03},
      {0x36,0x49,0x49,0x49,0x36},{0x06,0x49,0x49,0x29,0x1E},
      {0x7E,0x11,0x11,0x11,0x7E},{0x7F,0x49,0x49,0x49,0x36},
      {0x3E,0x41,0x41,0x41,0x22},{0x7F,0x41,0x41,0x22,0x1C},
      {0x7F,0x49,0x49,0x49,0x41},{0x7F,0x09,0x09,0x09,0x01},
      {0x3E,0x41,0x49,0x49,0x7A},{0x7F,0x08,0x08,0x08,0x7F},
      {0x00,0x41,0x7F,0x41,0x00},{0x20,0x40,0x41,0x3F,0x01},
      {0x7F,0x08,0x14,0x22,0x41},{0x7F,0x40,0x40,0x40,0x40},
      {0x7F,0x02,0x0C,0x02,0x7F},{0x7F,0x04,0x08,0x10,0x7F},
      {0x3E,0x41,0x41,0x41,0x3E},{0x7F,0x09,0x09,0x09,0x06},
      {0x3E,0x41,0x51,0x21,0x5E},{0x7F,0x09,0x19,0x29,0x46},
      {0x46,0x49,0x49,0x49,0x31},{0x01,0x01,0x7F,0x01,0x01},
      {0x3F,0x40,0x40,0x40,0x3F},{0x1F,0x20,0x40,0x20,0x1F},
      {0x3F,0x40,0x38,0x40,0x3F},{0x63,0x14,0x08,0x14,0x63},
      {0x07,0x08,0x70,0x08,0x07},{0x61,0x51,0x49,0x45,0x43}
    };
    static const uint8_t dash[5]  = {0x08,0x08,0x08,0x08,0x08};
    static const uint8_t slash[5] = {0x20,0x10,0x08,0x04,0x02};
    static const uint8_t colon[5] = {0x00,0x36,0x36,0x00,0x00};
    static const uint8_t dot[5]   = {0x00,0x60,0x60,0x00,0x00};
    static const uint8_t bang[5]  = {0x00,0x00,0x5F,0x00,0x00};
    if (c >= 'a' && c <= 'z') c = char(c - 'a' + 'A');
    if (c == ' ') return font[0];
    if (c >= '0' && c <= '9') return font[1 + c - '0'];
    if (c >= 'A' && c <= 'Z') return font[11 + c - 'A'];
    if (c == '-') return dash;
    if (c == '/') return slash;
    if (c == ':') return colon;
    if (c == '.') return dot;
    if (c == '!') return bang;
    return font[0];
  }

  void drawCharacter(uint8_t x, uint8_t row, char c) {
    const uint8_t *shape = glyph(c);
    const uint16_t base = uint16_t(row) * 128 + x;
    for (uint8_t column = 0; column < 5; ++column) frame[base + column] = shape[column];
  }
};


// ===== Accelerometer.h =====
#include <Arduino.h>

// Supports ADXL345, BMA150, and BMX160 accelerometers.
// Both use the separate ADC-sensed, open-drain bus on A2/A3.
template<typename Bus>
class Adxl345 {
 public:
  explicit Adxl345(Bus &b):bus(b){}
  bool ready=false,hasSample=false;
  uint8_t address=0;
  float x=0,y=0,z=0;
  uint32_t sampledAt=0;
  bool begin() {
    ready=false;hasSample=false;address=0;
    const uint8_t candidates[]={0x53,0x1d};
    for(uint8_t a:candidates) {
      address=a;uint8_t id=0;
      if(readBytes(0x00,&id,1) && id==0xe5)break;
      address=0;
    }
    if(!address)return false;
    // Standby, full-resolution +/-4g, 25 Hz, FIFO bypass, data-ready enabled.
    // INT pins are not connected; INT_SOURCE is polled.
    if(!write8(0x2d,0) || !write8(0x31,0x09) || !write8(0x2c,0x08)
       || !write8(0x38,0) || !write8(0x2e,0x80) || !write8(0x2d,0x08))return false;
    uint8_t format=0,rate=0,power=0;
    if(!readBytes(0x31,&format,1)||!readBytes(0x2c,&rate,1)||!readBytes(0x2d,&power,1)
       || format!=0x09 || rate!=0x08 || !(power&0x08))return false;
    ready=true;lastPoll=millis();lastData=lastPoll;return true;
  }
  // Returns true only for a new sample, not each time loop() runs.
  bool poll(uint32_t now) {
    if(!ready || uint32_t(now-lastPoll)<40)return false;
    lastPoll=now;uint8_t source=0,data[6];
    if(!readBytes(0x30,&source,1)){fail();return false;}
    if(!(source&0x80)) {
      if(uint32_t(now-lastData)>500)fail();
      return false;
    }
    if(!readBytes(0x32,data,6)){fail();return false;}
    x=decode(data)*0.0039f;y=decode(data+2)*0.0039f;z=decode(data+4)*0.0039f;
    sampledAt=millis();lastData=sampledAt;hasSample=true;return true;
  }
  bool fresh(uint32_t now)const{return ready && hasSample && uint32_t(now-sampledAt)<300;}
 private:
  Bus &bus;
  uint32_t lastPoll=0,lastData=0;
  void fail(){ready=false;hasSample=false;}
  static int16_t decode(const uint8_t *p) {
    uint16_t u=uint16_t(p[0])|(uint16_t(p[1])<<8);
    return u<32768?int16_t(u):int16_t(int32_t(u)-65536);
  }
  bool write8(uint8_t reg,uint8_t value){
    bus.beginTransmission(address);bus.write(reg);bus.write(value);return bus.endTransmission()==0;
  }
  bool readBytes(uint8_t reg,uint8_t *p,uint8_t n){
    bus.beginTransmission(address);bus.write(reg);
    if(bus.endTransmission(false)!=0)return false;
    if(bus.requestFrom(address,n)!=n){while(bus.available())bus.read();return false;}
    for(uint8_t i=0;i<n;++i)p[i]=uint8_t(bus.read());
    return true;
  }
};

// BMA150: fixed I2C address 0x38. CSB must be high and SDO is not exposed by
// the common breakout. The control byte is the register address shifted once.
template<typename Bus>
class Bma150 {
 public:
  explicit Bma150(Bus &b):bus(b){}
  bool ready=false,hasSample=false;
  uint8_t address=0;
  float x=0,y=0,z=0;
  uint32_t sampledAt=0;
  static constexpr uint8_t I2C_ADDRESS=0x38;

  bool begin() {
    ready=false;hasSample=false;address=0;
    address=I2C_ADDRESS;
    uint8_t id=0;
    if(!readBytes(0x00,&id,1) || id!=0x02){address=0;return false;}
    uint8_t settings=0;
    if(!readBytes(0x14,&settings,1)){address=0;return false;}
    // Keep the protected calibration bits 7..5; use +/-2g and the 25 Hz filter.
    settings&=0xe0;
    if(!write8(0x14,settings)){address=0;return false;}
    uint8_t check=0;
    if(!readBytes(0x14,&check,1) || (check&0x1f)!=0){address=0;return false;}
    ready=true;lastPoll=millis();lastData=lastPoll;return true;
  }

  bool poll(uint32_t now) {
    if(!ready || uint32_t(now-lastPoll)<40)return false;
    lastPoll=now;uint8_t data[6];
    if(!readBytes(0x02,data,6)){fail();return false;}
    x=decode(data)*0.00390625f;
    y=decode(data+2)*0.00390625f;
    z=decode(data+4)*0.00390625f;
    sampledAt=millis();lastData=sampledAt;hasSample=true;return true;
  }

  bool fresh(uint32_t now)const{return ready && hasSample && uint32_t(now-sampledAt)<300;}

 private:
  Bus &bus;
  uint32_t lastPoll=0,lastData=0;
  void fail(){ready=false;hasSample=false;}
  static int16_t decode(const uint8_t *p) {
    uint16_t raw=uint16_t((uint16_t(p[1])<<2)|(p[0]>>6));
    return raw<512?int16_t(raw):int16_t(int32_t(raw)-1024);
  }
  bool write8(uint8_t reg,uint8_t value){
    bus.beginTransmission(address);bus.write(uint8_t(reg<<1));bus.write(value);
    return bus.endTransmission()==0;
  }
  bool readBytes(uint8_t reg,uint8_t *p,uint8_t n){
    bus.beginTransmission(address);bus.write(uint8_t(reg<<1));
    // BMA150 requires a STOP before the read START, unlike ADXL345.
    if(bus.endTransmission(true)!=0)return false;
    if(bus.requestFrom(address,n)!=n){while(bus.available())bus.read();return false;}
    for(uint8_t i=0;i<n;++i)p[i]=uint8_t(bus.read());
    return true;
  }
};

// BMX160 uses the BMI160 primary I2C register map. The common 7-pin
// VCC/GND/SCL/SDA/CSB/INT1/INT2 breakout responds at fixed address 0x68.
template<typename Bus>
class Bmx160 {
 public:
  explicit Bmx160(Bus &b):bus(b){}
  bool ready=false,hasSample=false;
  uint8_t address=0;
  float x=0,y=0,z=0;
  uint32_t sampledAt=0;
  static constexpr uint8_t I2C_ADDRESS=0x68;

  bool begin() {
    ready=false;hasSample=false;address=0;
    address=I2C_ADDRESS;
    uint8_t id=0;
    if(!readBytes(0x00,&id,1) || id!=0xd8){address=0;return false;}
    // 100 Hz normal-mode accelerometer, +/-2g.
    if(!write8(0x40,0x28) || !write8(0x41,0x03)){address=0;return false;}
    if(!write8(0x7e,0x11)){address=0;return false;}
    delay(5);
    uint8_t pmu=0;
    if(!readBytes(0x03,&pmu,1) || ((pmu>>4)&3)!=1){address=0;return false;}
    ready=true;lastPoll=millis();lastData=lastPoll;return true;
  }

  bool poll(uint32_t now) {
    if(!ready || uint32_t(now-lastPoll)<40)return false;
    lastPoll=now;uint8_t data[6];
    if(!readBytes(0x12,data,6)){fail();return false;}
    x=decode(data)*0.00006103515625f;
    y=decode(data+2)*0.00006103515625f;
    z=decode(data+4)*0.00006103515625f;
    sampledAt=millis();lastData=sampledAt;hasSample=true;return true;
  }

  bool fresh(uint32_t now)const{return ready && hasSample && uint32_t(now-sampledAt)<300;}

 private:
  Bus &bus;
  uint32_t lastPoll=0,lastData=0;
  void fail(){ready=false;hasSample=false;}
  static int16_t decode(const uint8_t *p) {
    uint16_t u=uint16_t(p[0])|(uint16_t(p[1])<<8);
    return u<32768?int16_t(u):int16_t(int32_t(u)-65536);
  }
  bool write8(uint8_t reg,uint8_t value){
    bus.beginTransmission(address);bus.write(reg);bus.write(value);
    return bus.endTransmission()==0;
  }
  bool readBytes(uint8_t reg,uint8_t *p,uint8_t n){
    bus.beginTransmission(address);bus.write(reg);
    if(bus.endTransmission(true)!=0)return false;
    if(bus.requestFrom(address,n)!=n){while(bus.available())bus.read();return false;}
    for(uint8_t i=0;i<n;++i)p[i]=uint8_t(bus.read());
    return true;
  }
};

template<typename Bus>
class Accelerometer {
 public:
  enum Kind:uint8_t {NONE,ADXL345,BMA150,BMX160};
  explicit Accelerometer(Bus &b):adxl(b),bma(b),bmx(b){}
  bool ready=false,hasSample=false;
  uint8_t address=0;
  float x=0,y=0,z=0;
  uint32_t sampledAt=0;
  Kind kind=NONE;

  bool begin() {
    kind=NONE;
    if(adxl.begin())kind=ADXL345;
    else if(bma.begin())kind=BMA150;
    else if(bmx.begin())kind=BMX160;
    copyFromSource();
    return kind!=NONE;
  }
  bool poll(uint32_t now) {
    bool got=false;
    if(kind==ADXL345)got=adxl.poll(now);
    else if(kind==BMA150)got=bma.poll(now);
    else if(kind==BMX160)got=bmx.poll(now);
    copyFromSource();
    return got;
  }
  bool fresh(uint32_t now)const{
    return kind==ADXL345?adxl.fresh(now):kind==BMA150?bma.fresh(now):
      kind==BMX160?bmx.fresh(now):false;
  }

 private:
  Adxl345<Bus> adxl;
  Bma150<Bus> bma;
  Bmx160<Bus> bmx;
  void copyFromSource() {
    if(kind==ADXL345){
      ready=adxl.ready;hasSample=adxl.hasSample;address=adxl.address;
      x=adxl.x;y=adxl.y;z=adxl.z;sampledAt=adxl.sampledAt;
    }else if(kind==BMA150){
      ready=bma.ready;hasSample=bma.hasSample;address=bma.address;
      x=bma.x;y=bma.y;z=bma.z;sampledAt=bma.sampledAt;
    }else if(kind==BMX160){
      ready=bmx.ready;hasSample=bmx.hasSample;address=bmx.address;
      x=bmx.x;y=bmx.y;z=bmx.z;sampledAt=bmx.sampledAt;
    }else{
      ready=hasSample=false;address=0;sampledAt=0;
    }
  }
};


// ===== MovementMonitor.h =====
#include <stdint.h>
#include <math.h>

// Consistency with a chosen demonstration, not a diagnosis or form assessment.
// Uses host timestamps: it does not assume the custom bus delivers every 25 Hz sample.
namespace movement {
struct Vec {float x,y,z;};
inline float norm(Vec a){return sqrtf(a.x*a.x+a.y*a.y+a.z*a.z);}
inline Vec subtract(Vec a,Vec b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
inline float angle(Vec a,Vec b){
  float n=norm(a)*norm(b);if(n<0.01f)return 0;
  float c=(a.x*b.x+a.y*b.y+a.z*b.z)/n;
  if(c>1)c=1;
  if(c< -1)c= -1;
  return acosf(c)*57.2957795f;
}
enum Feedback:uint8_t {NO_SENSOR,SET_ENDPOINTS,HOLD_STILL,RECORD_REFERENCE,REFERENCE_SAVED,
  CONSISTENT,FASTER,SLOWER,MORE_ABRUPT,ENDPOINT_DIFFERENT,DATA_GAP,PAUSE_AT_ENDPOINT};

class Monitor {
 public:
  bool startSaved=false,targetSaved=false;
  uint32_t referenceMs=0,lastRepMs=0;
  float referenceAbruptness=0,lastAbruptness=0;
  uint16_t comparedReps=0,flaggedReps=0;
  static constexpr uint16_t MAX_SAMPLE_GAP=250;
  void reset(){*this=Monitor();}
  void clearEndpoints(){startSaved=false;targetSaved=false;resetRun();}
  void resetRun(){
    referenceMs=0;lastRepMs=0;referenceAbruptness=0;lastAbruptness=0;
    comparedReps=0;flaggedReps=0;active=false;lastFeedback=RECORD_REFERENCE;noticeActive=false;
  }
  void cancelCycle(){active=false;}
  void invalidateSample(){
    haveSample=false;stable=false;stillTracking=false;
    if(active)gap=true;
  }
  void ingest(uint32_t now,float x,float y,float z){
    Vec v{x,y,z};float magnitude=norm(v);
    bool sane=isfinite(x)&&isfinite(y)&&isfinite(z)&&magnitude>0.15f&&magnitude<6.8f
      &&fabsf(x)<3.95f&&fabsf(y)<3.95f&&fabsf(z)<3.95f;
    if(!sane){invalidateSample();return;}
    uint32_t dt=haveSample?uint32_t(now-sampleAt):0;
    bool continuous=haveSample && dt>0 && dt<=MAX_SAMPLE_GAP;
    float step=continuous?norm(subtract(v,previous)):0;
    if(!continuous){filtered=v;stable=false;stillTracking=false;if(active)gap=true;}
    else {
      float alpha=float(dt)/(200.0f+dt);
      filtered={filtered.x+alpha*(x-filtered.x),filtered.y+alpha*(y-filtered.y),filtered.z+alpha*(z-filtered.z)};
      bool quiet=step<0.07f && fabsf(magnitude-1.0f)<0.12f;
      if(quiet){
        if(!stillTracking||norm(subtract(v,stillAnchor))>0.04f){stillTracking=true;stillSince=now;stillAnchor=v;}
        stable=uint32_t(now-stillSince)>=350;
      }
      else {stable=false;stillTracking=false;}
      if(active){float seconds=dt/1000.0f;float rate=step/seconds;
        weightedStepSquared+=rate*rate*seconds;coveredMs+=dt;++samples;}
    }
    sampleAt=now;haveSample=true;previous=v;
  }
  bool fresh(uint32_t now)const{return haveSample && uint32_t(now-sampleAt)<300;}
  bool captureReady(uint32_t now)const{return fresh(now)&&stable;}
  void askStill(uint32_t now){noticeActive=true;noticeAt=now;}
  bool captureStart(uint32_t now){
    if(!captureReady(now)){askStill(now);return false;}
    start=filtered;startSaved=true;targetSaved=false;resetRun();return true;
  }
  bool captureTarget(uint32_t now){
    if(!captureReady(now)){askStill(now);return false;}
    target=filtered;targetSaved=true;resetRun();return true;
  }
  bool tiltValid(uint32_t now)const{return startSaved&&captureReady(now);}
  float tilt()const{return angle(start,filtered);}
  float homeError()const{return startSaved?angle(start,filtered):180;}
  float targetError()const{return targetSaved?angle(target,filtered):180;}
  float span()const{return startSaved&&targetSaved?angle(start,target):0;}
  float prospectiveSpan()const{return startSaved?angle(start,filtered):0;}
  bool endpointsReady()const{return startSaved&&targetSaved;}
  void beginCycle(uint32_t now){
    active=true;began=now;samples=0;coveredMs=0;weightedStepSquared=0;
    gap=!fresh(now);endpointDifferent=false;endpointUnclear=false;
  }
  void endpoint(bool atTarget,uint32_t now){
    if(!active)return;
    if(!captureReady(now)){endpointUnclear=true;return;}
    if(endpointsReady()&&angle(atTarget?target:start,filtered)>15.0f)endpointDifferent=true;
  }
  void finishCycle(uint32_t now){
    if(!active)return;
    active=false;lastRepMs=uint32_t(now-began);
    if(!endpointsReady()){lastFeedback=SET_ENDPOINTS;return;}
    if(gap||!fresh(now)||samples<8||lastRepMs<800||lastRepMs>30000
       ||coveredMs*10UL<lastRepMs*8UL){lastFeedback=DATA_GAP;return;}
    if(endpointUnclear){lastFeedback=PAUSE_AT_ENDPOINT;return;}
    lastAbruptness=sqrtf(weightedStepSquared/(coveredMs/1000.0f));
    if(endpointDifferent){lastFeedback=ENDPOINT_DIFFERENT;if(referenceMs){++comparedReps;++flaggedReps;}return;}
    if(referenceMs==0){referenceMs=lastRepMs;referenceAbruptness=lastAbruptness;lastFeedback=REFERENCE_SAVED;return;}
    ++comparedReps;
    if(lastRepMs*10UL<referenceMs*7UL)lastFeedback=FASTER;
    else if(lastRepMs*10UL>referenceMs*13UL)lastFeedback=SLOWER;
    else if(lastAbruptness>referenceAbruptness*1.8f+0.5f)lastFeedback=MORE_ABRUPT;
    else lastFeedback=CONSISTENT;
    if(lastFeedback!=CONSISTENT)++flaggedReps;
  }
  Feedback feedback(uint32_t now,bool sensorFresh)const{
    if(!sensorFresh||!fresh(now))return NO_SENSOR;
    if(noticeActive&&uint32_t(now-noticeAt)<1800)return HOLD_STILL;
    if(!endpointsReady())return SET_ENDPOINTS;
    if(!referenceMs && (lastFeedback==RECORD_REFERENCE||lastFeedback==SET_ENDPOINTS))return RECORD_REFERENCE;
    return lastFeedback;
  }
  static const char *shortText(Feedback f){
    switch(f){
      case NO_SENSOR:return "ACCEL DATA MISSING";
      case SET_ENDPOINTS:return "SAVE TILT ENDPOINTS";
      case HOLD_STILL:return "HOLD STILL THEN TAP";
      case RECORD_REFERENCE:return "FIRST REP: REFERENCE";
      case REFERENCE_SAVED:return "REFERENCE SAVED";
      case CONSISTENT:return "SIMILAR TO REFERENCE";
      case FASTER:return "FASTER THAN REFERENCE";
      case SLOWER:return "SLOWER THAN REFERENCE";
      case MORE_ABRUPT:return "MORE ABRUPT MOVEMENT";
      case ENDPOINT_DIFFERENT:return "TILT DIFFERS AT END";
      case DATA_GAP:return "REP DATA INCOMPLETE";
      default:return "PAUSE AT ENDPOINTS";
    }
  }
 private:
  Vec previous{0,0,1},filtered{0,0,1},start{0,0,1},target{0,0,1},stillAnchor{0,0,1};
  bool haveSample=false,stable=false,stillTracking=false,noticeActive=false;
  bool active=false,gap=false,endpointDifferent=false,endpointUnclear=false;
  uint32_t sampleAt=0,stillSince=0,noticeAt=0,began=0,coveredMs=0;
  uint16_t samples=0;
  float weightedStepSquared=0;
  Feedback lastFeedback=RECORD_REFERENCE;
};
}



// ===== Application: UNO R4 Minima only; one OLED; no ultrasonic sensor =====
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
constexpr uint8_t TOUCH_PIN=5,REP_GOAL=10;
static_assert(REP_GOAL>0&&REP_GOAL<=100,"REP_GOAL must be 1..100");
AnalogI2C heartBus(A0,A1),accelBus(A2,A3),displayBus(A4,A5);
HeartSensor heart(heartBus);
Accelerometer<AnalogI2C> accel(accelBus);
OledDisplay oled(displayBus);
movement::Monitor motion;
workout::Engine exercise;
workout::PageCycle pages;
bool heartBusReady=false,accelBusReady=false;
uint32_t lastHeartRetry=0,lastAccelRetry=0,lastDisplayRetry=0,lastDisplay=0,lastPacket=0;
bool rawTouch=false,stableTouch=false,holdHandled=false;
uint32_t rawChanged=0,touchStarted=0;
char outgoing[1800]={};size_t outgoingLength=0,outgoingSent=0;
uint32_t sequence=0;

// Avoid depending on printf's optional floating-point support in embedded libc.
void decimalText(char *out,size_t capacity,float value,uint8_t places){
 if(!isfinite(value)){snprintf(out,capacity,"null");return;}
 unsigned long scale=places==3?1000UL:places==2?100UL:10UL;
 unsigned long n=static_cast<unsigned long>(lroundf(fabsf(value)*scale));
 snprintf(out,capacity,"%s%lu.%0*lu",value<0?"-":"",n/scale,int(places),n%scale);
}

const char *stateName(uint8_t s){
 const char *names[]={"IDLE","SAVE START","SAVE TARGET","READY","RUNNING","PAUSED","DONE"};
 return s<7?names[s]:"UNKNOWN";
}
const char *prompt(uint32_t now){
 using namespace workout;
 Feedback f=exercise.feedback(now);
 if(f==CAL_TOO_SMALL)return "NEED 25 DEG CHANGE";
 if(f==HOLD_STILL)return "HOLD STILL THEN TAP";
 if(f==MOVE_TIMEOUT)return "RESTART AT HOME";
 switch(exercise.state){
  case IDLE:return "TOUCH TO SET UP";
  case CAL_START:return "HOLD HOME THEN TAP";
  case CAL_TARGET:return "HOLD TARGET THEN TAP";
  case READY:return "TOUCH TO START";
  case PAUSED:return "TOUCH TO RESUME";
  case DONE:return "TOUCH FOR NEXT SET";
  default:return f==NO_ACCEL?"CHECK ACCELEROMETER":f==FIND_START?"GO TO HOME POSE":
    f==REACH_TARGET?"REACH TARGET POSE":"RETURN TO HOME POSE";
 }
}
uint8_t shownPage(){return exercise.state==workout::CAL_START||exercise.state==workout::CAL_TARGET?1:pages.page;}
void resetSession(){exercise.reset();motion.reset();}
void sessionTap(uint32_t now){
 using namespace workout;
 switch(exercise.state){
  case IDLE:motion.reset();exercise.state=CAL_START;break;
  case CAL_START:
   if(!accel.fresh(now)||!motion.captureStart(now)){exercise.notify(HOLD_STILL,now);break;}
   exercise.notice=NONE;exercise.state=CAL_TARGET;break;
  case CAL_TARGET:
   if(!accel.fresh(now)||!motion.captureReady(now)){exercise.notify(HOLD_STILL,now);break;}
   if(motion.prospectiveSpan()<Engine::MIN_SPAN_DEG){exercise.notify(CAL_TOO_SMALL,now);break;}
   if(motion.captureTarget(now)){exercise.notice=NONE;exercise.state=READY;}break;
  case READY:motion.resetRun();exercise.start(now);break;
  case RUNNING:exercise.pause(now);motion.cancelCycle();break;
  case PAUSED:motion.cancelCycle();exercise.resume(now);break;
  case DONE:exercise.reps=0;exercise.accumulatedMs=0;exercise.state=READY;break;
 }
}
void updateTouch(uint32_t now){
 bool reading=digitalRead(TOUCH_PIN)==HIGH;
 if(reading!=rawTouch){rawTouch=reading;rawChanged=now;}
 if(rawTouch!=stableTouch&&uint32_t(now-rawChanged)>=35){
  stableTouch=rawTouch;
  if(stableTouch){touchStarted=now;holdHandled=false;}
  else if(!holdHandled)sessionTap(now);
 }
 if(stableTouch&&!holdHandled&&uint32_t(now-touchStarted)>=2000){resetSession();holdHandled=true;}
}
void serialCommands(uint32_t now){
 static char input[32];static uint8_t length=0;static bool dropping=false;
 uint8_t budget=64;
 while(Serial.available()&&budget--){
  char c=char(Serial.read());if(c=='\r')continue;
  if(c=='\n'){
   if(!dropping){input[length]=0;
    if(strcmp(input,"tap")==0)sessionTap(now);
    else if(strcmp(input,"reset")==0)resetSession();
    else if(strcmp(input,"page")==0)pages.next(now);
    else if(strncmp(input,"goal=",5)==0&&exercise.state!=workout::RUNNING&&exercise.state!=workout::PAUSED){
     char *end=nullptr;long goal=strtol(input+5,&end,10);
     if(end!=input+5&&*end==0&&goal>=1&&goal<=100){exercise.goal=uint8_t(goal);exercise.reps=0;}
    }
   }
   length=0;dropping=false;
  }else if(!dropping){if(length<sizeof(input)-1)input[length++]=c;else dropping=true;}
 }
}
void updateDisplay(uint32_t now){
 if(!oled.found()||oled.busy()||uint32_t(now-lastDisplay)<700)return;
 lastDisplay=now;oled.clear();char text[24];
 uint8_t page=shownPage();
 if(page==0){
  oled.line(0,"HEART RATE       1/3");oled.line(1,"PROTOTYPE ESTIMATE");
  float bpm=heart.bpm(now);
  if(bpm>0)snprintf(text,sizeof(text),"%ld BPM",lroundf(bpm));else snprintf(text,sizeof(text),"-- BPM");
  oled.bigLine(2,text);
  oled.line(4,heart.ready?(bpm>0?"PULSE ACQUIRED":"HOLD FINGER STILL"):"CHECK HEART SENSOR");
  oled.line(5,accel.fresh(now)&&!motion.captureReady(now)?"MOVEMENT MAY AFFECT HR":"KEEP LIGHT CONTACT");
  snprintf(text,sizeof(text),"IR %lu",(unsigned long)heart.rawIR);oled.line(6,text);
 }else if(page==1){
  oled.line(0,"REPETITIONS      2/3");oled.line(1,stateName(exercise.state));
  snprintf(text,sizeof(text),"%u/%u",exercise.reps,exercise.goal);oled.bigLine(2,text);
  oled.line(4,prompt(now));
  if(motion.endpointsReady())snprintf(text,sizeof(text),"SPAN %ld DEG",lroundf(motion.span()));
  else snprintf(text,sizeof(text),"SAVE TWO STILL POSES");
  oled.line(5,text);snprintf(text,sizeof(text),"TIME %lu S",(unsigned long)(exercise.elapsedMs(now)/1000));oled.line(6,text);
 }else{
  oled.line(0,"MOVEMENT         3/3");
  oled.line(1,movement::Monitor::shortText(motion.feedback(now,accel.fresh(now))));
  if(accel.fresh(now)&&motion.tiltValid(now))snprintf(text,sizeof(text),"%ld DEG",lroundf(motion.tilt()));
  else snprintf(text,sizeof(text),"-- DEG");
  oled.bigLine(2,text);
  oled.line(4,accel.fresh(now)?"TILT SHOWN WHEN STILL":"CHECK ACCELEROMETER");
  if(motion.lastRepMs)snprintf(text,sizeof(text),"LAST REP %lu.%lu S",(unsigned long)(motion.lastRepMs/1000),(unsigned long)(motion.lastRepMs%1000/100));
  else snprintf(text,sizeof(text),"FIRST REP: REFERENCE");
  oled.line(5,text);
  snprintf(text,sizeof(text),"REFERENCE %lu.%lu S",(unsigned long)(motion.referenceMs/1000),(unsigned long)(motion.referenceMs%1000/100));oled.line(6,text);
 }
 oled.line(7,exercise.state==workout::RUNNING?"TAP PAUSE / HOLD RESET":"TAP ACTION / HOLD RESET");
 oled.startUpdate();
}
void queueReadings(uint32_t now){
 if(uint32_t(now-lastPacket)<750||outgoingSent<outgoingLength||!Serial)return;
 lastPacket=now;
 char bpm[16]="null",ax[16]="null",ay[16]="null",az[16]="null",tilt[16]="null",span[16]="null";
 bool fresh=accel.fresh(now)&&motion.fresh(now);
 if(fresh){decimalText(ax,sizeof(ax),accel.x,3);decimalText(ay,sizeof(ay),accel.y,3);
   decimalText(az,sizeof(az),accel.z,3);
   if(motion.tiltValid(now))decimalText(tilt,sizeof(tilt),motion.tilt(),1);}
 if(motion.endpointsReady())decimalText(span,sizeof(span),motion.span(),1);
 float pulse=heart.bpm(now);if(pulse>0)decimalText(bpm,sizeof(bpm),pulse,1);
 int n=snprintf(outgoing,sizeof(outgoing),
  "{\"kind\":\"workout\",\"protocol\":2,\"seq\":%lu,\"uptime_ms\":%lu,\"state\":%u,\"feedback\":%u,"
  "\"reps\":%u,\"goal\":%u,\"seconds\":%lu,\"page\":%u,\"bpm\":%s,\"heart_status\":\"%s\","
  "\"chip\":\"%s\",\"ir\":%lu,\"overflows\":%lu,\"heart_found\":%s,\"pulse_motion_warning\":%s,"
  "\"display_found\":%s,\"display_address\":%u,\"page_period_ms\":4000,"
  "\"heart_bus_adc\":[%u,%u],\"display_bus_adc\":[%u,%u],"
  "\"motion\":{\"found\":%s,\"fresh\":%s,\"address\":%u,\"feedback\":%u,"
  "\"x_g\":%s,\"y_g\":%s,\"z_g\":%s,\"tilt_deg\":%s,\"span_deg\":%s,"
  "\"reference_ms\":%lu,\"last_rep_ms\":%lu,\"compared_reps\":%u,\"flagged_reps\":%u,"
  "\"bus_sda_adc\":%u,\"bus_scl_adc\":%u}}\n",
  (unsigned long)++sequence,(unsigned long)now,exercise.state,exercise.feedback(now),exercise.reps,exercise.goal,
  (unsigned long)(exercise.elapsedMs(now)/1000),shownPage(),bpm,heartBusReady?heart.status(now):"Heart bus unavailable",
  heart.chipName(),(unsigned long)heart.rawIR,(unsigned long)heart.overflows,heart.ready?"true":"false",
  fresh&&!motion.captureReady(now)?"true":"false",oled.found()?"true":"false",oled.i2cAddress(),
  heartBus.idleSda,heartBus.idleScl,displayBus.idleSda,displayBus.idleScl,
  accel.ready?"true":"false",fresh?"true":"false",accel.address,motion.feedback(now,fresh),ax,ay,az,tilt,span,
  (unsigned long)motion.referenceMs,(unsigned long)motion.lastRepMs,motion.comparedReps,motion.flaggedReps,
  accelBus.idleSda,accelBus.idleScl);
 if(n>0&&size_t(n)<sizeof(outgoing)){outgoingLength=size_t(n);outgoingSent=0;}
 else {outgoingLength=outgoingSent=0;}
}
void drainReadings(){
 if(!Serial){outgoingLength=outgoingSent=0;return;}
 if(outgoingSent>=outgoingLength)return;
 int room=Serial.availableForWrite();if(room<=0)return;
 size_t n=outgoingLength-outgoingSent;if(n>64)n=64;if(n>size_t(room))n=size_t(room);
 outgoingSent+=Serial.write(reinterpret_cast<const uint8_t*>(outgoing+outgoingSent),n);
}
void setup(){
 Serial.begin(115200);pinMode(TOUCH_PIN,INPUT);analogReadResolution(12);
 exercise.goal=REP_GOAL;
 heartBusReady=heartBus.begin();if(heartBusReady)heart.begin();
 accelBusReady=accelBus.begin();if(accelBusReady)accel.begin();
 oled.begin();
}
void loop(){
 heart.poll(millis());
 uint32_t now=millis();
 if(accel.poll(now))motion.ingest(accel.sampledAt,accel.x,accel.y,accel.z);
 now=millis();if(!accel.fresh(now))motion.invalidateSample();
 updateTouch(now);serialCommands(now);pages.update(now);
 workout::Phase oldPhase=exercise.phase;uint8_t oldReps=exercise.reps;workout::State oldState=exercise.state;
 exercise.update(now,accel.fresh(now)&&motion.fresh(now),motion.captureReady(now),motion.homeError(),motion.targetError(),motion.span());
 if(oldState==workout::RUNNING){
  if(oldPhase==workout::SEEK_START&&exercise.phase==workout::SEEK_TARGET){motion.beginCycle(now);motion.endpoint(false,now);}
  if(oldPhase==workout::SEEK_TARGET&&exercise.phase==workout::SEEK_RETURN)motion.endpoint(true,now);
  if(exercise.reps>oldReps){motion.endpoint(false,now);motion.finishCycle(now);}
  if(oldPhase==workout::SEEK_RETURN&&exercise.phase==workout::SEEK_TARGET&&exercise.state==workout::RUNNING){motion.beginCycle(now);motion.endpoint(false,now);}
  if(exercise.phase==workout::SEEK_START&&oldPhase!=workout::SEEK_START)motion.cancelCycle();
 }
 updateDisplay(now);oled.tick();heart.poll(millis());now=millis();
 if(!heart.ready&&uint32_t(now-lastHeartRetry)>=5000){lastHeartRetry=now;heartBusReady=heartBus.begin();if(heartBusReady)heart.begin();}
 if(!accel.ready&&uint32_t(now-lastAccelRetry)>=5000){lastAccelRetry=now;accelBusReady=accelBus.begin();if(accelBusReady)accel.begin();}
 if(!oled.found()&&uint32_t(now-lastDisplayRetry)>=5000){lastDisplayRetry=now;oled.begin();}
 queueReadings(millis());drainReadings();
}
