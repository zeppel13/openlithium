#pragma once
#include <stdint.h>

// (c) Sebastian Kind 30.05.2026
// response table from protocol_and_response_table.txt
// dominant = bit7 set, sent most of the time
// confirmed = bit7 clear, sent every ~4th frame

struct RegEntry {
    uint8_t cmd2;
    uint8_t resp1;
    uint8_t resp2_dominant;
    uint8_t resp2_confirmed;
    const char* label;
};

#define RESP1_FALLBACK  0xFF
#define RESP2_FALLBACK  0xFF

static const RegEntry RESPONSE_TABLE[] = {
    // cmd2   resp1   dom    conf   label
    // static identity bytes, never change 
    // thes names are just guestimaed, they can be anything. 
    { 0xC3,   0xF0,   0xA2,  0x21,  "model_id_1" },
    { 0xC4,   0xF3,   0xA6,  0x2A,  "model_id_2" },
    { 0xC5,   0x9F,   0xA7,  0x47,  "model_id_3" },
    { 0xDF,   0xFF,   0xCD,  0x5C,  "rated_cap_1" },
    { 0xEF,   0xFF,   0xDD,  0x42,  "rated_cap_2" },
    { 0xF7,   0x55,   0xFB,  0x55,  "sync_pattern" },
    { 0xFC,   0x55,   0xF0,  0x70,  "rated_cap_3" },
    { 0xFA,   0xDF,   0xF8,  0x78,  "reg_FA" },
    { 0xFB,   0xEF,   0xC9,  0x49,  "reg_FB" },
    // dynamic registers, fake reasonable values
    // we might find the actual battery info *someday* lol
    { 0xC0,   0xFF,   0xA2,  0x28,  "charge_cycle" },
    { 0xC1,   0xFF,   0xA3,  0x00,  "heartbeat" }, // overridden in sketch
    { 0xC7,   0xFF,   0xA5,  0x39,  "temp" },
    { 0xCB,   0xFF,   0xA9,  0x2C,  "SOC_pct" },
    { 0xF0,   0xFF,   0xD2,  0x52,  "voltage" },
    // ping
    { 0xFF,   0xF0,   0xEE,  0x6E,  "ping" },
};

#define TABLE_SIZE (sizeof(RESPONSE_TABLE) / sizeof(RESPONSE_TABLE[0]))

inline bool lookup(uint8_t cmd2, uint8_t &resp1, uint8_t &resp2_dom, uint8_t &resp2_conf) {
    for (uint8_t i = 0; i < TABLE_SIZE; i++) {
        if (RESPONSE_TABLE[i].cmd2 == cmd2) {
            resp1       = RESPONSE_TABLE[i].resp1;
            resp2_dom   = RESPONSE_TABLE[i].resp2_dominant;
            resp2_conf  = RESPONSE_TABLE[i].resp2_confirmed;
            return true;
        }
    }
    resp1 = resp2_dom = resp2_conf = RESP1_FALLBACK;
    return false;
}
