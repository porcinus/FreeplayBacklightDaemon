/*
NNS @ 2018
nns-freeplay-backlight-daemon v0.1c
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
#include <time.h> 
#include <dirent.h> 




FILE *temp_filehandle;								//file handle
int gpio_pin = -1;										//gpio pin
char gpio_path[PATH_MAX];							//gpio full path to sysfs
bool gpio_activelow=false;						//gpio active low
char gpio_buffer[4];									//gpio read buffer
int gpio_interval=-1;									//gpio check interval
int gpio_value;												//gpio value
int backlight_last_state=-1;					//gpio last update state
bool gpio_wrong_direction = true;			//gpio wrong direction

char pbuffer[20];													//buffer use to read process pipe
DIR *inputdir;														//input dir handle
struct dirent *inputdir_cnt;							//input dir contener
int input_listcheck_interval = 15;				//interval to refresh input list
unsigned int input_listcheck_start = 0;		//time of last /dev/input/ check
int input_found = 0;											//count input device found
bool input_detected = false;							//user input detected
char input_cmd_tmp[PATH_MAX];							//temporary array to store event
char input_cmd[PATH_MAX];									//array to store event command


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
	
	if(access("/home/pi/Freeplay/setPCA9633/fpbrightness.val",R_OK)!=0){
			printf("fpbrightness.val not found, Creating now one\n");
			temp_filehandle=fopen("/home/pi/Freeplay/setPCA9633/fpbrightness.val","wb");
			fprintf(temp_filehandle,"%d",255);
			fclose(temp_filehandle); //create file
		}
	
	
	
	
	if(access(gpio_path,R_OK)!=0){ //gpio not accessible, try to export
		printf("%s not accessible, trying export\n",gpio_path);
		temp_filehandle = fopen("/sys/class/gpio/export","wo"); fprintf(temp_filehandle,"%d", gpio_pin); fclose(temp_filehandle); //try export gpio
		if(access(gpio_path,R_OK)!=0){printf("Failed to export\n");return(1);}else{printf("Export with success\n");} //export gpio failed
	}
	
	chdir(gpio_path); //change directory to gpio sysfs
	
	//check pin direction
	temp_filehandle = fopen("direction","r"); fgets(gpio_buffer,sizeof(gpio_buffer),temp_filehandle); fclose(temp_filehandle); //read gpio direction
	
	while(gpio_wrong_direction){
		if(strcmp(gpio_buffer,"in")==0){
			printf("Failed, gpio pin direction is %s\n",gpio_buffer); //check gpio direction
			sleep(2);
		}else{
			printf("GPIO: direction is %s\n",gpio_buffer); //debug
			gpio_wrong_direction = false;
		}
	}
	
	//check if pin is active low
	temp_filehandle = fopen("active_low","r"); fgets(gpio_buffer,sizeof(gpio_buffer),temp_filehandle); fclose(temp_filehandle); //read gpio active low
	if(strcmp(gpio_buffer,"1")==0){gpio_activelow=true;} //parse gpio active low
	printf("GPIO: active_low is %s\n",gpio_buffer); //debug
	
	while(true){
		chdir(gpio_path); //change directory to gpio sysfs
		temp_filehandle = fopen("value","r"); fgets(gpio_buffer,sizeof(gpio_buffer),temp_filehandle); fclose(temp_filehandle); //read gpio value
		gpio_value=atoi(gpio_buffer); //parse gpio value
		
		input_detected=false;
		if((time(NULL) - input_listcheck_start)>input_listcheck_interval){ //time to refresh input list
			input_listcheck_start=time(NULL); //update start time
		  input_found=0; //reset input count
			inputdir = opendir("/dev/input/"); //open dir handle
		  if(inputdir){ //success
		  	strcpy(input_cmd,"bash -c '"); //start building command
		    while((inputdir_cnt = readdir(inputdir)) != NULL){ //scan input folder
		      if(strncmp(inputdir_cnt->d_name,"event",5)==0){ //filename start with 'event'
		      	input_found++; //increment input count
		      	snprintf(input_cmd_tmp,sizeof(input_cmd_tmp),"([ -e /dev/input/%s ] && timeout 0.2s cat /dev/input/%s;) & ",inputdir_cnt->d_name,inputdir_cnt->d_name); //build temporary command
		      	//snprintf(input_cmd_tmp,sizeof(input_cmd_tmp),"cat /dev/input/%s & ",inputdir_cnt->d_name); //build temporary command
		      	strcat(input_cmd,input_cmd_tmp); //concatenate command and temporary command
		      }
		    }
		    input_cmd[strlen(input_cmd)-3]=0; //trim last ' & '
		    strcat(input_cmd,"'"); //complete command line
		    closedir(inputdir); //close dir handle
		    //printf("Refresh input device list : %i device(s) found\n",input_found); //debug
		    //printf("Rebuild command: %s\n\n",input_cmd); //debug
		  }else{printf("Failed to open /dev/input/\n");} //oups
		}
		
		if(input_found>0){ //if input device found
			temp_filehandle = popen(input_cmd, "r"); //open process pipe
			if(temp_filehandle!=NULL){ //if process not fail
				if(fgets(pbuffer,16,temp_filehandle)){input_detected=true;} //if output detected
		  	pclose(temp_filehandle); //close process pipe
			}
		}
		
		if(backlight_last_state!=gpio_value||input_detected){
			if(((gpio_value==0&&!gpio_activelow)||(gpio_value==1&&gpio_activelow))&&!input_detected){ //gpio button pressed
				//printf("turn backlight off\n");
				//if(input_detected){printf("backlight off, user input detected\n");} //debug
				system("/home/pi/Freeplay/setPCA9633/setPCA9633 --verbosity=0 --i2cbus=1 --address=0x62 --mode1=0x01 --mode2=0x15 --led0=PWM --pwm0=0x00 > /dev/null");
			}else{
				//printf("turn backlight on\n");
				//if(input_detected){printf("backlight on, user input detected\n");} //debug
				system("/home/pi/Freeplay/setPCA9633/setPCA9633 --verbosity=0 --i2cbus=1 --address=0x62 --mode1=0x01 --mode2=0x15 --led0=PWM --pwm0=0x$(printf \"%x\\n\" $(cat /home/pi/Freeplay/setPCA9633/fpbrightness.val)) > /dev/null");
			}
			backlight_last_state=gpio_value;
		}
		usleep(gpio_interval*1000); //sleep
	}
	
	return(0);
}