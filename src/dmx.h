/*
 * dmx.h — DMX512 output via UART2 + auto-direction RS485 module
 * v1.3.0
 *
 * DMX frame: BREAK (88µs LOW), MAB (8µs HIGH), start code (0x00), 512 slots
 *
 * This board only exposes VCC/TXD/RXD/GND — no DE/RE pin. Direction
 * (transmit vs receive) is switched automatically on-board based on line
 * activity, so the MCU just talks plain UART; there is nothing to drive
 * from software.
 *
 * Pin assignments for generic ESP32 (ESP32-D0WD-V3):
 *   GPIO17 = TX2 → module TXD (this module labels pins from its own
 *   GPIO16 = RX2 ← module RXD  UART side, not the MCU side — wire straight across)
 */
#pragma once
#include <Arduino.h>
#include <driver/uart.h>

// ─── Pin config ──────────────────────────────────────────────────────────────
#define DMX_UART        UART_NUM_2
#define DMX_TX_PIN      17
#define DMX_RX_PIN      16
#define DMX_BAUD        250000
#define DMX_BREAK_US    88
#define DMX_MAB_US      8

// ─── Init ────────────────────────────────────────────────────────────────────
inline void dmxInit() {
  uart_config_t cfg = {
    .baud_rate  = DMX_BAUD,
    .data_bits  = UART_DATA_8_BITS,
    .parity     = UART_PARITY_DISABLE,
    .stop_bits  = UART_STOP_BITS_2,
    .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
    .source_clk = UART_SCLK_APB,
  };
  uart_driver_install(DMX_UART, 1024, 0, 0, nullptr, 0);
  uart_param_config(DMX_UART, &cfg);
  uart_set_pin(DMX_UART, DMX_TX_PIN, DMX_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
  Serial.println("[dmx] init ok");
}

// ─── Send frame ──────────────────────────────────────────────────────────────
// slots[0] = start code (0x00 for DMX), slots[1..512] = channel data
inline void dmxSend(const uint8_t* slots, uint16_t count) {
  // BREAK: set UART TX low by temporarily switching to GPIO
  uart_set_line_inverse(DMX_UART, UART_SIGNAL_TXD_INV); // drive line LOW
  delayMicroseconds(DMX_BREAK_US);
  uart_set_line_inverse(DMX_UART, 0);                    // restore HIGH
  delayMicroseconds(DMX_MAB_US);

  // Start code + slot data  (slot 0 = 0x00 DMX start code)
  uint8_t startCode = 0x00;
  uart_write_bytes(DMX_UART, (const char*)&startCode, 1);
  uart_write_bytes(DMX_UART, (const char*)(slots + 1), count); // slots 1-512
  uart_wait_tx_done(DMX_UART, pdMS_TO_TICKS(10));
}

// ─── RDM helpers (used by rdm.h) ─────────────────────────────────────────────
// No DE pin on this module — direction switches automatically in hardware.
// Kept as functions so rdm.h's call sites don't change; dmxSetReceive()
// just waits for pending TX to flush before the caller starts listening.
inline void dmxSetReceive() {
  uart_wait_tx_done(DMX_UART, pdMS_TO_TICKS(5));
}

inline void dmxSetTransmit() {
  // no-op — module re-asserts transmit automatically as soon as we drive TX
}

inline void dmxSendRaw(const uint8_t* data, size_t len) {
  uart_write_bytes(DMX_UART, (const char*)data, len);
  uart_wait_tx_done(DMX_UART, pdMS_TO_TICKS(10));
}

inline int dmxReadByte(uint32_t timeoutMs = 5) {
  uint8_t b;
  int n = uart_read_bytes(DMX_UART, &b, 1, pdMS_TO_TICKS(timeoutMs));
  return (n == 1) ? b : -1;
}
