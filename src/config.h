/*
 * config.h — persistent configuration
 * v1.0.0
 */
#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include <IPAddress.h>

struct Config {
  char     ssid[64]       = "";
  char     password[64]   = "";
  char     hostname[32]   = "artdmx";
  char     otaPassword[32]= "artdmx";
  uint16_t universe       = 0;      // Art-Net universe 0-32767
  uint8_t  sacnPriority   = 100;
  bool     dhcp           = true;
  IPAddress staticIP      = IPAddress(192,168,0,123);
  IPAddress gateway       = IPAddress(192,168,0,1);
  IPAddress subnet        = IPAddress(255,255,255,0);

  // RDM device config
  uint16_t dmxStartAddress = 1;
  bool     rdmIdentify     = false;
  char     rdmLabel[33]    = "ArtDMX Node";

  void load(Preferences& prefs) {
    prefs.begin("artdmx", true);
    prefs.getString("ssid",     ssid,     sizeof(ssid));
    prefs.getString("pass",     password, sizeof(password));
    prefs.getString("hostname", hostname, sizeof(hostname));
    prefs.getString("otapwd",   otaPassword, sizeof(otaPassword));
    universe       = prefs.getUShort("universe",  0);
    sacnPriority   = prefs.getUChar("sacnpri",    100);
    dhcp           = prefs.getBool("dhcp",         true);
    dmxStartAddress= prefs.getUShort("dmxstart",  1);
    prefs.getString("rdmlabel", rdmLabel, sizeof(rdmLabel));
    uint32_t ip = prefs.getUInt("staticip", IPAddress(192,168,0,123));
    uint32_t gw = prefs.getUInt("gateway",  IPAddress(192,168,0,1));
    uint32_t sn = prefs.getUInt("subnet",   IPAddress(255,255,255,0));
    staticIP = IPAddress(ip);
    gateway  = IPAddress(gw);
    subnet   = IPAddress(sn);
    prefs.end();
    if (strlen(hostname) == 0) strlcpy(hostname, "artdmx", sizeof(hostname));
  }

  void save(Preferences& prefs) {
    prefs.begin("artdmx", false);
    prefs.putString("ssid",     ssid);
    prefs.putString("pass",     password);
    prefs.putString("hostname", hostname);
    prefs.putString("otapwd",   otaPassword);
    prefs.putUShort("universe", universe);
    prefs.putUChar("sacnpri",   sacnPriority);
    prefs.putBool("dhcp",       dhcp);
    prefs.putUShort("dmxstart", dmxStartAddress);
    prefs.putString("rdmlabel", rdmLabel);
    prefs.putUInt("staticip",   (uint32_t)staticIP);
    prefs.putUInt("gateway",    (uint32_t)gateway);
    prefs.putUInt("subnet",     (uint32_t)subnet);
    prefs.end();
  }
};
