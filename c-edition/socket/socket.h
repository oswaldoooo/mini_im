#pragma once
#include <stdint.h>
#include <stdio.h>
#include <sys/types.h>
int connect_tcp(const char*, const int);
int listen_tcp(const int port);
ssize_t write_to_client(int, uint8_t act_code, const u_char*);
const u_char* writeTo(uint8_t code, const u_char* content);
void milli_sleep(unsigned long);
