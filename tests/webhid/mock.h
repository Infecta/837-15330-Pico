#ifndef WEBHID_TEST_MOCK_H
#define WEBHID_TEST_MOCK_H
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <assert.h>
#define GPIO_OUT 1
#define GPIO_DRIVE_STRENGTH_12MA 12
#define USB_DESCRIPTORS_H
#define CONTROL_HID_INSTANCE 1
#define CONTROL_REPORT_ID 1
#define CONTROL_REPORT_SIZE 63
#define DMA_SIZE_32 0
#define PIO_FIFO_JOIN_TX 0
#define clk_sys 0
typedef unsigned uint;
typedef uint64_t absolute_time_t;
typedef struct {int unused;} pio_sm_config;
typedef struct {int unused;} dma_channel_config;
typedef struct {uint32_t scratch[8];} mock_watchdog_t;
extern mock_watchdog_t mock_watchdog;
#define watchdog_hw (&mock_watchdog)
extern uint64_t mock_now;
extern uint16_t mock_samples[6];
extern uint8_t mock_report[63];
extern unsigned mock_reports, mock_selected, mock_phase, mock_flash_calls;
extern bool mock_mounted, mock_ready, mock_rebooted, mock_save_ok;
static inline uint64_t time_us_64(void) {return mock_now;}
static inline absolute_time_t make_timeout_time_us(uint32_t us) {return mock_now+us;}
static inline bool time_reached(absolute_time_t t) {return mock_now>=t;}
static inline void sleep_us(uint64_t us) {mock_now+=us;}
static inline void tight_loop_contents(void) {}
typedef struct {bool locked;} critical_section_t;
static inline void critical_section_init(critical_section_t *c) {c->locked=false;}
static inline void critical_section_enter_blocking(critical_section_t *c) {assert(!c->locked);c->locked=true;}
static inline void critical_section_exit(critical_section_t *c) {assert(c->locked);c->locked=false;}
static inline void gpio_init(unsigned p) {(void)p;}
static inline void gpio_set_dir(unsigned p,bool b) {(void)p;(void)b;}
static inline void gpio_set_drive_strength(unsigned p,unsigned s) {(void)p;(void)s;}
void gpio_put(unsigned p,bool b);
static inline void adc_init(void) {}
static inline void adc_gpio_init(unsigned p) {(void)p;}
static inline void adc_select_input(unsigned i) {mock_selected=i;}
uint16_t adc_read(void);
static inline bool tud_mounted(void) {return mock_mounted;}
static inline bool tud_hid_n_ready(unsigned n) {assert(n==1);return mock_ready;}
bool tud_hid_n_report(unsigned n,uint8_t id,const void *data,uint16_t size);
static inline bool watchdog_caused_reboot(void) {return mock_rebooted;}
void watchdog_reboot(uint32_t pc,uint32_t sp,uint32_t delay);
// LED hardware is stubbed; the actual power/brightness/frame code is exercised.
struct mock_pio {uint32_t txf[4];};
extern struct mock_pio mock_pio_instance;
#define pio0 (&mock_pio_instance)
static inline uint pio_claim_unused_sm(void *p,bool req) {(void)p;(void)req;return 0;}
static inline uint dma_claim_unused_channel(bool req) {(void)req;return 0;}
static inline uint pio_add_program(void *p,const void *program) {(void)p;(void)program;return 0;}
static inline void pio_gpio_init(void *p,uint pin) {(void)p;(void)pin;}
static inline void pio_sm_set_consecutive_pindirs(void *p,uint s,uint pin,uint n,bool out) {(void)p;(void)s;(void)pin;(void)n;(void)out;}
static inline void sm_config_set_sideset_pins(pio_sm_config *c,uint p) {(void)c;(void)p;}
static inline void sm_config_set_out_shift(pio_sm_config *c,bool a,bool b,uint n) {(void)c;(void)a;(void)b;(void)n;}
static inline void sm_config_set_fifo_join(pio_sm_config *c,uint n) {(void)c;(void)n;}
static inline void sm_config_set_clkdiv(pio_sm_config *c,float n) {(void)c;(void)n;}
static inline uint32_t clock_get_hz(uint c) {(void)c;return 125000000;}
static inline void pio_sm_init(void *p,uint s,uint o,pio_sm_config *c) {(void)p;(void)s;(void)o;(void)c;}
static inline void pio_sm_set_enabled(void *p,uint s,bool e) {(void)p;(void)s;(void)e;}
static inline dma_channel_config dma_channel_get_default_config(uint n) {(void)n;return (dma_channel_config){0};}
static inline void channel_config_set_transfer_data_size(dma_channel_config *c,uint n) {(void)c;(void)n;}
static inline void channel_config_set_read_increment(dma_channel_config *c,bool b) {(void)c;(void)b;}
static inline void channel_config_set_write_increment(dma_channel_config *c,bool b) {(void)c;(void)b;}
static inline uint pio_get_dreq(void *p,uint s,bool b) {(void)p;(void)s;(void)b;return 0;}
static inline void channel_config_set_dreq(dma_channel_config *c,uint n) {(void)c;(void)n;}
static inline void dma_channel_configure(uint d,dma_channel_config *c,void *w,const void *r,uint n,bool b) {(void)d;(void)c;(void)w;(void)r;(void)n;(void)b;}
static inline bool dma_channel_is_busy(uint n) {(void)n;return false;}
static inline void dma_channel_set_read_addr(uint n,const void *p,bool b) {(void)n;(void)p;(void)b;}
static inline void dma_channel_set_trans_count(uint n,uint count,bool b) {(void)n;(void)count;(void)b;}
#endif
