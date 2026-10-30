#include <assert.h>
#include <stdio.h>
#include "device/dac_sequence.h"
static unsigned writes, channel_a, channel_b, mono;
static bool enabled, muted, amp, power_b, reset;
static unsigned sequence_index;
static uint32_t sequence_wait;
static bool checking_power;
static const struct { DacPin p; bool high; } expected[] = {
 {DAC_PIN_MUTE,true}, {DAC_PIN_ENABLE_A,true}, {DAC_PIN_AMP,true},
 {DAC_PIN_RESET,true}, {DAC_PIN_AMP,false}, {DAC_PIN_ENABLE_B,true},
 {DAC_PIN_RESET,false}, {DAC_PIN_AMP,true}
};
static bool write_reg(void *c,uint8_t addr,uint32_t map,uint8_t value) {
 (void)c;assert(addr==0x66 && map!=0);writes++;
 if(map==0x05000A) {assert(value==7);channel_a++;}
 if(map==0x05000B) {assert(value==15);channel_b++;}
 if(map>=0x180009 && map<=0x180040)mono++;
 return true;
}
static bool read_reg(void *c,uint8_t addr,uint32_t map,uint8_t *value) {
 (void)c;assert(addr==0x66);*value=map==0x020000?0xF6:map==0x010000?0x43:0;return true;
}
static void pin(void *c,DacPin p,bool high) {
 (void)c;
 if(checking_power) {
  assert(sequence_index < sizeof expected/sizeof expected[0]);
  assert(p==expected[sequence_index].p && high==expected[sequence_index].high);
  if(p==DAC_PIN_RESET && !high) assert(sequence_wait>=11000);
  sequence_index++;
 }
 if(p==DAC_PIN_RESET)reset=high;
 if(p==DAC_PIN_ENABLE_B)power_b=high;
 if(p==DAC_PIN_ENABLE_A)enabled=high;
 if(p==DAC_PIN_MUTE)muted=high;
 if(p==DAC_PIN_AMP)amp=high;
}
static void delay_us(void *c,uint32_t us) {(void)c;sequence_wait+=us;}
int main(void) {
 DacBus bus={write_reg,read_reg,pin,delay_us,NULL};
 checking_power=true;
 dac_power_pins(&bus);assert(enabled && muted && amp && power_b && !reset);
 assert(sequence_index==sizeof expected/sizeof expected[0] && sequence_wait>=31000);
 checking_power=false;
 assert(dac_probe(&bus));assert(dac_configure(&bus,1)==DAC_READY);
 dac_output_on(&bus,150);assert(enabled && !muted && amp);
 assert(channel_a && channel_b && mono==0 && writes>20);
 dac_set_muted(&bus,true,255);assert(muted);
 dac_output_off(&bus);assert(muted && !amp && reset && !power_b);
 puts("Neo DAC: one 0x66 stereo chip, phase mapping, power and mute PASS");return 0;
}
