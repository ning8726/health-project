#include "Arduino.h"
#include <assert.h>
#include <iostream>
#include <limits>
uint64_t mockMicros=0;
MockSerial Serial;
#include "../R4_Health/R4_Health.ino"

using workout::Engine;
struct MockBmaBus {
 uint8_t target=0,reg=0,tx[8]={},txCount=0,rx[16]={},rxPos=0,rxCount=0;
 uint8_t regs[32]={};
 void beginTransmission(uint8_t a){target=a;txCount=0;}
 void write(uint8_t b){if(txCount<sizeof(tx))tx[txCount++]=b;}
 uint8_t endTransmission(bool stop=true){
   (void)stop;
   if(target!=0x38||!txCount)return 2;
   reg=tx[0]>>1;
   if(txCount>1)regs[tx[0]>>1]=tx[1];
   return 0;
 }
 uint8_t requestFrom(uint8_t a,uint8_t n){
   if(a!=0x38||n>sizeof(rx))return 0;
   rxCount=n;rxPos=0;
   for(uint8_t i=0;i<n;++i)rx[i]=regs[(uint8_t(reg+i))&31];
   return n;
 }
 int available(){return rxCount-rxPos;}
 int read(){return rxPos<rxCount?rx[rxPos++]:-1;}
};
void holdPose(Engine &e,uint32_t start,uint32_t duration,bool target){
 for(uint32_t t=0;t<=duration;t+=40)e.update(start+t,true,true,target?90:0,target?0:90,90);
}
void completeRep(Engine &e,uint32_t start){
 holdPose(e,start,640,false);holdPose(e,start+900,640,true);holdPose(e,start+1800,640,false);
}

int main(int argc,char **argv){
 if(argc>1&&std::string(argv[1])=="--telemetry"){
  setup();for(int i=0;i<700;++i){mockMicros+=5000;loop();}
  std::cout<<Serial.output;return 0;
 }
 Engine e;e.start(0);completeRep(e,0);assert(e.reps==1);
 e.reset();e.start(0);holdPose(e,0,640,false);holdPose(e,900,640,true);
 assert(e.reps==0);holdPose(e,1800,640,false);assert(e.reps==1);
 e.reset();e.start(0);holdPose(e,0,640,false);holdPose(e,900,240,true);
 holdPose(e,1400,1000,false);assert(e.reps==0);
 e.reset();e.start(0);holdPose(e,0,640,false);holdPose(e,900,640,true);
 e.update(2500,false,false,0,0,90);assert(e.phase==workout::SEEK_START);
 holdPose(e,3000,640,true);assert(e.reps==0);
 e.reset();e.start(0);holdPose(e,0,640,false);holdPose(e,900,640,true);
 e.pause(1700);e.resume(9000);holdPose(e,9000,640,false);assert(e.reps==0);
 assert(e.elapsedMs(10000)==2700);
 e.reset();e.start(0);holdPose(e,0,640,false);e.update(32000,true,true,0,90,90);
 assert(e.reps==0&&e.phase==workout::SEEK_START);
 e.reset();e.goal=1;e.start(0);completeRep(e,0);assert(e.state==workout::DONE&&e.reps==1);
 completeRep(e,4000);assert(e.reps==1);
 e.reset();e.start(0);
 for(uint32_t t=0;t<10000;t+=40)e.update(t,true,true,0,10,10);
 assert(e.reps==0&&e.phase==workout::SEEK_START);
 e.update(10000,true,true,NAN,0,90);assert(e.reps==0);
 workout::PageCycle pc;pc.update(3999);assert(pc.page==0);pc.update(4000);assert(pc.page==1);
 pc.update(8000);assert(pc.page==2);pc.update(12000);assert(pc.page==0);
 pc.since=UINT32_MAX-2000;pc.page=0;pc.update(1000);assert(pc.page==0);
 pc.update(2100);assert(pc.page==1);
 movement::Monitor m;
 for(uint32_t t=0;t<=1000;t+=40)m.ingest(t,0,0,1);
 assert(m.captureStart(1000));
 for(uint32_t t=1040;t<=2200;t+=40)m.ingest(t,1,0,0);
 assert(m.captureReady(2200)&&m.prospectiveSpan()>85);
 assert(m.captureTarget(2200)&&m.span()>85&&m.span()<95);
 assert(!m.fresh(2600));
 MockBmaBus bmaBus;
 bmaBus.regs[0x00]=0x02;bmaBus.regs[0x14]=0xe0;
 bmaBus.regs[0x02]=0x00;bmaBus.regs[0x03]=0x40;
 bmaBus.regs[0x04]=0x00;bmaBus.regs[0x05]=0xc0;
 bmaBus.regs[0x06]=0x00;bmaBus.regs[0x07]=0x00;
 Bma150<MockBmaBus> bma(bmaBus);
 bool bmaReady=bma.begin();
 if(!bmaReady||bma.address!=0x38||(bmaBus.regs[0x14]&0x1f)!=0)
   std::cerr<<"BMA begin: ready="<<bmaReady<<" address="<<unsigned(bma.address)
            <<" config="<<unsigned(bmaBus.regs[0x14])<<"\n";
 assert(bmaReady&&bma.address==0x38&&(bmaBus.regs[0x14]&0x1f)==0);
 mockMicros+=50000;assert(bma.poll(millis()));
 assert(bma.x>0.99f&&bma.x<1.01f);
 assert(bma.y< -0.99f&&bma.y> -1.01f);
 assert(bma.z> -0.01f&&bma.z<0.01f);
 oled.bigLine(6,"123456789012345678901234567890");
 oled.bigLine(7,"OUTSIDE SCREEN");
 char text[24];decimalText(text,sizeof(text),-1.234f,3);assert(std::string(text)=="-1.234");
 decimalText(text,sizeof(text),78.25f,1);assert(std::string(text)=="78.3");
 decimalText(text,sizeof(text),NAN,1);assert(std::string(text)=="null");
 std::cout<<"PASS: complete/partial reps, dwell, data loss, pause, timeout, goal, pose span, "
             "page cycling and rollover, pose calibration, BMA150 decoding, "
             "framebuffer bounds, decimal encoding.\n";
}
