#pragma once
inline bool fakeMqttNtpEnabled=false;
inline unsigned fakeMqttNtpRequests=0;
inline bool esp_sntp_enabled(){return fakeMqttNtpEnabled;}
inline void configTime(long,int,const char *,const char *){fakeMqttNtpEnabled=true;++fakeMqttNtpRequests;}
