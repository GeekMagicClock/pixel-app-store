#!/bin/bash

# 初始化请求计数器
request_count=0

# 循环发送curl请求
while true; do
    # 发送curl请求并获取HTTP响应码
    response_code=$(curl -s -o /dev/null -w "%{http_code}" --proxy http://127.0.0.1:7890 "https://query1.finance.yahoo.com/v8/finance/chart/nflx?interval=1m&range=84m")

    # 增加请求计数
    ((request_count++))

    # 打印当前的HTTP响应码
    echo "$request_count HTTP Response Code: $response_code"

    # 如果响应码是429，则终止循环
    if [ "$response_code" == "429" ]; then
        break
    fi

    if [ "$response_code" == "403" ]; then
        break
    fi
done

# 输出统计数据
echo "Total number of requests sent: $request_count"

