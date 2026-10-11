#include <stdio.h>
#include <string.h>
#include <setjmp.h>
#include "mock.h"
#define __attribute__(x)
#pragma pack(push,1)
#include "../../src/io4.h"
#pragma pack(pop)
#include "../../src/air.c"
#include "../../src/led_driver.c"
#include "../../src/controller_control.c"
#include "../../src/io4.c"
uint64_t mock_now;
uint16_t mock_samples[6];
uint8_t mock_report[63];
unsigned mock_reports,mock_selected,mock_phase,mock_flash_calls;
bool mock_mounted=true,mock_ready=true,mock_rebooted,mock_save_ok=true;
mock_watchdog_t mock_watchdog;
struct mock_pio mock_pio_instance;
static bool pins[32];
static uint16_t saved_baselines[6];
static jmp_buf reboot_jump;
void gpio_put(unsigned p,bool b) {pins[p]=b;}
uint16_t adc_read(void) {
    assert(mock_selected==2||mock_selected==3);
    const unsigned left[3]={8,7,6},right[3]={15,26,27};
    unsigned phase=3;
    for(unsigned i=0;i<3;i++) if(pins[left[i]]||pins[right[i]]) {assert(pins[left[i]]&&pins[right[i]]&&phase==3);phase=i;}
    assert(phase<3);
    return mock_samples[2*phase+(mock_selected==3?0:1)];
}
bool tud_hid_n_report(unsigned n,uint8_t id,const void *data,uint16_t size) {assert(n==1&&id==1&&size==63);memcpy(mock_report,data,63);mock_reports++;return true;}
bool calibration_save(const uint16_t b[6]) {mock_flash_calls++;memcpy(saved_baselines,b,12);return mock_save_ok;}
void watchdog_reboot(uint32_t pc,uint32_t sp,uint32_t delay) {assert(pc==0&&sp==0&&delay==10);longjmp(reboot_jump,1);}
static void sweep(void) {for(unsigned i=0;i<3;i++)air_sensor_update();mock_now+=1000;calibration_and_snapshot_task();}
static void command_test(uint8_t op,uint16_t seq,uint8_t value) {uint8_t b[63]={1,op};put16(b+2,seq);b[4]=value;controller_control_receive(1,b,63);}
static void emit_frame(void) {state=LED_LATCHING;led_driver_task();}
int main(void) {
    uint16_t initial[6]={3800,3700,3600,3500,3400,3300};memcpy(mock_samples,initial,12);
    air_sensor_init();air_sensor_set_baselines(initial);sweep();controller_control_init(true);
    air_snapshot_t a;air_sensor_get_snapshot(&a);
    assert(a.trigger[0]==3344&&a.release[0]==3390&&a.trigger_percent==12&&a.hysteresis==90);
    // Boundary and hysteresis, with actual IO4 report mapping.
    mock_samples[0]=3344;sweep();assert(air_sensor_blocked_bitmap()==1);
    mock_samples[0]=3390;sweep();assert(air_sensor_blocked_bitmap()==1);
    mock_samples[0]=3391;sweep();assert(air_sensor_blocked_bitmap()==0);
    const uint16_t map[6][2]={{0,1<<12},{1<<11,0},{0,1<<11},{1<<12,0},{0,1<<13},{1<<13,0}};
    io4_state_t io;io4_input_report_t report;io4_init(&io);
    for(unsigned i=0;i<6;i++){memcpy(mock_samples,initial,12);mock_samples[i]=0;sweep();io4_set_air_blocked(&io,air_sensor_blocked_bitmap());io4_build_input_report(&io,&report);assert(report.buttons[0]==((7u<<11)&~map[i][0]));assert(report.buttons[1]==((7u<<11)&~map[i][1]));}
    // Trigger requests apply on core 1; invalid values leave the setting unchanged.
    command_test(5,100,20);assert(ack_result==0);sweep();
    air_sensor_get_snapshot(&a);assert(a.trigger_percent==20&&a.trigger[0]==3040&&a.release[0]==3116);
    memcpy(mock_samples,initial,12);mock_samples[0]=3040;sweep();assert(air_sensor_blocked_bitmap()==1);
    mock_samples[0]=3116;sweep();assert(air_sensor_blocked_bitmap()==1);
    mock_samples[0]=3117;sweep();assert(air_sensor_blocked_bitmap()==0);
    command_test(5,101,0);assert(ack_result==1);command_test(5,102,100);assert(ack_result==1);
    sweep();air_sensor_get_snapshot(&a);assert(a.trigger_percent==20);
    command_test(5,103,1);sweep();air_sensor_get_snapshot(&a);assert(a.trigger_percent==1&&a.trigger[0]==3762);
    command_test(5,104,99);sweep();air_sensor_get_snapshot(&a);assert(a.trigger_percent==99&&a.trigger[0]==38);
    command_test(5,105,12);sweep();lease_until=0;
    assert(air_sensor_set_channel_trigger(4,25,false));sweep();air_sensor_get_snapshot(&a);
    assert(a.trigger[4]==2550&&a.trigger[0]==3344&&a.trigger_percent==0);
    assert(air_sensor_set_channel_trigger(1,3000,true));sweep();air_sensor_get_snapshot(&a);
    assert(a.trigger[1]==3000&&a.release[1]==3070&&a.absolute_mask==2);
    memcpy(mock_samples,initial,12);mock_samples[1]=3000;sweep();assert(air_sensor_blocked_bitmap()==2);
    mock_samples[1]=3070;sweep();assert(air_sensor_blocked_bitmap()==2);
    mock_samples[1]=3071;sweep();assert(air_sensor_blocked_bitmap()==0);
    assert(!air_sensor_set_channel_trigger(6,20,false));assert(!air_sensor_set_channel_trigger(1,3700,true));
    assert(!air_sensor_set_channel_trigger(1,100,false));assert(!air_sensor_set_channel_trigger(1,4095,true));
    uint8_t tune[63]={1,7,106,0,2,0xB8,0x0B};controller_control_receive(1,tune,63);sweep();
    air_sensor_get_snapshot(&a);assert(a.trigger[2]==3000&&a.trigger[1]==3000&&a.trigger[0]==3344);
    command_test(5,107,12);sweep();air_sensor_get_snapshot(&a);assert(a.absolute_mask==0&&a.trigger_percent==12);lease_until=0;
    // No browser lease => no diagnostic reports, then bounded 20 Hz traffic.
    controller_control_task();assert(mock_reports==0);command_test(1,1,0);controller_control_task();assert(mock_reports==1&&get16(mock_report+8)==1);controller_control_task();assert(mock_reports==1);
    mock_now+=50000;controller_control_task();assert(mock_reports==2);
    mock_now+=3000000;controller_control_task();assert(mock_reports==2);
    // Invalid reports cannot mutate brightness; control and interrupt OUT paths work.
    command_test(2,2,50);assert(led_driver_brightness()==50);command_test(2,3,101);assert(ack_result==1&&led_driver_brightness()==50);
    uint8_t b[64]={1,1,2,4,0,25};controller_control_receive(0,b,64);assert(led_driver_brightness()==25);
    b[5]=80;controller_control_receive(0,b,63);assert(led_driver_brightness()==25);
    b[1]=2;controller_control_receive(0,b,64);assert(ack_result==1&&led_driver_brightness()==25);
    // Save before calibration rejected; calibration is asynchronous and never writes flash.
    command_test(4,5,0);assert(ack_result==3&&!restarting);
    for(unsigned i=0;i<6;i++)mock_samples[i]=3000+i*100;
    command_test(3,6,0);assert(ack_result==0);
    // A publication prepared before core 1 consumes the request cannot erase RUNNING.
    publish_snapshot();controller_control_task();assert(was_calibrating);
    air_sensor_get_snapshot(&a);assert(a.calibration_status==AIR_CAL_RUNNING);command_test(3,7,0);assert(ack_result==2);
    command_test(4,8,0);assert(ack_result==2);
    for(unsigned i=0;i<2001;i++){sweep();controller_control_task();}
    air_sensor_get_snapshot(&a);assert(a.calibration_status==AIR_CAL_DONE&&dirty&&mock_flash_calls==0);
    for(unsigned i=0;i<6;i++)assert(a.baselines[i]==3000+i*100);
    uint8_t packet[63];assert(controller_control_report(packet,62)==0);assert(controller_control_report(packet,63)==63);assert(get16(packet+26)==3000&&packet[5]==12&&packet[62]==200);
    // Failed capture retains all previous baselines.
    command_test(3,9,0);mock_samples[0]=0;
    for(unsigned i=0;i<2001;i++){sweep();controller_control_task();}
    air_sensor_get_snapshot(&a);assert(a.calibration_status==AIR_CAL_FAILED&&a.baselines[0]==3000);
    mock_samples[0]=3000;command_test(3,10,0);for(unsigned i=0;i<2001;i++){sweep();controller_control_task();}
    // Save stages watchdog scratch then reboots, never writing flash at runtime.
    command_test(4,11,0);assert(restarting&&mock_flash_calls==0);
    if(setjmp(reboot_jump)==0){mock_now+=250000;controller_control_task();assert(false);}
    assert(mock_watchdog.scratch[0]==SAVE_MAGIC&&mock_flash_calls==0);
    mock_rebooted=true;uint16_t restored[6];assert(controller_control_restore_after_reboot(restored));assert(mock_flash_calls==1&&mock_watchdog.scratch[0]==0&&restored[0]==3000);
    assert(!controller_control_restore_after_reboot(restored)&&mock_flash_calls==1);
    // Brightness scales AFTER the power cap and changing it retains the original frame.
    uint32_t frame[LED_COUNT];for(unsigned i=0;i<LED_COUNT;i++)frame[i]=0xffffff00u;
    led_driver_set_brightness(100);led_driver_submit(frame);emit_frame();uint32_t full=dma_pixels[0];assert((full>>24)<=46);
    led_driver_set_brightness(50);emit_frame();assert((dma_pixels[0]>>24)==(full>>24)/2);
    led_driver_set_brightness(100);emit_frame();assert(dma_pixels[0]==full);
    led_driver_set_brightness(0);emit_frame();assert(dma_pixels[0]==0);
    assert(!led_driver_set_brightness(101)&&led_driver_brightness()==0);
    puts("PASS: calibration/mailbox, thresholds/hysteresis, IO4 mapping, bounded telemetry, command validation, save/reboot handoff, brightness and power ceiling");
    return 0;
}
