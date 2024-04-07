#!/bin/bash
#pio run && ./upload.sh esp32 192.168.3.14
cd test/web/ && ./rebuild_web.sh && cd - && pio run && ./upload.sh esp32 192.168.123.130
#pio run && ./upload.sh esp32 192.168.123.130
