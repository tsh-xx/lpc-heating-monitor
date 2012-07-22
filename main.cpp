#include "mbed.h"
#include "USBSerial.h"
#include "DS1820.h"

DigitalOut led(LED1);
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
	SET_ID
};
enum {QUIET, VERBOSE};

#define MAX_PROBES 9
 DS1820* probe[MAX_PROBES];

 void countPulse() {
	 int current_time;
	 meter_pulses++;
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

     // See if this makes the USB more stable.
     wait_us(500);

     serial.printf("Heating Control\r");

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
        		 verbosity = VERBOSE;
        		 interval = 5;
        		 serial.printf("Debug verbosity\n");
        		 break;
        	 case '?':
        		 serial.printf("v/V : Normal/High verbosity\n");
        		 serial.printf("t   : Set Time as hhmmss\n");
        		 serial.printf("d   : Set Date as ddmmyy\n");
        		 serial.printf("i   : Information\n");
        		 serial.printf("e   : Enumerate 1-wire devices\n");
        		 serial.printf("s   : Set thermometer identifier\n");
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
             	 }
             	 // Detect midnight
             	 if (buffer[0]=='0' && buffer[1]=='0' && buffer[2]=='0' && buffer[3]=='0' && buffer[4]=='0'){
             		clean_pulses = 0;
                	 strftime(buffer_date, 10, "%y%m%d", localtime(&seconds));
                	 serial.printf(" New Day %s\n%s ", buffer_date, buffer);
             	 }


             	 for (i=0; i<MAX_PROBES; i++) {
             		 int temp;
             		 temp = seq[i];
             		 if(temp > devices_found){
             			serial.printf("-00.00 ");
             		 } else {
             			serial.printf("%2.2f ",probe[seq[i]]->temperature('c'));
             		 }
             	 }

             	 serial.printf(" : %6.1f %5d\n", meter_rate, clean_pulses);
             	 i=0;
             	 while(i<devices_new){
             		 serial.printf("%2.2f ",probe[seq_new[i]]->temperature('c'));
             		 i++;
             	 }
        	 }

        	 // Idle loop processing of meter reading
             if (meter_pulses){
            	 meter_rate = 3600000.0 / meter_interval;
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

