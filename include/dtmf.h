// include/dtmf.h
#ifndef DTMF_H
#define DTMF_H

#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include "secrets.h"


// === Hook: prueba de sistema (lo implementas en main.c) ===
#ifdef __cplusplus
extern "C" {
#endif
void alert_test_start(uint32_t ms);   // <- solo prototipo
#ifdef __cplusplus
}
#endif

#include <stdint.h>

#ifndef TEST_BUZZ_MS
#define TEST_BUZZ_MS 10000   // 10 s de prueba
#endif

#ifdef __cplusplus
extern "C" {
#endif
void alert_test_start(uint32_t ms);  
#ifdef __cplusplus
}
#endif

// Mapping de dígitos DTMF a ficheros WAV
static const char *dtmf_files[10] = {
    "0.wav",  
    "1.wav",   
    "2.wav",      
    "3.wav",           
    "4.wav",       
    "5.wav",        
    "6.wav",    
    "7.wav",  
    "8.wav",       
    "9.wav"  
};

// Comprueba si 'num' coincide con NUM1 o NUM2
static bool caller_authorized(const char *num) {
    return (strcmp(num, NUM1) == 0) || (strcmp(num, NUM2) == 0);
}

#endif // DTMF_H
