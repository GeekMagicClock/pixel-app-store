#!/bin/bash
ip=$1
echo $ip
#exit

#pio run && ./upload.sh esp32 192.168.3.14
cd test/web/ && ./rebuild_web.sh && cd - && pio run && ./upload.sh esp32 $ip
#cd test/web/ && ./rebuild_web.sh && cd - && pio run && ./upload.sh esp32 192.168.123.129
#pio run && ./upload.sh esp32 192.168.123.130
