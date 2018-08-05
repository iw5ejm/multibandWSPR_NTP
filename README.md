# multibandWSPR_NTP
Multiband WSPR beacon with Si5351A and ESP8266 for Wi-Fi NTP update. No gps needed. 
Run on arduino zero compatible board

Forked from NT7S code
https://gist.github.com/NT7S/2b5555aa28622c1b3fcbc4d7c74ad926

This sketch transmit on three band using only two clock output from Si5351A board.
In the example 40 and 30 meter are alternated every 10 minutes.

Work on arduino nano linked to Si5351A and ESP8266 module via serial port.
On ESP8266 run AT firmware.
