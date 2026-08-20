/*
 * ArtNet / sACN → DMX Decoder
 * Hardware: generic ESP32 (ESP32-D0WD-V3) + auto-direction RS485 module (VCC/TXD/RXD/GND) on UART2
 * v1.5.0
 *
 * Features:
 *   - Art-Net (UDP 6454) universe → DMX512
 *   - sACN / E1.31 (UDP 5568) universe → DMX512
 *   - Web config UI (WiFi, universe, priority, IP)
 *   - OTA updates (ArduinoOTA + web upload)
 *   - RDM scaffolding: correct half-duplex, core PIDs
 *     (DEVICE_INFO, DMX_START_ADDRESS, IDENTIFY, SUPPORTED_PARAMETERS)
 *     Discovery (DUB) is stubbed — see rdm.h
 *
 * Wiring:
 *   GPIO17 → module TXD  (UART2 TX; this module labels pins from its own
 *   GPIO16 ← module RXD  (UART2 RX;  UART side — wire straight across, not crossed)
 *   No DE/RE pin — this module switches direction automatically in hardware.
 *
 * PlatformIO board: esp32dev
 */

// ─── Includes ────────────────────────────────────────────────────────────────
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <AsyncUDP.h>
#include <WebServer.h>
#include <ArduinoOTA.h>
#include <Preferences.h>
#include "artnet.h"
#include "sacn.h"
#include "dmx.h"
#include "rdm.h"
#include "webui.h"
#include "config.h"

// ─── Globals ─────────────────────────────────────────────────────────────────
Config        cfg;
Preferences   prefs;
AsyncUDP      udpArtnet;
AsyncUDP      udpSacn;
WebServer     server(80);
uint8_t       dmxBuffer[513];   // slot 0 unused; slots 1-512
volatile bool dmxDirty = false;

// ─── Setup ───────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  Serial.println("\n[boot] ArtNet/sACN→DMX v1.5.0");

  cfg.load(prefs);
  dmxInit();

  // WiFi
  WiFi.setHostname(cfg.hostname);
  if (cfg.dhcp) {
    WiFi.begin(cfg.ssid, cfg.password);
  } else {
    WiFi.config(cfg.staticIP, cfg.gateway, cfg.subnet);
    WiFi.begin(cfg.ssid, cfg.password);
  }

  Serial.print("[wifi] connecting");
  uint8_t retries = 0;
  while (WiFi.status() != WL_CONNECTED && retries < 40) {
    delay(500); Serial.print("."); retries++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("\n[wifi] connected: %s\n", WiFi.localIP().toString().c_str());
    Serial.printf("[wifi] gateway: %s  subnet: %s\n",
                   WiFi.gatewayIP().toString().c_str(),
                   WiFi.subnetMask().toString().c_str());
  } else {
    Serial.println("\n[wifi] failed — starting AP");
    WiFi.mode(WIFI_AP);
    WiFi.softAP("ArtDMX-Config", "artdmx123");
    Serial.printf("[ap] %s\n", WiFi.softAPIP().toString().c_str());
  }

  // Art-Net
  if (udpArtnet.listen(ARTNET_PORT)) {
    udpArtnet.onPacket([](AsyncUDPPacket pkt) {
      artnetProcess(pkt.data(), pkt.length(), dmxBuffer, cfg.universe, &dmxDirty);
      webuiCountPacket("artnet");
    });
    Serial.printf("[artnet] listening on :%d\n", ARTNET_PORT);
  }

  // sACN
  if (udpSacn.listenMulticast(sacnMulticast(cfg.universe), SACN_PORT)) {
    udpSacn.onPacket([](AsyncUDPPacket pkt) {
      sacnProcess(pkt.data(), pkt.length(), dmxBuffer, cfg.universe, &dmxDirty);
      webuiCountPacket("sacn");
    });
    Serial.printf("[sacn] listening multicast universe %d\n", cfg.universe);
  }

  // Web UI
  webuiSetup(server, cfg, prefs, dmxBuffer, &dmxDirty);
  server.begin();

  // OTA
  ArduinoOTA.setHostname(cfg.hostname);
  ArduinoOTA.setPassword(cfg.otaPassword);
  ArduinoOTA.onStart([]() { Serial.println("[ota] start"); });
  ArduinoOTA.onEnd([]()   { Serial.println("\n[ota] done"); });
  ArduinoOTA.onError([](ota_error_t e) {
    Serial.printf("[ota] error %u\n", e);
  });
  ArduinoOTA.begin();

  Serial.println("[boot] ready");
}

// ─── Loop ────────────────────────────────────────────────────────────────────
void loop() {
  ArduinoOTA.handle();
  server.handleClient();

  // Recompute fps/pps on a fixed schedule — webuiUpdateMetrics() only
  // guards on "has 1000ms passed", so relying solely on HTTP polling
  // (irregular, tab-throttling-dependent) to trigger it produced bogus
  // spikes (e.g. fps reading 8194 after a slow poll averaged over several
  // real seconds instead of one).
  webuiUpdateMetrics();

  // DMX fixtures expect a continuous signal, not just one frame per change —
  // send at a steady ~40Hz (23ms), the standard DMX512 refresh rate, rather
  // than only when the buffer changes.
  static uint32_t lastDmxSend = 0;
  uint32_t now = millis();
  if (now - lastDmxSend >= 23) {
    lastDmxSend = now;
    dmxDirty = false;
    dmxSend(dmxBuffer, 512);
    webuiCountDmxFrame();
  }
}

