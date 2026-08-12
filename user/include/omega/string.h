#ifndef OMEGA_USER_STRING_H
#define OMEGA_USER_STRING_H

#include <stdint.h>

uint64_t strlen(const char* value);
int strcmp(const char* left, const char* right);
void* memcpy(void* destination, const void* source, uint64_t count);
void* memset(void* destination, int value, uint64_t count);
int memcmp(const void* left, const void* right, uint64_t count);

#endif
