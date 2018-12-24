/*
NNS @ 2018
nns-freeplay-backlight-daemon v0.2b
Monitor gpio pin and evdev input to turn on and off lcd backlight
*/


#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <cstring>
#include <limits.h>
#include <errno.h>
//#include <time.h> 
#include <dirent.h> 

#include <pthread.h>
#include <sys/time.h>
#include <signal.h>

#include <linux/input.h>


int debug_mode=0; //program is in debug mode, 0=no 1=low 2=full

//input section
pthread_t input_thread; 									//input thread id
bool input_check=true;										//monitor input check
int input_thread_rc=-1; 									//input thread return code
int input_listcheck_interval = 15000; 		//interval to refresh input list in msec
long long input_listcheck_start = 0; 			//timestamp of last /dev/input/ check
int input_device_found = -1; 							//count input device found
DIR *input_device_dir_handle; 						//input dir handle
struct dirent *input_device_dir_cnt; 			//input dir contener
char input_device_list[31][PATH_MAX]; 		//input device path array
int input_device_filehandle[31]; 							//file handle
fd_set input_device_filedecriptor;				//file descriptor
int input_device_highfd;									//file descriptor higher fd
bool input_event_readcomplete=false;			//use for input event read timeout
struct timeval input_event_timeout;				//use to set event read timeout
int input_event_read_return;							//use for select in event read loop	
struct input_event input_event_scan[64]; 			//use to scan event input
unsigned int input_event_size[31]; 						//use to store event input size
int input_event_duration = 200; 					//use for input event read timeout in msec
long long input_event_detected = 0; 			//timestamp of last detected input


//gpio section
pthread_t gpio_thread; 										//gpio thread id
int gpio_thread_rc=-1; 										//gpio thread return code
long long gpio_tmp_timestamp = 0; 				//temporary gpio timestamp
FILE *temp_filehandle; 										//file handle
int gpio_pin = -1; 												//gpio pin
char gpio_path[PATH_MAX]; 								//gpio full path to sysfs
char gpio_buffer[4]; 											//gpio read buffer
bool gpio_exported=false; 								//gpio export with success
bool gpio_wrong_direction = true; 				//gpio wrong direction
bool gpio_activelow=false; 								//gpio active low
bool gpio_ready=false; 										//gpio ready
int gpio_interval=500; 										//gpio check interval in msec
int gpio_value; 													//gpio value

bool backlight_on=true;											//current backlight state
bool backlight_forced_on=false;							//force backlight on
int input_event_forced_wake_duration = 60;	//wake forced duration if input detected in sec





long long timestamp_msec(){ //https://stackoverflow.com/questions/3756323/how-to-get-the-current-time-in-milliseconds-from-c-in-linux
    struct timeval te; 
    gettimeofday(&te, NULL); // get current time
    long long milliseconds = te.tv_sec*1000LL + te.tv_usec/1000; // calculate milliseconds
    return milliseconds;
}


void *input_routine(void *){ //input routine
	printf("input thread (%lu) started\n",input_thread); //debug
	
	while(input_thread_rc>-1){
		if(debug_mode){printf("input device : %lli : refresh device(s) list\n",timestamp_msec());} //debug
		input_listcheck_start=timestamp_msec(); //update start time
		input_device_found=-1; //reset input device counter
		input_device_dir_handle = opendir("/dev/input/"); //open dir handle
		if(input_device_dir_handle){ //no problem opening dir handle
			while((input_device_dir_cnt=readdir(input_device_dir_handle))!=NULL){ //scan input folder
				if(strncmp(input_device_dir_cnt->d_name,"event",5)==0){ //filename start with 'event'
					input_device_found++; //increment input count
					strcpy(input_device_list[input_device_found],"/dev/input/"); //start building device path
					strcat(input_device_list[input_device_found],input_device_dir_cnt->d_name); //concatenate device path
					if(debug_mode){printf("input device : %lli : device : %s\n",timestamp_msec(),input_device_list[input_device_found]);} //debug
				}
			}
			if(debug_mode){printf("input device : %lli : refresh complete : %i device(s) found(s)\n",timestamp_msec(),input_device_found+1);} //debug
			closedir(input_device_dir_handle); //close dir handle
		}else{if(debug_mode){printf("input device : failed to open /dev/input/\n");}} //fail to open dir handle
		
		if(input_device_found>-1){
			for(int input_device_loop=0;input_device_loop<input_device_found+1;input_device_loop++){ //open each file handle loop
				if(access(input_device_list[input_device_loop],R_OK)==0){input_device_filehandle[input_device_loop]=open(input_device_list[input_device_loop],O_RDONLY); //open input device file handle
				}else{if(debug_mode){printf("input device : %lli : failed to open %s, skip\n",timestamp_msec(),input_device_list[input_device_loop]);}} //file not more exist
			}
			
			while(((timestamp_msec()-input_listcheck_start)<input_listcheck_interval)&&input_device_found>-1){ //loop until refresh input list is needed
				input_event_timeout.tv_sec=0; //set select sec timout
		    input_event_timeout.tv_usec=input_event_duration*1000; //set select usec timout
				input_device_highfd=-1;
				FD_ZERO(&input_device_filedecriptor); //clear file desciptor set
				for(int input_device_loop=0;input_device_loop<input_device_found+1;input_device_loop++){ //set all file descriptor
					if(access(input_device_list[input_device_loop],R_OK)==0){
						FD_SET(input_device_filehandle[input_device_loop],&input_device_filedecriptor); //add descriptor to the set
						input_device_highfd=input_device_filehandle[input_device_loop];
					}else{
						close(input_device_filehandle[input_device_loop]);
					}
				}
				
				input_event_read_return=1; //trick
				while(input_event_read_return&&input_device_found>-1){ //input read loop
					input_event_read_return=select(input_device_highfd+1,&input_device_filedecriptor,NULL,NULL,&input_event_timeout); //use to bypass non blocking mode
					if(input_event_read_return&&input_device_found>-1){
						for(int input_device_loop=0;input_device_loop<input_device_found+1;input_device_loop++){ //select each file handle loop
							if(access(input_device_list[input_device_loop],R_OK)==0){ //file exist
								if(FD_ISSET(input_device_filehandle[input_device_loop],&input_device_filedecriptor)){ //data receive for this handle
									input_event_size[input_device_loop] = read(input_device_filehandle[input_device_loop],&input_event_scan,sizeof(struct input_event)*64); //read input device
									if(input_event_size[input_device_loop] < sizeof(struct input_event)){ //read wrong size, failed
										if(debug_mode>1){printf("input device : %lli : %s : error, wrong size : need %u bytes, got %u\n",timestamp_msec(),input_device_list[input_device_loop],sizeof(struct input_event),input_event_size);} //debug
									}else{ //no problem
										if(input_event_size[input_device_loop]!=-1){ //avoid sigfault is a input is disconnected
											for(int i=0;i<input_event_size[input_device_loop]/sizeof(struct input_event);i++){ //if multiple input in a small time
												if(sizeof(input_event_scan[i])==sizeof(struct input_event)){
													if(input_event_scan[i].type>0&&input_event_scan[i].type<=0x20&&input_event_scan[i].type!=3){ //https://github.com/torvalds/linux/blob/master/include/uapi/linux/input-event-codes.h, don't monitor sync, abs or wrong type
														input_event_detected=input_event_scan[i].time.tv_sec*1000LL + input_event_scan[i].time.tv_usec/1000; //register last detected input
														if(debug_mode){printf("input device : %lli : %s : input detected, type: %i, code: %i, value: %i\n",input_event_detected,input_device_list[input_device_loop],input_event_scan[i].type,input_event_scan[i].code,input_event_scan[i].value);} //debug
													}
												}
											}
										}
									}
								}
							}else{ //a input is disconnected, force list update
								if(debug_mode){printf("input device : %lli : %s : error, device no more exist, force list update\n",timestamp_msec(),input_device_list[input_device_loop]);}
								input_device_found=-1; //reset device count
							}
						}
					}
				}
			}
			for(int input_device_loop=0;input_device_loop<input_device_found+1;input_device_loop++){close(input_device_filehandle[input_device_loop]);} //close each input device file handle loop
		}else{
			if(debug_mode){printf("input device : %lli : no input device found\n",timestamp_msec());} //debug
			sleep(5); //sleep 5 sec if no input device found
		}
	}
	
	input_thread_rc=-1; //pseudo error code
	pthread_cancel(input_thread); //close input thread if trouble
	return NULL;
}


void *gpio_routine(void *){ //gpio routine
	printf("gpio thread (%lu) started\n",input_thread); //debug
	gpio_tmp_timestamp=timestamp_msec(); //timestamp backup
	snprintf(gpio_path,sizeof(gpio_path),"/sys/class/gpio/gpio%i/",gpio_pin); //parse gpio path
	while(!gpio_exported){
		if(access(gpio_path,R_OK)!=0){ //gpio not accessible, try to export
			printf("gpio : %lli : %s not accessible, trying export\n",gpio_tmp_timestamp,gpio_path); //debug
			temp_filehandle = fopen("/sys/class/gpio/export","wo"); //open file handle
			fprintf(temp_filehandle,"%d", gpio_pin); //write pin number
			fclose(temp_filehandle); //close file handle
			sleep(2); //sleep to avoid spam
		}else{
			gpio_exported=true; //gpio export with success
			printf("gpio : %lli : %s is accessible\n",gpio_tmp_timestamp,gpio_path); //debug
		}
	}
	
	chdir(gpio_path); //change directory to gpio sysfs
	
	//check pin direction
	temp_filehandle = fopen("direction","r"); //open file handle
	fgets(gpio_buffer,sizeof(gpio_buffer),temp_filehandle); //read file handle
	fclose(temp_filehandle); //close file handle
	
	while(gpio_wrong_direction){ //wrong pin direction
		if(strcmp(gpio_buffer,"in")==0){ //pin direction is 'in'
			printf("gpio : %lli : %s : wrong pin direction : in , retry\n",gpio_tmp_timestamp,gpio_path); //debug
			sleep(5); //sleep to avoid spam
		}else{ //pin direction is 'out'
			printf("gpio : %lli : %s : pin direction : out\n",gpio_tmp_timestamp,gpio_path); //debug
			gpio_wrong_direction = false; //right gpio direction
		}
	}
	
	//check if pin is active low
	temp_filehandle = fopen("active_low","r"); //open file handle
	fgets(gpio_buffer,sizeof(gpio_buffer),temp_filehandle); //read file handle
	fclose(temp_filehandle); //close file handle
	if(strcmp(gpio_buffer,"1")==0){gpio_activelow=true;} //parse gpio active low
	printf("gpio : %lli : %s : active_low : %i\n",gpio_tmp_timestamp,gpio_path,gpio_activelow); //debug
	
	gpio_ready=true; //trigger gpio ready state
	
	while(gpio_thread_rc>-1){
		gpio_tmp_timestamp=timestamp_msec(); //timestamp backup
		chdir(gpio_path); //change directory to gpio sysfs
		temp_filehandle = fopen("value","r"); //open file handle
		fgets(gpio_buffer,sizeof(gpio_buffer),temp_filehandle); //read file handle
		fclose(temp_filehandle); //close file handle
		gpio_value=atoi(gpio_buffer); //parse gpio value
		if(debug_mode>1){printf("gpio : %lli : %s : value : %i\n",gpio_tmp_timestamp,gpio_path,gpio_value);} //debug
		usleep(gpio_interval*1000); //sleep
	}
	
	gpio_thread_rc=-1; //pseudo error code
	pthread_cancel(gpio_thread); //close input thread if trouble
	return NULL;
}


void show_usage(void){
	printf("Example : ./nns-freeplay-backlight-daemon -pin 31 -interval 200 -noinput -debug 1\n");
	printf("Options:\n");
	printf("\t-pin, gpio pin number to monitor\n");
	printf("\t-interval, optional, gpio pin checking interval in msec, 500 if not set\n");
	printf("\t-noinput, optional, do not monitor user input, monitor input if not set\n");
	printf("\t-inputinterval, optional, monitoring interval for each detected evdev input device in msec, 100 if not set\n");
	printf("\t-inputwakeduration, optional, if no activity on screen but user input detected, wake backlight for specified duration, in sec, 60 if not set\n");
	printf("\t-debug, optional, 1=limited, 2=full(will spam logs), 0 if not set\n");
}

int main(int argc, char *argv[]){ //main
	for(int i=1;i<argc;++i){ //argument to variable
		if(strcmp(argv[i],"-help")==0){show_usage();return 1;
		}else if(strcmp(argv[i],"-pin")==0){gpio_pin=atoi(argv[i+1]);
		}else if(strcmp(argv[i],"-interval")==0){gpio_interval=atoi(argv[i+1]);
		}else if(strcmp(argv[i],"-noinput")==0){input_check=false;
		}else if(strcmp(argv[i],"-inputinterval")==0){input_event_duration=atoi(argv[i+1]);
		}else if(strcmp(argv[i],"-inputwakeduration")==0){input_event_forced_wake_duration=atoi(argv[i+1]);
		}else if(strcmp(argv[i],"-debug")==0){debug_mode=atoi(argv[i+1]);}
	}
	
	if(gpio_pin<0){printf("Failed, missing pin argument\n");show_usage();return 1;} //user miss some needed arguments
	if(gpio_interval<100||gpio_interval>600){printf("Warning, wrong gpio interval set, setting it to 500msec\n");gpio_interval=500;} //wrong interval
	if(input_check&&(input_event_duration<50||input_event_duration>200)){printf("Warning, wrong input interval set, setting it to 100msec\n");input_event_duration=200;} //wrong interval
	if(!input_check){printf("Input device monitoring disable\n");input_event_duration=100;} //wrong interval
	
	if(access("/home/pi/Freeplay/setPCA9633/fpbrightness.val",R_OK)!=0){ //fpbrightness.val not exist
			printf("fpbrightness.val not found, Creating now one\n"); //debug
			temp_filehandle=fopen("/home/pi/Freeplay/setPCA9633/fpbrightness.val","wb"); //open file handle
			fprintf(temp_filehandle,"%d",255); //write data
			fclose(temp_filehandle); //close file handle
	}
	
	while(true){
		//input thread section
		if(input_thread_rc==-1 && input_check){ //thread failed or not started
			if(input_thread!=0){ //thread already existed
				printf("input thread (%lu) failed, restarting\n",input_thread); //debug
				pthread_join(input_thread,NULL); //avoid pthread memory leak
			}
			input_thread_rc=pthread_create(&input_thread, NULL, input_routine, NULL); //create routine thread
		}
		
		//gpio thread section
		if(gpio_thread_rc==-1){ //thread failed
			if(gpio_thread!=0){ //thread already existed
				printf("gpio thread (%lu) failed, restarting\n",gpio_thread); //debug
				pthread_join(gpio_thread,NULL); //avoid pthread memory leak
			}
			gpio_thread_rc=pthread_create(&gpio_thread, NULL, gpio_routine, NULL); //create routine thread
		}
		
		//control backlight section
		usleep(500000); //sleep 500ms
		if(gpio_ready){ //gpio is ready
			if(input_check&&(timestamp_msec()-input_event_detected)<1000&&((gpio_value==0&&!gpio_activelow)||(gpio_value==1&&gpio_activelow))){ //user input detected during last 1000msec
				if(debug_mode){printf("user input force backlight on for next %i sec\n",input_event_forced_wake_duration);}
				backlight_forced_on=true;
			}else if(backlight_forced_on&&input_check&&(timestamp_msec()-input_event_detected)>input_event_forced_wake_duration*1000){ //no user input detected during last forced wake duration
				if(debug_mode){printf("backlight no more in force mode\n");}
				backlight_forced_on=false;
			}

			if(((gpio_value==1&&!gpio_activelow)||(gpio_value==0&&gpio_activelow))||backlight_forced_on){ //gpio input or input detected
				if(!backlight_on){ //if backlight is off
					if(debug_mode){printf("turn backlight on\n");}
					system("/home/pi/Freeplay/setPCA9633/setPCA9633 --verbosity=0 --i2cbus=1 --address=0x62 --mode1=0x01 --mode2=0x15 --led0=PWM --pwm0=0x$(printf \"%x\\n\" $(cat /home/pi/Freeplay/setPCA9633/fpbrightness.val)) > /dev/null");
					backlight_on=true; //toggle backlight variable
				}
			}else{
				if(backlight_on){ //if backlight is on
					if(debug_mode){printf("turn backlight off\n");}
					system("/home/pi/Freeplay/setPCA9633/setPCA9633 --verbosity=0 --i2cbus=1 --address=0x62 --mode1=0x01 --mode2=0x15 --led0=PWM --pwm0=0x00 > /dev/null");
					backlight_on=false; //toggle backlight variable
				}
			}
		}
	}
	
	return(0);
}
