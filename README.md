# Keyfob/WiFi-Button
Wemos D1 mini wireless keyfob with MQTT publishing

![keyf_combo](https://user-images.githubusercontent.com/96028811/162501529-a7a92796-8460-4d62-b336-825e914196ab.jpg)

Abstract

For using the Wemos D1 mini board as a one-button remote control, say, as a garage door opener, light switch, or panic button, it would be nice to have it switch on at the touch of a pushbutton, do its transmitting job (means: publish a message to a MQTT server), and turn off its own power once done, off really meaning off, without continuously draining the batteries. Such device is actually also known as a WiFi button and the Amazon Dash button is an example of it. The circuit that does the job is a power latch. The approach here is innovative in that it uses a voltage converter with Enable input for doing so. It is easier to assemble than the two transistors that are needed for a powerlatch whenever the circuit is running at regulated voltage whereas the battery voltage to be shut off is unregulated. Furthermore, the voltage converter is a buck-boost type, and allows our microcontroller to run from a battery voltage downto 2.7 V in boost mode, lower than its 3.3 V operating voltage .

Hardware

The description and layout are under http://heinerd.online.fr/elektronik/keyfob.php3
In summary, the Enable function of a voltage converter is used to implement a power latch function that suspends system power after the code has been executed once. Quiescent current of the converter is far below the Deep sleep mode of the ESP8266, along with the USB/serial bridge of the D1 mini.
Further features are the storage of settings in the EEPROM and the configurablilty by the user via the USB/serial bridge. None of the settings are hardcoded. A battery warning is also implemented: the LED flashes twice for V_bat < 3.3 V. Below 2.9 V, it flashes 6 times and the MQTT publishing is cancelled.

![screenshot](https://user-images.githubusercontent.com/96028811/162502748-3fa2c3c2-bb7e-4187-b247-d712f99aa2bc.jpg)

This is the terminal screenshot at the first start. The EEPROM is still empty and the connections to WiFi and MQTT servers fail. While USB powered, the MCU instead of shutting down, enters the setup utility where the user can enter the settings and save them by typing eepromstore. Terminal settings are 9k6, 8N1, and commands are terminated by newline command (Ctrl-J in PuTTY) or 2 second timeout.
The program can be tested on a plain D1 mini board without the Keyfob shield. In this case, the voltage measuring fails, yielding 0 V, but the forced shutdown feature also fails. When assembled, the D1 mini can still be connected via its USB port for configuration or flashing without causing conflicts.
