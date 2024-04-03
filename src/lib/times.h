#ifndef __TIMES_H__
#define __TIMES_H__

void init_time();
void update_time();
String week();
int getRemainDays(int iYear1, int iMonth1, int iDay1, int iYear2, int iMonth2, int iDay2);  //1. 确保 日期1 < 日期2
void sync_http_time();
time_t getNtpTime();
void sync_udp_time();
#endif