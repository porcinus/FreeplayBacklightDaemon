/*
 *  Arcade Joystick Driver for RaspberryPi
 *
 *  Copyright (c) 2014 Matthieu Proucelle
 *
 *  Based on the gamecon driver by Vojtech Pavlik, and Markus Hiienkari
 */


/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/mutex.h>
#include <linux/slab.h>

#include <linux/i2c.h>

#include <linux/ioport.h>
#include <asm/io.h>




MODULE_AUTHOR("Matthieu Proucelle (edited for Freeplaytech by Ed Mandy)");
MODULE_DESCRIPTION("Freeplay GPIO Arcade Joystick Driver");
MODULE_LICENSE("GPL");

#define MK_MAX_DEVICES		2
//#define MK_MAX_BUTTONS      13
#define MK_MAX_BUTTONS      21

#ifdef RPI2
#define PERI_BASE        0x3F000000
#else
#define PERI_BASE        0x20000000
#endif

#define GPIO_BASE                (PERI_BASE + 0x200000) /* GPIO controller */

#define INP_GPIO(g) *(gpio+((g)/10)) &= ~(7<<(((g)%10)*3))
#define OUT_GPIO(g) *(gpio+((g)/10)) |=  (1<<(((g)%10)*3))
#define GPIO_READ(g)  ((g < 32) ? (*(gpio + 13) &= (1<<(g))) : (*(gpio + 14) &= (1<<(g-32))))

#define GET_GPIO(g) (*(gpio.addr + BCM2835_GPLEV0/4)&(1<<g)) // 0 if LOW, (1<<g) if HIGH

#define SET_GPIO_ALT(g,a) *(gpio+(((g)/10))) |= (((a)<=3?(a)+4:(a)==4?3:2)<<(((g)%10)*3))

#define GPIO_SET *(gpio+7)
#define GPIO_CLR *(gpio+10)

#define BSC1_BASE		(PERI_BASE + 0x804000)


static volatile unsigned *gpio;

struct mk_config {
	int args[MK_MAX_DEVICES];
	unsigned int nargs;
};

static struct mk_config mk_cfg __initdata;

module_param_array_named(map, mk_cfg.args, int, &(mk_cfg.nargs), 0);
MODULE_PARM_DESC(map, "Enable or disable GPIO, TFT and Custom Arcade Joystick");

struct gpio_config {
	int mk_arcade_gpio_maps_custom[MK_MAX_BUTTONS];
	unsigned int nargs;
};

// for player 1
static struct gpio_config gpio_cfg __initdata;

module_param_array_named(gpio, gpio_cfg.mk_arcade_gpio_maps_custom, int, &(gpio_cfg.nargs), 0);
MODULE_PARM_DESC(gpio, "Numbers of custom GPIO for Arcade Joystick 1");

// for player 2
static struct gpio_config gpio_cfg2 __initdata;

// hotkey
unsigned char hk_state_prev = 0xFF;
//unsigned char hk_data_prev = 0;

unsigned char hk_pre_mode = 0;
int hotkey_combo_btn = -1;

unsigned char data[MK_MAX_BUTTONS];     //so we always keep the state of data

module_param_array_named(gpio2, gpio_cfg2.mk_arcade_gpio_maps_custom, int, &(gpio_cfg2.nargs), 0);
MODULE_PARM_DESC(gpio2, "Numbers of custom GPIO for Arcade Joystick 2");

#define HOTKEY_MODE_UNDEFINED   0
#define HOTKEY_MODE_NORMAL      1
#define HOTKEY_MODE_TOGGLE      2
struct hkmode_config {
	int mode[1];   //HOTKEY_MODE_*
	unsigned int nargs;
};
static struct hkmode_config hkmode_cfg __initdata;

module_param_array_named(hkmode, hkmode_cfg.mode, int, &(hkmode_cfg.nargs), 0);
MODULE_PARM_DESC(hkmode, "Hotkey Button Mode: 1=NORMAL, 2=TOGGLE");

struct i2cbus_config {
	int busnum[1];   //HOTKEY_MODE_*
	unsigned int nargs;
};

static struct i2cbus_config i2cbus_cfg __initdata;

module_param_array_named(i2cbus, i2cbus_cfg.busnum, int, &(i2cbus_cfg.nargs), 0);
MODULE_PARM_DESC(i2cbus, "I2C Bus Number /dev/i2c-# (typically 0 or 1)");

struct analog_config {
	int address[1];   //i2c address of the adc
	unsigned int nargs;
};

static struct analog_config analog_x1_cfg __initdata;
static struct analog_config analog_y1_cfg __initdata;
static struct analog_config analog_x2_cfg __initdata;
static struct analog_config analog_y2_cfg __initdata;

module_param_array_named(x1addr, analog_x1_cfg.address, int, &(analog_x1_cfg.nargs), 0);
MODULE_PARM_DESC(x1addr, "I2C address of X1 ADC MCP3021A chip");

module_param_array_named(y1addr, analog_y1_cfg.address, int, &(analog_y1_cfg.nargs), 0);
MODULE_PARM_DESC(y1addr, "I2C address of Y1 ADC MCP3021A chip");

module_param_array_named(x2addr, analog_x2_cfg.address, int, &(analog_x2_cfg.nargs), 0);
MODULE_PARM_DESC(x2addr, "I2C address of X2 ADC MCP3021A chip");

module_param_array_named(y2addr, analog_y2_cfg.address, int, &(analog_y2_cfg.nargs), 0);
MODULE_PARM_DESC(y2addr, "I2C address of Y2 ADC MCP3021A chip");

struct auto_center_config {
	int auto_center[1];
	unsigned int nargs;
};

static struct auto_center_config auto_center_cfg __initdata;

module_param_array_named(auto_center_analog, auto_center_cfg.auto_center, int, &(auto_center_cfg.nargs), 0);
MODULE_PARM_DESC(auto_center_analog, "Use auto centering for analog sticks (1=yes, 0=no)");

struct analog_direction_config { //nns: add analog direction
	int dir[1];
	unsigned int nargs;
};

static struct analog_direction_config analog_x1_direction_cfg __initdata;
static struct analog_direction_config analog_y1_direction_cfg __initdata;
static struct analog_direction_config analog_x2_direction_cfg __initdata;
static struct analog_direction_config analog_y2_direction_cfg __initdata;

module_param_array_named(x1dir, analog_x1_direction_cfg.dir, int, &(analog_x1_direction_cfg.nargs), 0);
MODULE_PARM_DESC(x1dir, "X1 analog direction");

module_param_array_named(y1dir, analog_y1_direction_cfg.dir, int, &(analog_y1_direction_cfg.nargs), 0);
MODULE_PARM_DESC(y1dir, "Y1 analog direction");

module_param_array_named(x2dir, analog_x2_direction_cfg.dir, int, &(analog_x2_direction_cfg.nargs), 0);
MODULE_PARM_DESC(x2dir, "X2 analog direction");

module_param_array_named(y2dir, analog_y2_direction_cfg.dir, int, &(analog_y2_direction_cfg.nargs), 0);
MODULE_PARM_DESC(y2dir, "Y2 analog direction");


//i2c ADC
#define ABS_PARAMS_DEFAULT_X_MIN 374
#define ABS_PARAMS_DEFAULT_X_MAX 3418
#define ABS_PARAMS_DEFAULT_X_FUZZ 16
#define ABS_PARAMS_DEFAULT_X_FLAT 384

#define ABS_PARAMS_DEFAULT_Y_MIN 517
#define ABS_PARAMS_DEFAULT_Y_MAX 3378
#define ABS_PARAMS_DEFAULT_Y_FUZZ 16
#define ABS_PARAMS_DEFAULT_Y_FLAT 384


struct analog_abs_params_config { //nns: add analog parameters
    int abs_params[4];
    unsigned int nargs;
};

static struct analog_abs_params_config analog_x1_abs_params_cfg __initdata;
static struct analog_abs_params_config analog_x2_abs_params_cfg __initdata;
static struct analog_abs_params_config analog_y1_abs_params_cfg __initdata;
static struct analog_abs_params_config analog_y2_abs_params_cfg __initdata;

module_param_array_named(x1params, analog_x1_abs_params_cfg.abs_params, int, &(analog_x1_abs_params_cfg.nargs), 0);
MODULE_PARM_DESC(x1params, "X1 ADC absolute parameters (min,max,fuzz,flat)");

module_param_array_named(y1params, analog_y1_abs_params_cfg.abs_params, int, &(analog_y1_abs_params_cfg.nargs), 0);
MODULE_PARM_DESC(y1params, "Y1 ADC absolute parameters (min,max,fuzz,flat)");

module_param_array_named(x2params, analog_x2_abs_params_cfg.abs_params, int, &(analog_x2_abs_params_cfg.nargs), 0);
MODULE_PARM_DESC(x2params, "X2 ADC absolute parameters (min,max,fuzz,flat)");

module_param_array_named(y2params, analog_y2_abs_params_cfg.abs_params, int, &(analog_y2_abs_params_cfg.nargs), 0);
MODULE_PARM_DESC(y2params, "Y2 ADC absolute parameters (min,max,fuzz,flat)");


struct delayed_work mk_delayed_work;
struct mk *g_mk = NULL;

static struct i2c_board_info __initdata board_info[] = {{I2C_BOARD_INFO("MCP3021X1", 0x48),}};

struct i2c_client *i2c_new_MCP3021(struct i2c_adapter *adapter, u16 address){
	struct i2c_board_info info = {I2C_BOARD_INFO("MCP3021", address),};
	return i2c_new_device(adapter, &info);
}

struct i2c_adapter* i2c_dev = NULL;
struct i2c_client* i2c_client_x1 = NULL;
struct i2c_client* i2c_client_y1 = NULL;
struct i2c_client* i2c_client_x2 = NULL;
struct i2c_client* i2c_client_y2 = NULL;
uint16_t x1_max = 0;
uint16_t x1_min = 0xFFFF;
uint16_t x1_offset = 2048; //nns
uint16_t y1_max = 0;
uint16_t y1_min = 0xFFFF;
uint16_t y1_offset = 2048; //nns
uint16_t x2_max = 0;
uint16_t x2_min = 0xFFFF;
uint16_t x2_offset = 2048; //nns
uint16_t y2_max = 0;
uint16_t y2_min = 0xFFFF;
uint16_t y2_offset = 2048; //nns

bool x1_reverse = false; //nns: x1 reverse direction
bool y1_reverse = false; //nns: y1 reverse direction
bool x2_reverse = false; //nns: x2 reverse direction
bool y2_reverse = false; //nns: y2 reverse direction
bool auto_center = false; //nns: analog center offcenter

struct analog_abs_params_struct {int min, max, fuzz, flat;} x1_analog_abs_params, x2_analog_abs_params, y1_analog_abs_params, y2_analog_abs_params;

enum mk_type {
	MK_NONE = 0,
	MK_ARCADE_GPIO,
	MK_ARCADE_GPIO_BPLUS,
	MK_ARCADE_GPIO_TFT,
	MK_ARCADE_GPIO_CUSTOM,
	MK_ARCADE_GPIO_CUSTOM2,
	MK_MAX
};

#define MK_REFRESH_TIME	HZ/100

struct mk_pad {
	struct input_dev *dev;
	enum mk_type type;
	char phys[32];
	int hotkey_mode;
	int gpio_maps[MK_MAX_BUTTONS];
};

struct mk_nin_gpio {
	unsigned pad_id;
	unsigned cmd_setinputs;
	unsigned cmd_setoutputs;
	unsigned valid_bits;
	unsigned request;
	unsigned request_len;
	unsigned response_len;
	unsigned response_bufsize;
};

struct mk {
	struct mk_pad pads[MK_MAX_DEVICES];
	struct timer_list timer;
	int used;
	struct mutex mutex;
	int total_pads;
};

struct mk_subdev {unsigned int idx;};

static struct mk *mk_base;

// Map of the gpios :                     up, down, left, right, start, select, a,  b,  tr, y,  x,  tl, hk
static const int mk_arcade_gpio_maps[] = {4,  17,    27,  22,    10,    9,      25, 24, 23, 18, 15, 14, 2, -1, -1, -1, -1};
// 2nd joystick on the b+ GPIOS                 up, down, left, right, start, select, a,  b,  tr, y,  x,  tl, hk
static const int mk_arcade_gpio_maps_bplus[] = {11, 5,    6,    13,    19,    26,     21, 20, 16, 12, 7,  8,  3, -1, -1, -1, -1};

// Map joystick on the b+ GPIOS with TFT      up, down, left, right, start, select, a,  b,  tr, y,  x,  tl, hk
static const int mk_arcade_gpio_maps_tft[] = {21, 13,    26,    19,    5,    6,     22, 4, 20, 17, 27,  16, 12, -1, -1, -1, -1};
static const short mk_arcade_gpio_btn[] = {BTN_START, BTN_SELECT, BTN_A, BTN_B, BTN_TR, BTN_Y, BTN_X, BTN_TL, BTN_MODE /*this one can be special*/, BTN_TL2, BTN_TR2, BTN_C, BTN_Z, BTN_TOP, BTN_TOP2, BTN_BASE, BTN_BASE2};
static const char *mk_names[] = {NULL, "GPIO Controller 1", "GPIO Controller 2", "MCP23017 Controller", "GPIO Controller 1" , "GPIO Controller 1", "GPIO Controller 2"};


static int16_t ADC_OffsetCenter(uint16_t adc_resolution,uint16_t adc_value,uint16_t adc_min,uint16_t adc_max,int16_t adc_offset){
	int16_t adc_center;
	int16_t range;
	int32_t ratio;
	int16_t corrected_value;
	
	adc_center=adc_resolution/2;
	
	if(adc_value<(adc_center+adc_offset)){ //value under center offset
		range=(adc_center+adc_offset)-adc_min;
		if(range!=0){ //to avoid divide by 0
			ratio=10000*adc_center/range; //float workaround
			corrected_value=(adc_value-adc_min)*ratio/10000;
		}else{corrected_value=adc_value;} //range=0, setting problems?
	}else{ //value over center offset
		range=adc_max-(adc_center+adc_offset);
		if(range!=0){ //to avoid divide by 0
			ratio=10000*adc_center/range; //float workaround
			corrected_value=adc_center+(adc_value-(adc_center+adc_offset))*ratio/10000;
		}else{corrected_value=adc_value;} //range=0, setting problems?
	}
	
	if(corrected_value<0){corrected_value=0;}else if(corrected_value>4095){corrected_value=4095;} //constrain computed value to 12bits value
	return corrected_value;
}


/* GPIO UTILS */
static void setGpioPullUps(uint32_t pullUps, uint32_t pullUpsHigh){
	*(gpio + 37) = 0x02;
	udelay(10);
	*(gpio + 38) = pullUps;
	*(gpio + 39) = pullUpsHigh;
	udelay(10);
	*(gpio + 37) = 0x00;
	*(gpio + 38) = 0x00;
	*(gpio + 39) = 0x00;
}


static void setGpioAsInput(int gpioNum){
	INP_GPIO(gpioNum);
}


static int getPullUpMask(int gpioMap[], uint32_t *maskLow, uint32_t *maskHigh){
	int i;
	*maskLow = 0x0000000;
	*maskHigh = 0x0000000;
	
	for(i=0; i<MK_MAX_BUTTONS;i++){
		if(gpioMap[i] > -1){ //to avoid unused pins
			if(gpioMap[i] < 32){
				*maskLow |= 1<<gpioMap[i];
			}else if(gpioMap[i] < 64){
				*maskHigh |= 1<<(gpioMap[i] - 32);
			}
		}
	}
	
	//printk("mask low=0x%08X high=0x%08X\n", *maskLow, *maskHigh);
	return *maskLow;
}


static void mk_gpio_read_packet(struct mk_pad * pad, unsigned char *data){
	int i;
	
	for(i = 0; i < MK_MAX_BUTTONS; i++){
		if(pad->gpio_maps[i] != -1){    // to avoid unused buttons
			if((i==12) && (pad->hotkey_mode == HOTKEY_MODE_TOGGLE)){  //the hotkey
				//we use the hotkey as a toggle (press to toggle data[i])
				unsigned char hk_state;
				int read;
				if(pad->gpio_maps[i] < 0){
					read = GPIO_READ(pad->gpio_maps[i] * -1); //invert this signal
					if(read == 0){
						hk_state = 0;
					}else{
						hk_state = 1; //pressed
					}
				}else{
					read = GPIO_READ(pad->gpio_maps[i]);
					if(read == 0){
						hk_state = 1; //pressed
					}else{
						hk_state = 0;
					}
				}
				
				if(hk_state != hk_state_prev){ //the hotkey changed
					hk_state_prev = hk_state;
					
					//if it changed and it's now a 1, we enter pre-hotkey mode
					if(hk_state){
						if(hk_pre_mode){
							//the PWR btn itself is the hotkey
							data[12] = 1;   //turn on the hotkey
							hotkey_combo_btn = i;
						}else{
							hk_pre_mode = 1;
							hotkey_combo_btn = -1;
						}
					}else if(hotkey_combo_btn == i){  //the hotkey was just released, and the PWR btn itself is the hotkey
						data[12] = 0;   //turn off the hotkey
						hk_pre_mode = 0;
						hotkey_combo_btn = -1;
					}
				}
			}else{
				//all other (non-hotkey) buttons just report their state to data[i]
				//except when we are in hk_state
				unsigned char prev_data = data[i];
				if(pad->gpio_maps[i] < 0){ //invert this signal
					int read = GPIO_READ(pad->gpio_maps[i] * -1);
					if(read == 0){
						data[i] = 0;
					}else{data[i] = 1;}
				}else{
					int read = GPIO_READ(pad->gpio_maps[i]);
					if(read == 0){
						data[i] = 1;
					}else{data[i] = 0;}
				}
				
				if(prev_data != data[i]){ //the state of this button changed
					if(hk_pre_mode){
						if(data[i]){ //the button was just pressed
							data[12] = 1;   //turn on the hotkey
							hotkey_combo_btn = i;
						}else if(i == hotkey_combo_btn){   //the button was just released
							data[12] = 0;   //turn off the hotkey
							hk_pre_mode = 0;
							hotkey_combo_btn = -1;
						}
					}
				}
			}
		}else{data[i] = 0;}
	}
}


static void mk_input_report(struct mk_pad * pad, unsigned char * data){
	struct input_dev * dev = pad->dev;
	int j;
	int16_t adc_val = 0;
	uint8_t buf[2];
	
	if(i2c_client_x1){ //if using analog, then the DPAD is ABS_HAT0X
		input_report_abs(dev, ABS_HAT0X, !data[2]-!data[3]);
	}else{
		input_report_abs(dev, ABS_X, !data[2]-!data[3]);
	}
	
	if(i2c_client_y1){ //if using analog, then the DPAD is ABS_HAT0Y
		input_report_abs(dev, ABS_HAT0Y, !data[0]-!data[1]);
	}else{
		input_report_abs(dev, ABS_Y, !data[0]-!data[1]);
	}
	
	if(i2c_client_x1){
		adc_val = i2c_smbus_read_word_swapped(i2c_client_x1, 0);
		if(adc_val>=0){
			if(x1_reverse){adc_val = 4096 - adc_val;} //nns: reverse 12bits value
			if(adc_val < x1_min){x1_min = adc_val;}
			if(adc_val > x1_max){x1_max = adc_val;}
			if(auto_center){adc_val = ADC_OffsetCenter(4096,adc_val,x1_analog_abs_params.min,x1_analog_abs_params.max,x1_offset);} //nns: adc value correction
			input_report_abs(dev, ABS_X, adc_val);
		}else{
			printk("mk_arcade_joystick_rpi: failed to read analog X1\n");
		}
	}
	
	if(i2c_client_y1){
		adc_val = i2c_smbus_read_word_swapped(i2c_client_y1, 0);
		if(adc_val>=0){
			if(y1_reverse){adc_val = 4096 - adc_val;} //nns: reverse 12bits value
			if(adc_val < y1_min){y1_min = adc_val;}
			if(adc_val > y1_max){y1_max = adc_val;}
			if(auto_center){adc_val = ADC_OffsetCenter(4096,adc_val,y1_analog_abs_params.min,y1_analog_abs_params.max,y1_offset);} //nns: adc value correction
			input_report_abs(dev, ABS_Y, adc_val);
		}else{
			printk("mk_arcade_joystick_rpi: failed to read analog Y1\n");
		}
	}
	
	if(i2c_client_x2){
		adc_val = i2c_smbus_read_word_swapped(i2c_client_x2, 0);
		if(adc_val>=0){
			if(x2_reverse){adc_val = 4096 - adc_val;} //nns: reverse 12bits value
			if(adc_val < x2_min){x2_min = adc_val;}
			if(adc_val > x2_max){x2_max = adc_val;}
			if(auto_center){adc_val = ADC_OffsetCenter(4096,adc_val,x2_analog_abs_params.min,x2_analog_abs_params.max,x2_offset);} //nns: adc value correction
			input_report_abs(dev, ABS_RX, adc_val);
		}else{
			printk("mk_arcade_joystick_rpi: failed to read analog X2\n");
		}
	}
	
	if(i2c_client_y2){
		adc_val = i2c_smbus_read_word_swapped(i2c_client_y2, 0);
		if(adc_val>=0){
			if(y2_reverse){adc_val = 4096 - adc_val;} //nns: reverse 12bits value
			if(adc_val < y2_min){y2_min = adc_val;}
			if(adc_val > y2_max){y2_max = adc_val;}
			if(auto_center){adc_val = ADC_OffsetCenter(4096,adc_val,y2_analog_abs_params.min,y2_analog_abs_params.max,y2_offset);} //nns: adc value correction
			input_report_abs(dev, ABS_RY, adc_val);
		}else{
			printk("mk_arcade_joystick_rpi: failed to read analog Y2\n");
		}
	}
	
	for (j = 4; j < MK_MAX_BUTTONS; j++){
		if(pad->gpio_maps[j] != -1){input_report_key(dev, mk_arcade_gpio_btn[j - 4], data[j]);}
	}
	
	input_sync(dev);
}


static void mk_process_packet(struct mk *mk){
	struct mk_pad *pad;
	int i;
	
	for(i = 0; i < mk->total_pads; i++){
		pad = &mk->pads[i];
		mk_gpio_read_packet(pad, data);     //data is now global
		mk_input_report(pad, data);
	}
}


static void mk_timer(unsigned long private){ //mk_timer() initiates reads of console pads data.
	struct mk *mk = (void *) private;
	mk_process_packet(mk);
	mod_timer(&mk->timer, jiffies + MK_REFRESH_TIME);
}


static void mk_work_handler(struct work_struct* work){
	struct mk *mk = g_mk;
	mk_process_packet(mk);
	schedule_delayed_work(&mk_delayed_work, MK_REFRESH_TIME);
}


static int mk_open(struct input_dev *dev){
	struct mk *mk = input_get_drvdata(dev);
	int err;
	
	err = mutex_lock_interruptible(&mk->mutex);
	if(err){return err;}
	
	if(!mk->used++){
		//mod_timer(&mk->timer, jiffies + MK_REFRESH_TIME);
		schedule_delayed_work(&mk_delayed_work, MK_REFRESH_TIME);
	}
	
	mutex_unlock(&mk->mutex);
	return 0;
}


static void mk_close(struct input_dev *dev){
	struct mk *mk = input_get_drvdata(dev);
	
	mutex_lock(&mk->mutex);
	
	if(!--mk->used){
		//del_timer_sync(&mk->timer);
		cancel_delayed_work_sync(&mk_delayed_work);
	}
	
	mutex_unlock(&mk->mutex);
}


static int __init mk_setup_pad(struct mk *mk, int idx, int pad_type_arg){
	struct mk_pad *pad = &mk->pads[idx];
	struct input_dev *input_dev;
	int i, pad_type;
	int err;
	pr_err("Freeplay Button Driver\n");
	pr_err("pad type : %d\n",pad_type_arg);
	
	pad_type = pad_type_arg;
	
	if(pad_type < 1 || pad_type >= MK_MAX){
		pr_err("Pad type %d unknown\n", pad_type);
		return -EINVAL;
	}
	
	pad->hotkey_mode = hkmode_cfg.mode[0]; //for now, the hkmode parameter is "global" to all pads
	
	if(pad_type == MK_ARCADE_GPIO_CUSTOM){ //if the device is custom, be sure to get correct pins
		if(gpio_cfg.nargs < 1){
			pr_err("Custom device needs gpio argument\n");
			return -EINVAL;
		}else if(gpio_cfg.nargs != MK_MAX_BUTTONS){
			pr_err("Invalid gpio argument\n", pad_type);
			return -EINVAL;
		}
	}
	
	if(pad_type == MK_ARCADE_GPIO_CUSTOM2){ //if the device is custom, be sure to get correct pins
		if(gpio_cfg2.nargs < 1){
			pr_err("Custom device needs gpio argument\n");
			return -EINVAL;
		}else if(gpio_cfg2.nargs != MK_MAX_BUTTONS){
			pr_err("Invalid gpio argument\n", pad_type);
			return -EINVAL;
		}
	}
	
	pr_err("pad type : %d\n",pad_type);
	pad->dev = input_dev = input_allocate_device();
	if(!input_dev){
		pr_err("Not enough memory for input device\n");
		return -ENOMEM;
	}
	
	pad->type = pad_type;
	snprintf(pad->phys, sizeof (pad->phys), "input%d", idx);
	
	input_dev->name = mk_names[pad_type];
	input_dev->phys = pad->phys;
	input_dev->id.bustype = BUS_PARPORT;
	input_dev->id.vendor = 0x0001;
	input_dev->id.product = pad_type;
	input_dev->id.version = 0x0100;
	
	input_set_drvdata(input_dev, mk);
	
	input_dev->open = mk_open;
	input_dev->close = mk_close;
	
	input_dev->evbit[0] = BIT_MASK(EV_KEY) | BIT_MASK(EV_ABS);
	
	mk->total_pads++;
	
	// asign gpio pins
	switch (pad_type){
		case MK_ARCADE_GPIO:
			memcpy(pad->gpio_maps, mk_arcade_gpio_maps, MK_MAX_BUTTONS *sizeof(int));
			break;
		case MK_ARCADE_GPIO_BPLUS:
			memcpy(pad->gpio_maps, mk_arcade_gpio_maps_bplus, MK_MAX_BUTTONS *sizeof(int));
			break;
		case MK_ARCADE_GPIO_TFT:
			memcpy(pad->gpio_maps, mk_arcade_gpio_maps_tft, MK_MAX_BUTTONS *sizeof(int));
			break;
		case MK_ARCADE_GPIO_CUSTOM:
			memcpy(pad->gpio_maps, gpio_cfg.mk_arcade_gpio_maps_custom, MK_MAX_BUTTONS *sizeof(int));
			break;
		case MK_ARCADE_GPIO_CUSTOM2:
			memcpy(pad->gpio_maps, gpio_cfg2.mk_arcade_gpio_maps_custom, MK_MAX_BUTTONS *sizeof(int));
			break;
	}
	
	if(i2c_client_x1){ //if using analog, then DPAD is ABS_HAT0X
		input_set_abs_params(input_dev, ABS_HAT0X, -1, 1, 0, 0);
	}else{
		input_set_abs_params(input_dev, ABS_X, -1, 1, 0, 0);
	}
	
	if(i2c_client_y1){ //if using analog, then DPAD is ABS_HAT0Y
		input_set_abs_params(input_dev, ABS_HAT0Y, -1, 1, 0, 0);
	}else{
		input_set_abs_params(input_dev, ABS_Y, -1, 1, 0, 0);
	}
	
	/*
	MCP3221 is 12-bit, MCP3021 is 10-bit, but both report as 12-bits
	values from testing PSP 1000 stick on 3021:
	[ 1013.444203] mk_arcade_joystick_rpi: x1 min value: 0x01F6
	[ 1013.444206] mk_arcade_joystick_rpi: x1 max value: 0x0D38
	[ 1013.444248] mk_arcade_joystick_rpi: y1 min value: 0x0142
	[ 1013.444250] mk_arcade_joystick_rpi: y1 max value: 0x0CF8
	
	nns:
	[  +0.000103] mk_arcade_joystick_rpi: x1 min value: 0x0176
	[  +0.000006] mk_arcade_joystick_rpi: x1 max value: 0x0D5A
	[  +0.000079] mk_arcade_joystick_rpi: y1 min value: 0x0205
	[  +0.000005] mk_arcade_joystick_rpi: y1 max value: 0x0D32
	*/
	
	//i2c ADC
	if(auto_center){
		if(i2c_client_x1){input_set_abs_params(input_dev, ABS_X, 0x000, 0xFFF, x1_analog_abs_params.fuzz, x1_analog_abs_params.flat);} //nns: parameters for center offcenter values
		if(i2c_client_y1){input_set_abs_params(input_dev, ABS_Y, 0x000, 0xFFF, y1_analog_abs_params.fuzz, y1_analog_abs_params.flat);} //nns: parameters for center offcenter values
		if(i2c_client_x2){input_set_abs_params(input_dev, ABS_RX, 0x000, 0xFFF, x2_analog_abs_params.fuzz, x2_analog_abs_params.flat);} //nns: parameters for center offcenter values
		if(i2c_client_y2){input_set_abs_params(input_dev, ABS_RY, 0x000, 0xFFF, y2_analog_abs_params.fuzz, y2_analog_abs_params.flat);} //nns: parameters for center offcenter values
	}else{
		//void input_set_abs_params(struct input_dev *dev, unsigned int axis,int min, int max, int fuzz, int flat)
		if(i2c_client_x1){input_set_abs_params(input_dev, ABS_X, x1_analog_abs_params.min, x1_analog_abs_params.max, x1_analog_abs_params.fuzz, x1_analog_abs_params.flat);}
		if(i2c_client_y1){input_set_abs_params(input_dev, ABS_Y, y1_analog_abs_params.min, y1_analog_abs_params.max, y1_analog_abs_params.fuzz, y1_analog_abs_params.flat);}
		if(i2c_client_x2){input_set_abs_params(input_dev, ABS_RX, x2_analog_abs_params.min, x2_analog_abs_params.max, x2_analog_abs_params.fuzz, x2_analog_abs_params.flat);}
		if(i2c_client_y2){input_set_abs_params(input_dev, ABS_RY, y2_analog_abs_params.min, y2_analog_abs_params.max, y2_analog_abs_params.fuzz, y2_analog_abs_params.flat);}
	}
	
	for (i = 0; i < MK_MAX_BUTTONS - 4; i++){
		if(pad->gpio_maps[i+4] != -1){__set_bit(mk_arcade_gpio_btn[i], input_dev->keybit);}
	}
	
	// initialize gpio
	for (i = 0; i < MK_MAX_BUTTONS; i++){
		if(pad->gpio_maps[i] != -1){    // to avoid unused buttons
			if(pad->gpio_maps[i] < 0){
				setGpioAsInput(pad->gpio_maps[i] * -1);
			}else{
				setGpioAsInput(pad->gpio_maps[i]);
			}
		}
	}
	
	uint32_t pullUpMaskLow, pullUpMaskHigh;
	getPullUpMask(pad->gpio_maps, &pullUpMaskLow, &pullUpMaskHigh);
	
	setGpioPullUps(pullUpMaskLow, pullUpMaskHigh);
	printk("GPIO configured for pad%d\n", idx);
	
	err = input_register_device(pad->dev);
	if(err){goto err_free_dev;}
	
	return 0;
	
	err_free_dev: input_free_device(pad->dev); pad->dev = NULL; return err;
}


static struct mk __init *mk_probe(int *pads, int n_pads){
	struct mk *mk;
	int i;
	int count = 0;
	int err;
	
	mk = kzalloc(sizeof (struct mk), GFP_KERNEL);
	if(!mk){
		pr_err("Not enough memory\n");
		err = -ENOMEM;
		goto err_out;
	}
	
	mutex_init(&mk->mutex);
	//setup_timer(&mk->timer, mk_timer, (long) mk);
	g_mk = mk;
	INIT_DELAYED_WORK(&mk_delayed_work, mk_work_handler);
	
	for(i = 0; i < n_pads && i < MK_MAX_DEVICES; i++){
		if(!pads[i]){continue;}
		err = mk_setup_pad(mk, i, pads[i]);
		if(err){goto err_unreg_devs;}
		count++;
	}
	
	if(count == 0){
		pr_err("No valid devices specified\n");
		err = -EINVAL;
		goto err_free_mk;
	}
	
	return mk;
	
	err_unreg_devs: while(--i >= 0){if(mk->pads[i].dev){input_unregister_device(mk->pads[i].dev);}}
	err_free_mk: kfree(mk);
	err_out: return ERR_PTR(err);
}


static void mk_remove(struct mk *mk){
	int i;
	
	for (i = 0; i < MK_MAX_DEVICES; i++){
		if(mk->pads[i].dev){
			input_unregister_device(mk->pads[i].dev);
		}
	}
	
	kfree(mk);
}


static int __init mk_init(void){
	/* Set up gpio pointer for direct register access */
	if((gpio = ioremap(GPIO_BASE, 0xB0)) == NULL){
		pr_err("io remap failed\n");
		return -EBUSY;
	}
	
	if(hkmode_cfg.nargs == 0){ //if hkmode was not defined
		hkmode_cfg.mode[0] = HOTKEY_MODE_TOGGLE; //default to HOTKEY_MODE_TOGGLE if not set
	}
	
	if(i2cbus_cfg.nargs == 0){ //if i2cbus addr was not defined
		i2cbus_cfg.busnum[0] = -1; //default to not using i2c
	}
	
	if(analog_x1_cfg.nargs == 0){ //if analog input i2c addr was not defined
		analog_x1_cfg.address[0] = 0; //default to not using it
	}
	
	if(analog_y1_cfg.nargs == 0){ //if analog input i2c addr was not defined
		analog_y1_cfg.address[0] = 0; //default to not using it
	}
	
	if(analog_x2_cfg.nargs == 0){ //if analog input i2c addr was not defined
		analog_x2_cfg.address[0] = 0; //default to not using it
	}
	
	if(analog_y2_cfg.nargs == 0){ //if analog input i2c addr was not defined
		analog_y2_cfg.address[0] = 0; //default to not using it
	}
	
	if(auto_center_cfg.nargs > 0){ //if auto_center_analog set
		if(auto_center_cfg.auto_center[0]){auto_center = true;} //if value > 0 enable
	}
	
	if(analog_x1_direction_cfg.nargs > 0){ //if x1dir set
		if(analog_x1_direction_cfg.dir[0]<0){x1_reverse=true;} //nns:direction reversed
	}
	
	if(analog_y1_direction_cfg.nargs > 0){ //if y1dir set
		if(analog_y1_direction_cfg.dir[0]<0){y1_reverse=true;} //nns:direction reversed
	}
	
	if(analog_x2_direction_cfg.nargs > 0){ //if x2dir set
		if(analog_x2_direction_cfg.dir[0]<0){x2_reverse=true;} //nns:direction reversed
	}
	
	if(analog_y2_direction_cfg.nargs > 0){ //if y2dir set
		if(analog_y2_direction_cfg.dir[0]<0){y2_reverse=true;} //nns:direction reversed
	}
	
	
	x1_analog_abs_params.min = ABS_PARAMS_DEFAULT_X_MIN;
	x1_analog_abs_params.max = ABS_PARAMS_DEFAULT_X_MAX;
	x1_analog_abs_params.fuzz = ABS_PARAMS_DEFAULT_X_FUZZ;
	x1_analog_abs_params.flat = ABS_PARAMS_DEFAULT_X_FLAT;
	if(analog_x1_abs_params_cfg.nargs > 0){ //if analog_x1_abs_params_cfg set
		x1_analog_abs_params.min = analog_x1_abs_params_cfg.abs_params[0];
		if(analog_x1_abs_params_cfg.nargs > 1){
			x1_analog_abs_params.max = analog_x1_abs_params_cfg.abs_params[1];
			if(analog_x1_abs_params_cfg.nargs > 2){
				x1_analog_abs_params.fuzz = analog_x1_abs_params_cfg.abs_params[2];
				if(analog_x1_abs_params_cfg.nargs > 3){
				x1_analog_abs_params.flat = analog_x1_abs_params_cfg.abs_params[3];}
			}
		}
	}
	
	y1_analog_abs_params.min = ABS_PARAMS_DEFAULT_Y_MIN;
	y1_analog_abs_params.max = ABS_PARAMS_DEFAULT_Y_MAX;
	y1_analog_abs_params.fuzz = ABS_PARAMS_DEFAULT_Y_FUZZ;
	y1_analog_abs_params.flat = ABS_PARAMS_DEFAULT_Y_FLAT;
	if(analog_y1_abs_params_cfg.nargs > 0){ //if analog_y1_abs_params_cfg set
		y1_analog_abs_params.min = analog_y1_abs_params_cfg.abs_params[0];
		if(analog_y1_abs_params_cfg.nargs > 1){
			y1_analog_abs_params.max = analog_y1_abs_params_cfg.abs_params[1];
			if(analog_y1_abs_params_cfg.nargs > 2){
				y1_analog_abs_params.fuzz = analog_y1_abs_params_cfg.abs_params[2];
				if(analog_y1_abs_params_cfg.nargs > 3){y1_analog_abs_params.flat = analog_y1_abs_params_cfg.abs_params[3];}
			}
		}
	}
	
	x2_analog_abs_params.min = ABS_PARAMS_DEFAULT_X_MIN;
	x2_analog_abs_params.max = ABS_PARAMS_DEFAULT_X_MAX;
	x2_analog_abs_params.fuzz = ABS_PARAMS_DEFAULT_X_FUZZ;
	x2_analog_abs_params.flat = ABS_PARAMS_DEFAULT_X_FLAT;
	if(analog_x2_abs_params_cfg.nargs > 0){ //if analog_x2_abs_params_cfg set
		x2_analog_abs_params.min = analog_x2_abs_params_cfg.abs_params[0];
		if(analog_x2_abs_params_cfg.nargs > 1){
			x2_analog_abs_params.max = analog_x2_abs_params_cfg.abs_params[1];
			if(analog_x2_abs_params_cfg.nargs > 2){
				x2_analog_abs_params.fuzz = analog_x2_abs_params_cfg.abs_params[2];
				if(analog_x2_abs_params_cfg.nargs > 3){x2_analog_abs_params.flat = analog_x2_abs_params_cfg.abs_params[3];}
			}
		}
	}
	
	y2_analog_abs_params.min = ABS_PARAMS_DEFAULT_Y_MIN;
	y2_analog_abs_params.max = ABS_PARAMS_DEFAULT_Y_MAX;
	y2_analog_abs_params.fuzz = ABS_PARAMS_DEFAULT_Y_FUZZ;
	y2_analog_abs_params.flat = ABS_PARAMS_DEFAULT_Y_FLAT;
	if(analog_y2_abs_params_cfg.nargs > 0){ //if analog_y2_abs_params_cfg set
		y2_analog_abs_params.min = analog_y2_abs_params_cfg.abs_params[0];
		if(analog_y2_abs_params_cfg.nargs > 1){
			y2_analog_abs_params.max = analog_y2_abs_params_cfg.abs_params[1];
			if(analog_y2_abs_params_cfg.nargs > 2){
				y2_analog_abs_params.fuzz = analog_y2_abs_params_cfg.abs_params[2];
				if(analog_y2_abs_params_cfg.nargs > 3){y2_analog_abs_params.flat = analog_y2_abs_params_cfg.abs_params[3];}
			}
		}
	}
	
	
	if(i2cbus_cfg.busnum[0] >= 0){
		int rm;
		int retry;
		rm = request_module("i2c_bcm2835");
		printk("mk_arcade_joystick_rpi: request_module i2c_bcm2835 returned %d\n", rm);
		
		i2c_dev = i2c_get_adapter(i2cbus_cfg.busnum[0]);
		
		for(retry=0;retry<3;retry++){
			if(!i2c_dev){
				printk("mk_arcade_joystick_rpi: I2C bus %d NOT opened (sleeping and retrying)\n", i2cbus_cfg.busnum[0]);
				msleep(500);
				i2c_dev = i2c_get_adapter(i2cbus_cfg.busnum[0]);
			}else{
				retry=3;
			}
		}
		
		if(i2c_dev){
			int16_t value = 0;
			i2c_dev->timeout=1; //try to set i2c timeout to 10ms (1=10ms)
			
			//https://github.com/torvalds/linux/blob/master/drivers/i2c/i2c-dev.c:482
			printk("mk_arcade_joystick_rpi: I2C bus %d opened\n", i2cbus_cfg.busnum[0]);
			printk("mk_arcade_joystick_rpi: I2C bus timeout set to %d ms\n", (i2c_dev->timeout)*10);
			
			if(analog_x1_cfg.address[0] > 0){
				i2c_client_x1 = i2c_new_MCP3021(i2c_dev, analog_x1_cfg.address[0]);
				if(i2c_client_x1){
					printk("mk_arcade_joystick_rpi: X1 assigned to I2C address 0x%02X\n", analog_x1_cfg.address[0]);
					value = i2c_smbus_read_word_swapped(i2c_client_x1, 0);
					if(value & 0x8000){
						printk("mk_arcade_joystick_rpi: X1 chip not found\n");
						i2c_unregister_device(i2c_client_x1);
						i2c_client_x1 = NULL;
					}else{
						if(x1_reverse){ //nns: reverse direction
							printk("mk_arcade_joystick_rpi: X1 direction reversed\n");
							value = 4096-value; //nns: reverse 12bits value
						}
						
						if(value >= 0){x1_offset = value-2047;} //nns: center offset
						printk("mk_arcade_joystick_rpi: initial X1 value: %d (0x%04X)\n", value, value);
					}
				}
			}
			
			if(analog_y1_cfg.address[0] > 0){
				i2c_client_y1 = i2c_new_MCP3021(i2c_dev, analog_y1_cfg.address[0]);
				if(i2c_client_y1){
					printk("mk_arcade_joystick_rpi: Y1 assigned to I2C address 0x%02X\n", analog_y1_cfg.address[0]);
					value = i2c_smbus_read_word_swapped(i2c_client_y1, 0);
					if(value & 0x8000){
						printk("mk_arcade_joystick_rpi: Y1 chip not found\n");
						i2c_unregister_device(i2c_client_y1);
						i2c_client_y1 = NULL;
					}else{
						if(y1_reverse){ //nns: reverse direction
							printk("mk_arcade_joystick_rpi: Y1 direction reversed\n");
							value = 4096-value; //nns: reverse 12bits value
						}
						
						if(value >= 0){y1_offset = value-2047;} //nns: center offset
						printk("mk_arcade_joystick_rpi: initial Y1 value: %d (0x%04X)\n", value, value);
					}
				}
			}
			
			if(analog_x2_cfg.address[0] > 0){
				i2c_client_x2 = i2c_new_MCP3021(i2c_dev, analog_x2_cfg.address[0]);
				if(i2c_client_x2){
					printk("mk_arcade_joystick_rpi: X2 assigned to I2C address 0x%02X\n", analog_x2_cfg.address[0]);
					value = i2c_smbus_read_word_swapped(i2c_client_x2, 0);
					if(value & 0x8000){
						printk("mk_arcade_joystick_rpi: X2 chip not found\n");
						i2c_unregister_device(i2c_client_x2);
						i2c_client_x2 = NULL;
					}else{
						if(x2_reverse){ //nns: reverse direction
							printk("mk_arcade_joystick_rpi: X2 direction reversed\n");
							value = 4096-value; //nns: reverse 12bits value
						}
						
						if(value >= 0){x2_offset = value-2047;} //nns: center offset
						printk("mk_arcade_joystick_rpi: initial X2 value: %d (0x%04X)\n", value, value);
					}
				}
			}
			
			if(analog_y2_cfg.address[0] > 0){
				i2c_client_y2 = i2c_new_MCP3021(i2c_dev, analog_y2_cfg.address[0]);
				if(i2c_client_y2){
					printk("mk_arcade_joystick_rpi: Y2 assigned to I2C address 0x%02X\n", analog_y2_cfg.address[0]);
					value = i2c_smbus_read_word_swapped(i2c_client_y2, 0);
					if(value & 0x8000){
						printk("mk_arcade_joystick_rpi: Y2 chip not found\n");
						i2c_unregister_device(i2c_client_y2);
						i2c_client_y2 = NULL;
					}else{
						if(y2_reverse){ //nns: reverse direction
							printk("mk_arcade_joystick_rpi: Y2 direction reversed\n");
							value = 4096-value; //nns: reverse 12bits value
						}
						
						if(value >= 0){y2_offset = value-2047;} //nns: center offset
						printk("mk_arcade_joystick_rpi: initial Y2 value: %d (0x%04X)\n", value, value);
					}
				}
			}
		}else{
			printk("mk_arcade_joystick_rpi ERROR: I2C bus %d NOT opened (make sure that I2C is enabled and loaded before this driver)\n", i2cbus_cfg.busnum[0]);
		}
	}
	
	if(mk_cfg.nargs < 1){
		pr_err("at least one device must be specified\n");
		return -EINVAL;
	}else{
		mk_base = mk_probe(mk_cfg.args, mk_cfg.nargs);
		if(IS_ERR(mk_base)){return -ENODEV;}
	}
	
	return 0;
}

static void __exit mk_exit(void){
	if(mk_base){mk_remove(mk_base);}
	
	printk("mk_arcade_joystick_rpi: exiting\n");
	
	if(i2c_client_x1){
		i2c_unregister_device(i2c_client_x1);
		printk("mk_arcade_joystick_rpi: X1 min value: %d (0x%04X)\n", x1_min, x1_min);
		printk("mk_arcade_joystick_rpi: X1 max value: %d (0x%04X)\n", x1_max, x1_max);
	}
	if(i2c_client_y1){
		i2c_unregister_device(i2c_client_y1);
		printk("mk_arcade_joystick_rpi: Y1 min value: %d (0x%04X)\n", y1_min, y1_min);
		printk("mk_arcade_joystick_rpi: Y1 max value: %d (0x%04X)\n", y1_max, y1_max);
	}
	if(i2c_client_x2){
		i2c_unregister_device(i2c_client_x2);
		printk("mk_arcade_joystick_rpi: X2 min value: %d (0x%04X)\n", x2_min, x2_min);
		printk("mk_arcade_joystick_rpi: X2 max value: %d (0x%04X)\n", x2_max, x2_max);
	}
	
	if(i2c_client_y2){
		i2c_unregister_device(i2c_client_y2);
		printk("mk_arcade_joystick_rpi: Y2 min value: %d (0x%04X)\n", y2_min, y2_min);
		printk("mk_arcade_joystick_rpi: Y2 max value: %d (0x%04X)\n", y2_max, y2_max);
	}
	
	iounmap(gpio);
}

module_init(mk_init);
module_exit(mk_exit);
