#!/bin/bash

is_ip_reachable() {
    local ip_address=$1
    local port=$2
    local timeout=3

    # 尝试连接到指定IP地址和端口
    if exec 3<> /dev/tcp/$ip_address/$port; then
        echo "IP address $ip_address is reachable."
        exec 3>&-
        return 0
    else
        echo "Error: IP address $ip_address is not reachable."
        exec 3>&-
        return 1
    fi
}

select_firmware_path() {
    local board_type=$1

    if [ "$board_type" == "esp8266" ] || [ "$board_type" == "esp32" ]; then
        echo ".pio/build/adafruit_feather_esp32_v2/firmware.bin"
    else
        echo "Invalid board type. Please choose 'esp8266' or 'esp32'."
        exit 1
    fi
}

update_firmware() {
    local server_ip=$1
    local board_type=$2

    # 检查IP地址是否可达
    if ! is_ip_reachable "$server_ip" 80; then
        echo "Exiting script. IP address is not reachable."
        exit 1
    fi

    esp_ip="$server_ip"
    local_firmware_path=$(select_firmware_path "$board_type")

	cp $local_firmware_path ./
    echo "Local firmware path: $local_firmware_path"

    # 构建固件上传的请求
    url="http://$esp_ip/update"  # 使用端口80进行上传

    echo "Uploading firmware to $board_type at $esp_ip..."

    # 开始上传
    start_time=$(date +%s)
    response=$(curl -X POST "$url" --max-time 50  -F "firmware=@$local_firmware_path" -v -s -w "%{http_code}"  --output /dev/null)
    
    # 上传完成后的处理
    if [ "$response" == "200" ]; then
        elapsed_time=$(( $(date +%s) - start_time ))
        echo "Firmware uploaded successfully to $board_type at $esp_ip"
        echo "Total time: $elapsed_time s"

        # 此处添加 ESP8266/ESP32 升级固件的代码，例如使用 ESP8266HTTPUpdate 或 ESP32HTTPUpdate 库
        # 注意：这部分代码需要在 Arduino IDE 中运行

    else
        echo "Error uploading firmware to $board_type at $esp_ip. Status code: $response"
    fi
}

if [ "$#" -ne 2 ]; then
    echo "Usage: $0 <board_type> <esp_ip>"
    exit 1
fi

board_type=$1
esp_ip=$2

update_firmware "$esp_ip" "$board_type"

