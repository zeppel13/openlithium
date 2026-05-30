/* (c) Sebastian Kind 30.05.2026
 * infolithium_fake_pico.ino
 *
 * fakes a Sony InfoLithium battery on a Pi Pico so the camera boots
 * from a lab supply without complaining about the battery. 
 * Genuine 8V to infolithium adapter hehe:w
*
 * wiring:
 *   GP2 reads the bus (always input, never drives)
 *   GP3 goes to the gate of a 2N7000 n-fet
 *     drain on the bus, source to GND
 *   GP3 high = fet on = bus low
 *   GP3 low  = fet off = bus floats high (camera pullup)
 *
 * protocol: 490us/bit NRZ LSB first, frame is [CMD1][CMD2][RESP1][RESP2]
 * camera sends everything, we only drive the data bits of RESP1 and RESP2
 * and sync up to the cameras sync pulses
 */

#include "response_table.h"

#define BUS_PIN   2
#define DRIVE_PIN 3

#define BIT_US          490
#define HALF_BIT_US     245
#define PREAMBLE_US   10000
#define INTERBYTE_US   5000
#define INTERFRAME_US 20000
#define DEAD_US       250000

#define BUS_LOW()    digitalWrite(DRIVE_PIN, HIGH)
#define BUS_FLOAT()  digitalWrite(DRIVE_PIN, LOW)
#define BUS_READ()   digitalRead(BUS_PIN)

static uint8_t toggle_cnt[TABLE_SIZE];
static uint8_t c1_counter = 0;

static bool wait_for(int state, uint32_t timeout_us) {
    uint32_t t0 = micros();
    while (BUS_READ() != state) {
        if ((micros() - t0) >= timeout_us) return false;
    }
    return true;
}

// wait out the garbage between frames, returns false if camera went away
static bool resync_frame() {
    BUS_FLOAT();
    uint32_t last_low = micros();
    uint32_t hi_start = 0;
    bool was_high = (BUS_READ() == 1);
    if (was_high) hi_start = micros();

    while (true) {
        uint32_t now = micros();
        bool high = (BUS_READ() == 1);
        if (!high) {
            last_low = now;
            was_high = false;
        } else {
            if (!was_high) { was_high = true; hi_start = now; }
            if ((now - hi_start) >= INTERFRAME_US) return true;
        }
        if ((micros() - last_low) >= DEAD_US) {
            Serial.println("[DEAD] camera gone");
            return false;
        }
    }
}

// call at the falling edge of the start bit, reads 8 bits LSB first
static uint8_t recv_byte() {
    delayMicroseconds(BIT_US + HALF_BIT_US);
    uint8_t val = 0;
    for (int i = 0; i < 8; i++) {
        if (BUS_READ()) val |= (1 << i);
        delayMicroseconds(BIT_US);
    }
    return val;
}

// call one bit period after the camera's start bit falling edge
static void send_byte(uint8_t val) {
    for (int i = 0; i < 8; i++) {
        if (val & (1 << i)) BUS_FLOAT();
        else                 BUS_LOW();
        delayMicroseconds(BIT_US);
    }
    BUS_FLOAT();
}

static void detect_preamble() {
    int preambles = 0;
    uint32_t last_print = millis();

    while (BUS_READ() == 0);

    while (preambles < 2) {
        if ((millis() - last_print) >= 1000) {
            digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
            Serial.printf("[WAIT] GP%d=%s pre=%d\n", BUS_PIN, BUS_READ() ? "H" : "L", preambles);
            last_print = millis();
        }

        if (BUS_READ() != 0) continue;

        uint32_t t0 = micros();
        while (BUS_READ() == 0);
        uint32_t dur = micros() - t0;

        if (dur >= PREAMBLE_US) {
            preambles++;
            Serial.printf("[PRE] %d: %lu us\n", preambles, dur);
        } else if (dur > 200) {
            preambles = 0;
        }
    }
}

static void run_session() {
    uint32_t frame_num  = 0;
    uint8_t  cmd1       = 0;
    uint8_t  cmd2       = 0;
    uint8_t  resp1      = 0xFF;
    uint8_t  resp2_dom  = 0xFF;
    uint8_t  resp2_conf = 0xFF;
    uint8_t  resp2      = 0xFF;
    uint8_t  tbl_idx    = 0xFF;
    bool     found      = false;
    bool     do_confirm = false;
    uint8_t  fail_step  = 0;

    memset(toggle_cnt, 0, sizeof(toggle_cnt));
    c1_counter = 0;

    while (true) {

        if (!wait_for(0, 200000UL)) {
            Serial.println("[WARN] CMD1 timeout");
            return;
        }
        cmd1 = recv_byte();
        fail_step = 0;

        if (!wait_for(1, INTERBYTE_US)) { fail_step = 1; goto resync; }
        if (!wait_for(0, 20000UL))      { fail_step = 2; goto resync; }
        cmd2 = recv_byte();

        resp1 = resp2_dom = resp2_conf = 0xFF;
        tbl_idx = 0xFF;
        found   = false;
        for (uint8_t i = 0; i < TABLE_SIZE; i++) {
            if (RESPONSE_TABLE[i].cmd2 == cmd2) {
                resp1      = RESPONSE_TABLE[i].resp1;
                resp2_dom  = RESPONSE_TABLE[i].resp2_dominant;
                resp2_conf = RESPONSE_TABLE[i].resp2_confirmed;
                tbl_idx    = i;
                found      = true;
                break;
            }
        }

        do_confirm = false;
        if (found) {
            toggle_cnt[tbl_idx]++;
            if (toggle_cnt[tbl_idx] >= 3) {
                do_confirm = true;
                toggle_cnt[tbl_idx] = 0;
            }
        }

        if (cmd2 == 0xC1) {
            if (do_confirm) { c1_counter = (c1_counter + 1) % 126; resp2 = c1_counter; }
            else            { resp2 = c1_counter | 0x80; }
        } else {
            resp2 = do_confirm ? resp2_conf : resp2_dom;
        }

        // wait for camera RESP1 start bit then drive data after one bit period
        if (!wait_for(1, INTERBYTE_US)) { fail_step = 3; goto resync; }
        if (!wait_for(0, 20000UL))      { fail_step = 4; goto resync; }
        { uint32_t t0 = micros(); while ((micros() - t0) < (uint32_t)BIT_US); }
        send_byte(resp1);

        // same for RESP2
        if (!wait_for(1, INTERBYTE_US)) { fail_step = 5; goto resync; }
        if (!wait_for(0, 20000UL))      { fail_step = 6; goto resync; }
        { uint32_t t0 = micros(); while ((micros() - t0) < (uint32_t)BIT_US); }
        send_byte(resp2);

        frame_num++;
        Serial.printf("[%4lu] CMD1=%02X CMD2=%02X RESP1=%02X RESP2=%02X%s\n",
                      frame_num, cmd1, cmd2, resp1, resp2,
                      do_confirm ? " DATA" : "");
        continue;

    resync:
        BUS_FLOAT();
        Serial.printf("[RESYNC] step=%d CMD1=%02X CMD2=%02X\n", fail_step, cmd1, cmd2);
        if (!resync_frame()) return;
    }
}

static void blink(int n, int on_ms = 100, int off_ms = 100) {
    for (int i = 0; i < n; i++) {
        digitalWrite(LED_BUILTIN, HIGH); delay(on_ms);
        digitalWrite(LED_BUILTIN, LOW);  delay(off_ms);
    }
}

void setup() {
    pinMode(BUS_PIN, INPUT);
    pinMode(DRIVE_PIN, OUTPUT);
    digitalWrite(DRIVE_PIN, LOW);
    pinMode(LED_BUILTIN, OUTPUT);
    blink(3, 80, 80);

    Serial.begin(115200);
    uint32_t t0 = millis();
    while (!Serial && (millis() - t0) < 2000);
    delay(100);
    Serial.println("InfoLithium fake battery");
}

void loop() {
    Serial.println("[WAIT] waiting for preamble");
    detect_preamble();
    Serial.println("[OK] got preamble");
    digitalWrite(LED_BUILTIN, HIGH);
    run_session();
    digitalWrite(LED_BUILTIN, LOW);
    Serial.println("[END]");
    blink(2, 200, 100);
}
