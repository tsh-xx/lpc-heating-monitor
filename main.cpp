#include "mbed.h"
#include "USBSerial.h"
#include "DS1820.h"

//p9 is temperature bus
DigitalOut led(LED1);
DigitalOut suppress(p10);
AnalogIn adc(p18);
InterruptIn meter_pulse(p8);
DigitalIn pulse(p8);
Timer t;
Timer reading_interval;
//Virtual serial port over USB
USBSerial serial;


int meter_pulses = 0;
int clean_pulses = 0;
int meter_interval = 100000000;
int meter_previous = 0;
time_t clock_seconds;
struct tm clock_tm_g;

enum {WAITING,
	TIME,
	DATE,
	SET_PORT,
	SET_ID,
	SET_FLOOR,
	SET_DAY,
	SET_MIN,
	SET_NIGHT
};
enum {QUIET, VERBOSE, DHW};
enum {COLD, RELAX, WARM, RELAX_MISS, WARM_MISS};
enum {NIGHT, NIGHTBOOST, E7, SHORT, LONG, DAY};


#define MAX_PROBES 9
 DS1820* probe[MAX_PROBES];

 void countPulse() {
	 int current_time;
	 meter_pulses++;
         // Only use the first pulse before it gets sampled later for power calculations
	 if(meter_pulses == 1) {
		 current_time = t.read_ms();
		 if (current_time > meter_previous) {
			 meter_interval = current_time - meter_previous;
		 } else {
			 meter_interval = current_time - meter_previous;
		 }
		 meter_previous = current_time;
	 }
 }

 int main() {
     int i;
     int command;
     int devices_found=0;
     int devices_new=0;
     float meter_rate = 0;
     char buffer[10];
     char buffer_date[10];
     int input_count=0;
     int parse_state = WAITING;
     int verbosity = QUIET;
     int in_hold;
     int seq[MAX_PROBES];
     int seq_new[MAX_PROBES];
     int interval = 30;
	 int current_time;
	 int dhw_always = 0;
	 int dhw_floor = 38;
	 int dhw_am = 48;
	 int dhw_day = 45;
	 int dhw_recharge = 25;
//	 int dhw_setpoint = 45;
	 float low[5];
	 float oil;
	 float top;
	 int oil_state = COLD;
	 int dhw_state = DAY;
	 const char oil_char[5] = {'c','r','w','R','W'};
	 const char dhw_char[6] = {'n','N','7','S','L','d'};
     const char suppress_char[2] = {'-','*'};

     // See if this makes the USB more stable.
     wait_us(500);

     serial.printf("Heating Control\r");

     suppress = 0;

     for(i=0;i<5;i++){
    	 low[i]=10;
     }
     meter_pulse.fall(&countPulse);
     t.start();
     reading_interval.start();
     led = 1;
     for (i = 0; i < MAX_PROBES; i++){
         probe[i] = new DS1820(p9);
     	 seq[i] = MAX_PROBES+1;
     }
     // Initialise global state variables
     probe[0]->search_ROM_setup();
     // Loop to find all devices on the data line
     while (probe[devices_found]->search_ROM() and devices_found<MAX_PROBES-1)
         devices_found++;
     // If maximum number of probes are found,
     // bump the counter to include the last array entry
     if (probe[devices_found]->ROM[0] != 0xFF)
         devices_found++;

     if (devices_found==0)
         serial.printf("No devices found");
     else {
         serial.printf("Found %d thermometers\n", devices_found);

         // use devices_new to assign sequence for any un-configured thermometers
         for (i=0; i<devices_found; i++){
        	 command = probe[i]->read_scratchpad();
        	 if (command < MAX_PROBES-1){
        		 if(seq[command] == MAX_PROBES+1){
        			 seq[command] = i;
        		 } else {
        			 serial.printf("Two thermometers with same scratchpad value of %d, %d and %d\n",command,i,seq[command]);
        			 seq_new[devices_new]=i;
        			 devices_new++;
        		 }
        	 } else {
        			 serial.printf("Thermometers with scratchpad value out of range %d, %d (new count %d)\n",command,i,devices_new);
        			 seq_new[devices_new]=i;
        			 devices_new++;
        		 }
         }
         if(devices_new > 0){
        	 serial.printf("Total devices without valid positions %d\n",devices_new);
         }
         while (true) {

        	 // Check for commands from host
        	 if (serial.available() > 0) {
        		 command = serial.getc();
        	 } else {
        		 command = EOF;
        	 }
        	 switch(command){
        	 case EOF: // do nothing
        		 break;
        	 case 'i':{
        		 serial.printf("Heating Control on Cortex-M3\n");
        		 serial.printf("Hot Water enable: %d\n",dhw_always);
        		 serial.printf("Hot Water daytime floor  : %d\n",dhw_floor);
        		 serial.printf("Hot Water mornint preheat: %d\n",dhw_am);
        		 serial.printf("Hot Water daytime targer : %d\n",dhw_day);
        		 serial.printf("Hot Water min lower temp : %d\n",dhw_recharge);
        		 break;
        	 }
        	 case 't': {
        		 serial.printf("Enter Time: hhmmss (non-numeric cancels\n");
        		 clock_seconds = time(NULL);
        		 struct tm *clock_tm = localtime(&clock_seconds);
        		 clock_tm_g = *clock_tm;
        		 parse_state = TIME;
             	 break;
        	 }
        	 case 'd': {
        		 serial.printf("Enter Date: yymmdd (non-numeric cancels\n");
        		 clock_seconds = time(NULL);
        		 struct tm *clock_tm = localtime(&clock_seconds);
        		 clock_tm_g = *clock_tm;
        		 parse_state = DATE;
             	 break;
        	 }
        	 case '0':
        	 case '1':
        	 case '2':
        	 case '3':
        	 case '4':
        	 case '5':
        	 case '6':
        	 case '7':
        	 case '8':
        	 case '9':{
        		 switch(parse_state) {
        		 case SET_PORT:
        			 parse_state = SET_ID;
        			 in_hold = command - '0';
        			 serial.printf("Enter report index for probe %d\n",in_hold);
        			 break;
        		 case SET_ID:
        			 parse_state = WAITING;
        			 probe[in_hold]->write_scratchpad(command-'0');
        			 probe[in_hold]->store_scratchpad(DS1820::this_device);
        			 serial.printf("Write done\n");
        			 break;
        		 case TIME:
        			 input_count++;
        			 serial.putc(command);
        			 switch(input_count){
        			 case 1:
        			 case 3:
        			 case 5: in_hold = command - '0'; break;
        			 case 2: clock_tm_g.tm_hour = (in_hold * 10 + (command - '0')); break;
        			 case 4: clock_tm_g.tm_min  = (in_hold * 10 + (command - '0')); break;
        			 case 6: {
        				 clock_tm_g.tm_sec      = (in_hold * 10 + (command - '0'));
        				 parse_state = WAITING;
        				 input_count = 0;

                		 set_time(mktime(&clock_tm_g));
                		 //Check it worked OK
                		 clock_seconds = time(NULL);
                     	 strftime(buffer, 32, "%H%M%S", localtime(&clock_seconds));
                		 reading_interval.reset();
                     	 serial.printf("\nTime set to %s\n", buffer);
        			 break;
        			 }
        			 }
        			 break;
            		 case DATE:
            			 input_count++;
            			 serial.putc(command);
            			 switch(input_count){
            			 case 1:
            			 case 3:
            			 case 5: in_hold = command - '0'; break;
            			 case 2: clock_tm_g.tm_year = (in_hold * 10 + (command - '0')); break;
            			 case 4: clock_tm_g.tm_mon  = (in_hold * 10 + (command - '0')); break;
            			 case 6: {
            				 clock_tm_g.tm_mday      = (in_hold * 10 + (command - '0'));
            				 parse_state = WAITING;
            				 input_count = 0;

                    		 set_time(mktime(&clock_tm_g));
                    		 //Check it worked OK
                    		 clock_seconds = time(NULL);
                         	 strftime(buffer, 32, "%y%m%d", localtime(&clock_seconds));
                         	 serial.printf("\nDate set to %s\n", buffer);
                         	 serial.printf("Now set time\n");
            			 break;
            			 }
            			 }
            			 break;
                		 case SET_FLOOR:
                			 input_count++;
                			 serial.putc(command);
                			 switch(input_count){
                			 case 1: in_hold = command - '0'; break;
                			 case 2: {
                				 dhw_floor = (in_hold * 10 + (command - '0'));
                				 parse_state = WAITING;
                				 input_count = 0;
                             	 serial.printf("\nHot Water daytime floor set to %d\n", dhw_floor);
                			 break;
                			 }
                			 }
                			 break;
                    		 case SET_DAY:
                    			 input_count++;
                    			 serial.putc(command);
                    			 switch(input_count){
                    			 case 1: in_hold = command - '0'; break;
                    			 case 2: {
                    				 dhw_day = (in_hold * 10 + (command - '0'));
                    				 parse_state = WAITING;
                    				 input_count = 0;
                                 	 serial.printf("\nHot Water daytime target set to %d\n", dhw_day);
                    			 break;
                    			 }
                    			 }
                    			 break;                		 case SET_MIN:
                        			 input_count++;
                        			 serial.putc(command);
                        			 switch(input_count){
                        			 case 1: in_hold = command - '0'; break;
                        			 case 2: {
                        				 dhw_recharge = (in_hold * 10 + (command - '0'));
                        				 parse_state = WAITING;
                        				 input_count = 0;
                                     	 serial.printf("\nHot Water nighttime floor set to %d\n", dhw_recharge);
                        			 break;
                        			 }
                        			 }
                        			 break;                		 case SET_NIGHT:
                            			 input_count++;
                            			 serial.putc(command);
                            			 switch(input_count){
                            			 case 1: in_hold = command - '0'; break;
                            			 case 2: {
                            				 dhw_am = (in_hold * 10 + (command - '0'));
                            				 parse_state = WAITING;
                            				 input_count = 0;
                                         	 serial.printf("\nHot Water night setpoint set to %d\n", dhw_am);
                            			 break;
                            			 }
                            			 }
                            			 break;
                            			 default:
        			 serial.printf("Got digit, but no use for it '%c'\n",command);
        			 break;
        	}
        		 break;
        	 case 'v':
        		 verbosity = QUIET;
        		 interval = 30;
        		 serial.printf("Normal verbosity\n");
        		 break;
        	 case 'V':
        		 if (verbosity == DHW){
        		 verbosity = VERBOSE;
        		 interval = 5;
        		 serial.printf("Debug verbosity\n");
        		 } else {
            		 verbosity = DHW;
            		 interval = 30;
            		 serial.printf("Hot Water verbosity\n");
        		 }
        		 break;
        	 case '?':
        		 serial.printf("v/V : Normal/High verbosity\n");
        		 serial.printf("t   : Set Time as hhmmss\n");
        		 serial.printf("d   : Set Date as ddmmyy\n");
        		 serial.printf("i   : Information\n");
        		 serial.printf("e   : Enumerate 1-wire devices\n");
        		 serial.printf("s   : Set thermometer identifier\n");
        		 serial.printf("h/H : Hot water control active\n");
        		 serial.printf("f   : Set DHW floor daytime (38)\n");
        		 serial.printf("F   : Set DHW daytime charge (45)\n");
        		 serial.printf("n   : Set DHW floor nighttime (25)\n");
        		 serial.printf("N   : Set DHW overnight charge (48)\n");
        		 break;
        	 case 'e':
      			serial.printf("\nChecking Thermometers\n\n");
             	 probe[0]->convert_temperature(DS1820::all_devices);
             	 probe[0]->recall_scratchpad(DS1820::all_devices);
             	 for (i=0; i< devices_found; i++){
        			serial.printf("%d:%x %02x%02x_%02x%02x_%02x%02x %-2.3f @ %d",i,probe[i]->ROM[0],
        					probe[i]->ROM[1],probe[i]->ROM[2],probe[i]->ROM[3],
        					probe[i]->ROM[4],probe[i]->ROM[5],probe[i]->ROM[6],
        					probe[i]->temperature('c'),
        					probe[i]->read_scratchpad());
        			if(probe[i]->ROM_checksum_error()){
        				serial.printf("ROM checksum Error ");
        			}
        			if(probe[i]->RAM_checksum_error()){
        				serial.printf("RAM checksum Error ");
        			}
        			serial.printf("\n");
        		}
        		break;
        	 case 'h':
        		 dhw_always = 1;
        		 serial.printf("Hot water always enabled\n");
        		 break;
        	 case 'H':
        		 dhw_always = 0;
        		 serial.printf("Hot water timer control active\n");
        		 break;
        	 case 'f':
        		 parse_state = SET_FLOOR;
        		 serial.printf("Enter daytime DHW floor\n");
        		 break;
        	 case 'F':
        		 parse_state = SET_DAY;
        		 serial.printf("Enter daytime DHW target\n");
        		 break;
        	 case 'n':
        		 parse_state = SET_MIN;
        		 serial.printf("Enter night DHW floor\n");
        		 break;
        	 case 'N':
        		 parse_state = SET_NIGHT;
        		 serial.printf("Enter night heat target\n");
        		 break;

        	 case 's':
        		 parse_state = SET_PORT;
        		 serial.printf("Enter Thermometer index\n");
        		 break;
        	 // Command does not match
        	 default:{
        		 parse_state = WAITING;
        		 serial.printf("  Unknown command '%c'\n",command);
        	 break;
        	 }
        	 }
        	 }
        	 //ToDo: Reset power at midnight
        	 //Power timer wrap round
        	 //Illogical value check
        	 //fix format of all columns


        	 // Print readings every 30 Sec
        	 if(reading_interval.read()>interval) {
        		 reading_interval.reset();
        		 time_t seconds = time(NULL);
             	 probe[0]->convert_temperature(DS1820::all_devices);

             	 strftime(buffer, 10, "%H%M%S", localtime(&seconds));
             	 serial.printf("%s ", buffer);
             	 if (verbosity==QUIET){
             		 interval = 30 + '0' - buffer[5];
             		 if (buffer[4] == '1'){
             			 interval =-10;
             		 }
             		 if (buffer[4] == '2'){
             			 interval =-20;
             		 }
             		 if (interval < 15) {
             			 interval += 30;
             		 }
                         if (meter_rate > 2500 and interval > 15) {
                             interval = 15;
                         }
             	 }
             	 // Detect midnight
             	 if (buffer[0]=='0' && buffer[1]=='0' && buffer[2]=='0' && buffer[3]=='0' && buffer[4]=='0'){
             		clean_pulses = 0;
             	 }


             	 for (i=0; i<MAX_PROBES; i++) {
             		 int temp;
             		 temp = seq[i];
             		 if(temp > devices_found){
             			serial.printf("-00.00 ");
             		 } else {
             			serial.printf("% 2.2f ",probe[seq[i]]->temperature('c'));
             		 }
             	 }
                 // Check that rate is still accurate
                 // Doesn't handle wrap-round case...
		 current_time = t.read_ms();
		 if ((current_time > (meter_previous + meter_interval)) && (current_time > meter_previous)) {
			 meter_interval = current_time - meter_previous;
                         meter_rate = 3600000.0 / meter_interval;
		         // Clip bad numbers
                         if (meter_rate > 4800) {
                             meter_rate = 4803;
                         }
		 }

             	 serial.printf(" : %6.1f %5d", meter_rate, clean_pulses);

             	 serial.printf(" %c%c%c %.1f\n",oil_char[oil_state],dhw_char[dhw_state],suppress_char[suppress],low[4]-low[0]);
             	 if(verbosity != QUIET){
             		 serial.printf("DHW: Top:% 2.2f Low:% 2.2f Rate: % 2.2f\n",top,low[0],low[4]-low[0]);
             	 }


             	 // dump other temp sensors too
             	 i=0;
             	 while(i<devices_new){
             		 serial.printf("%2.2f ",probe[seq_new[i]]->temperature('c'));
             		 i++;
             	 }
            	 //Pipeline low temperatures
            	low[4] = low[3];
            	low[3] = low[2];
            	low[2] = low[1];
            	low[1] = low[0];
        	 }


        	 // Process oil heater state
        	 if(seq[6] == MAX_PROBES+1){
        		 oil=48;
        	 } else {
        		 oil =  probe[seq[6]]->temperature('c');
        		 if(oil<35) {
        			 oil =  probe[seq[6]]->temperature('c');
        		 }
        		 }
        	 switch (oil_state){
        	 case COLD:
        		 if(oil > 47) {
    				 serial.printf("Was cold, passed 47 to reheat\n");
        			 oil_state = RELAX;
        		 } else
        		 {
        			 if (suppress == 0 and dhw_always == 0){
        				 serial.printf("Reached Cold state - suppressing DHW\n");
        				 suppress = 1;
        			 }
        		 }
        		 break;
        	 case RELAX:
        		 if(oil < 42 && oil > 40) {
    				 serial.printf("Heat cycle done\n");
    				 oil_state = WARM;}
        		 if(oil < 36) {
    				 serial.printf("Failed to hear up %f\n",oil);
    				 oil_state = RELAX_MISS;}
        		 break;
        	 case WARM:
        		 if(oil < 32) {
        			 oil_state = WARM_MISS;
    				 serial.printf("Was warm, now cold @ %f\n",oil);
        		 }
        		 break;
        	 case RELAX_MISS:
        		 if(oil > 36) {
    				 serial.printf("Recovered\n");
    				 oil_state = RELAX;}
        		 if(oil < 34) {
    				 serial.printf("Still Failed to hear up %f\n",oil);
    				 oil_state = COLD;}
        		 break;
        	 case WARM_MISS:
        		 if(oil>33) {
        			 oil_state = WARM;
    				 serial.printf("Warm again @ %f\n",oil);
        		 }
        		 if(oil < 31) {
        			 oil_state = COLD;
    				 serial.printf("Was warm, now cold @ %f\n",oil);
        		 }
        		 break;
        	 }

        	 // Process Water heater state
        	 // Probe 0 = top
        	 // Probe 2 = Flow
        	 // Probe 1 = Lower

        	 if(seq[1] == MAX_PROBES+1){
        		 low[0] = 20;
        	 } else {
        		 low[0] = probe[seq[1]]->temperature('c');
        	 }
        	 if(seq[0] == MAX_PROBES+1){
        		 	 top = 40;
        	 } else {
        		 top =  probe[seq[0]]->temperature('c');
        	 }
        	 switch (dhw_state){
        	 // 9pm to 8am (or heat cycle done)
        	 case NIGHT:
        		 if(dhw_always == 0) {
        			 suppress = 1;
        		 }
        		 // Try to catch end of oil cool-down cycle
             	 if (buffer[0]=='0' && buffer[1]=='6' &&(oil < 42 || buffer[2]>'3')) {dhw_state = E7;}
             	 if (buffer[0]=='1' || buffer[1]>'8')  {dhw_state = DAY;}
             	 // Too cold, short heat
             	 if ((low[0] < dhw_recharge) && (oil_state == WARM)) {
             		 suppress = 0;
             		dhw_state = NIGHTBOOST;
             	 }
             	 break;
        	 case NIGHTBOOST:
        		 suppress = 0;
             	 if (top > dhw_day and dhw_always == 0) {
             		 suppress = 1;
             		 dhw_state = NIGHT;
             	 }
        		 if(oil_state != WARM and dhw_always == 0) {
        			 suppress = 1;
        		     dhw_state = NIGHT;
        		 }
        		 break;
        	 case E7:
        		 suppress = 0;
             	 if (top > dhw_am and dhw_always == 0) {
             		 suppress = 1;
             		 dhw_state = DAY;
             	 }
        		 if(oil_state != WARM and dhw_always == 0) {
        			 suppress = 1;
        		     dhw_state = NIGHT;
        		 }
        		 break;
        	 case SHORT:
        		 if ((top > dhw_day) and ((low[0]-low[4]) > .25) and dhw_always == 0) {
        			 suppress = 1;
        		     dhw_state = DAY;
        		 }
        		 if(oil_state != WARM and dhw_always == 0) {
        			 suppress = 1;
        		     dhw_state = DAY;
        		 }
        		 break;
        	 case LONG:
        		 if (top > dhw_am and dhw_always == 0) {
        			 suppress = 1;
        			 dhw_state = DAY;
        		 }
        		 if(oil_state != WARM and dhw_always == 0) {
        			 suppress = 1;
        		     dhw_state = DAY;
        		 }
        		 break;
        	 case DAY:
        		 if(dhw_always == 0) {
        			 suppress = 1;
        		 }
        		 // 10pm is night, no need for any hot water.
             	 if (buffer[0]=='2' &&  buffer[1]>'1' and dhw_always == 0)  {
             		 dhw_state = NIGHT;
             		 suppress = 1;
             		 break;
             	 }
             	 if (buffer[0]=='0' &&  buffer[1]<'6' and dhw_always == 0)  {
             		 dhw_state = NIGHT;
             		 suppress = 1;
             		 break;
             	 }
             	 if ((((low[4]-low[0]) > 1) ||(top < dhw_floor)) && (oil_state == WARM)) {
             		 dhw_state = SHORT;
             		 suppress = 0;
             	 }
             	 if ((low[0]< dhw_recharge) && (oil_state == WARM)) {
             		 dhw_state = LONG;
             		 suppress = 0;
             	 }
        		 break;
        	 }

        	 if (dhw_always == 1){
        		 suppress = 0;
        	 }
        	 // Idle loop processing of meter reading
             if (meter_pulses){
            	 meter_rate = 3600000.0 / meter_interval;
		 // Clip bad numbers
                 if (meter_rate > 4800) {
                     meter_rate = 4803;
                 }
            	 led = !led;
            	 if(verbosity==VERBOSE){
            	 serial.printf("%d Pulse interval %dus, %4.3f W\n",meter_pulses, meter_interval,meter_rate);
            	 }
            	 meter_pulses = 0;
            	 clean_pulses ++;
             }
         }
     }
 }

