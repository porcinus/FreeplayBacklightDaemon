/*
NNS @ 2018
nns-freeplay-backlight-daemon v0.1b
Monitor gpio pin to turn on and off lcd backlight
*/

#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <cstring>
#include <limits.h>




FILE *temp_filehandle;								//file handle
int gpio_pin = -1;										//gpio pin
char gpio_path[PATH_MAX];							//gpio full path to sysfs
bool gpio_activelow=false;						//gpio active low
char gpio_buffer[4];									//gpio read buffer
int gpio_interval=-1;									//gpio check interval
int gpio_value;												//gpio value
int backlight_last_state=-1;					//gpio last update state


void show_usage(void){
	printf("Example : ./nns-freeplay-backlight-daemon -pin 31 -interval 200\n");
	printf("Options:\n");
	printf("\t-pin, pin number corresponding to input to monitor\n");
	printf("\t-interval, optional, pin checking interval in msec\n");
}

int main(int argc, char *argv[]){
	if(argc<4){show_usage();return 1;} //wrong arguments count
	
	for(int i=1;i<argc;++i){ //argument to variable
		if(strcmp(argv[i],"-help")==0){show_usage();return 1;
		}else if(strcmp(argv[i],"-pin")==0){gpio_pin=atoi(argv[i+1]);snprintf(gpio_path,sizeof(gpio_path),"/sys/class/gpio/gpio%i/",gpio_pin);
		}else if(strcmp(argv[i],"-interval")==0){gpio_interval=atoi(argv[i+1]);}
	}
	
	if(gpio_pin<0){printf("Failed, missing pin argument\n");show_usage();return 1;} //user miss some needed arguments
	if(gpio_interval<100||gpio_interval>600){printf("Warning, wrong cheking interval set, setting it to 200msec\n");gpio_interval=200;} //wrong interval
	
	if(access(gpio_path,R_OK)!=0){ //gpio not accessible, try to export
		printf("%s not accessible, trying export\n",gpio_path);
		temp_filehandle = fopen("/sys/class/gpio/export","wo"); fprintf(temp_filehandle,"%d", gpio_pin); fclose(temp_filehandle); //try export gpio
		if(access(gpio_path,R_OK)!=0){printf("Failed to export\n");return(1);}else{printf("Export with success\n");} //export gpio failed
	}
	
	chdir(gpio_path); //change directory to gpio sysfs
	
	//check pin direction
	temp_filehandle = fopen("direction","r"); fgets(gpio_buffer,sizeof(gpio_buffer),temp_filehandle); fclose(temp_filehandle); //read gpio direction
	if(strcmp(gpio_buffer,"in")==0){printf("Failed, gpio pin direction is %s\n",gpio_buffer);return(1); //check gpio direction
	}/*else{printf("GPIO: direction is %s\n",gpio_buffer);}*/
	
	//check if pin is active low
	temp_filehandle = fopen("active_low","r"); fgets(gpio_buffer,sizeof(gpio_buffer),temp_filehandle); fclose(temp_filehandle); //read gpio active low
	if(strcmp(gpio_buffer,"1")==0){gpio_activelow=true;} //parse gpio active low
	//printf("GPIO: active_low is %s\n",gpio_buffer);
	
	//snprintf(omx_exec_path,sizeof(omx_exec_path),"omxplayer --no-osd --no-keys --alpha 150 --layer 2000 --win 0,0,%i,%i --align center --font-size 750 --no-ghost-box --subtitles \"%s\" \"%s/black.avi\" >/dev/null 2>&1",screen_width,bar_height,str_path,program_path); //parse command line for omx
	
	while(true){
		chdir(gpio_path); //change directory to gpio sysfs
		temp_filehandle = fopen("value","r"); fgets(gpio_buffer,sizeof(gpio_buffer),temp_filehandle); fclose(temp_filehandle); //read gpio value
		gpio_value=atoi(gpio_buffer); //parse gpio value
		if(backlight_last_state!=gpio_value){
			if((gpio_value==0&&!gpio_activelow)||(gpio_value==1&&gpio_activelow)){ //gpio button pressed
				//printf("turn backlight off\n");
				system("/home/pi/Freeplay/setPCA9633/setPCA9633 --verbosity=0 --i2cbus=1 --address=0x62 --mode1=0x01 --mode2=0x15 --led0=PWM --pwm0=0x00 > /dev/null");
			}else{
				//printf("turn backlight on\n");
				system("/home/pi/Freeplay/setPCA9633/setPCA9633 --verbosity=0 --i2cbus=1 --address=0x62 --mode1=0x01 --mode2=0x15 --led0=PWM --pwm0=0x$(printf \"%x\\n\" $(cat /home/pi/Freeplay/setPCA9633/fpbrightness.val)) > /dev/null");
			}
			backlight_last_state=gpio_value;
		}
		usleep(gpio_interval*1000); //sleep
	}
	
	return(0);
}