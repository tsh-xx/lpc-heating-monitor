lpc-heating-monitor
===================

Heating monitor and control system for LPC1769 dev board

Uses the LPC-xpresso dev board and the mbed and CMSIS libraries.

Libraries from mbed.com:
=======================
mbed-export rev 0, 22 Feb 2012
DS18S20     rev 1, 15 Dec 2011
USBDevice   rev 0, 30 May 2012

Connect one-wire devices to mbed pin 9 (with active power)
Connect LED sense circuit to mbed pin 9
Connect USB device for control/capturing data

Temperature sensors use the on-board EEPROM to identify which position to report in.
This means that missing devices don't affect the logging.

Electricity consumption is logged using an IRQ to sample timer intervals between flashes.
At the moment, there is no filtering on any of the sensors.

Readings are printed every 30 sec, or 5 sec in verbose mode.



