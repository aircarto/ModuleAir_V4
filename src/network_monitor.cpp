#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include "network_monitor.h"
#include "display.h"
#include "logger.h"

// Probe targets for "is the internet reachable?". Literal IPs — no DNS lookup
// needed, so we test purely the IP-routing layer. Port choice matters: the
// old single probe on 1.1.1.1:80 gave FALSE NEGATIVES on networks that filter
// outbound port 80 or 1.1.1.1 itself (seen in the field: badge stuck on "no
// internet" while the HTTPS POSTs went through fine). So we aim at 443
// (Cloudflare answers there), with a fallback on 8.8.8.8:53 (Google DNS) — an
// independent host AND port; a network would have to filter both to produce a
// false negative.
static const char* INTERNET_PROBE_HOST = "1.1.1.1";
static const uint16_t INTERNET_PROBE_PORT = 443;
static const char* INTERNET_FALLBACK_HOST = "8.8.8.8";
static const uint16_t INTERNET_FALLBACK_PORT = 53;

// Probe target for "is our data server reachable?". A plain TCP connect on
// :443 is enough — we don't speak TLS here, we just want SYN/ACK to confirm
// the server is listening. If this fails while internet probe succeeds,
// it's a server-side issue, not a network issue.
static const char* SERVER_PROBE_HOST = "gestion.aircarto.fr";
static const uint16_t SERVER_PROBE_PORT = 443;

// Cadence. 15 s is the sweet spot used by Tasmota & ESPHome — reactive
// enough that state changes show up quickly, low enough that we're not
// hammering the network or the upstream hosts.
static const uint32_t PROBE_INTERVAL_MS = 15000;

// Per-probe TCP connect timeout. Keep it short so an unreachable host
// doesn't block the whole task for too long. With every probe failing,
// worst-case task tick is 3 * PROBE_TIMEOUT_MS (server + the two internet
// probes) — still well under the 15 s cadence.
static const uint16_t PROBE_TIMEOUT_MS = 2500;

// Test reachability via TCP SYN/ACK. We open the connection, immediately
// close it. Returns true if the 3-way handshake completed within timeoutMs.
static bool tcpProbe(const char* host, uint16_t port, uint16_t timeoutMs) {
  WiFiClient c;
  bool ok = c.connect(host, port, timeoutMs);
  c.stop();
  return ok;
}

static NetStatus probeNetworkOnce() {
  if (WiFi.status() != WL_CONNECTED) return NET_OFFLINE;

  // Server first: if it answers, internet is proven by the same probe — a
  // reachable server is the safety net against a filtered internet probe.
  // Bonus: the nominal case costs ONE probe instead of two.
  if (tcpProbe(SERVER_PROBE_HOST, SERVER_PROBE_PORT, PROBE_TIMEOUT_MS)) return NET_OK;

  // Server unreachable: classify "server down" vs "no internet" with the
  // literal-IP probes (443 first, then the 8.8.8.8:53 fallback).
  bool internet = tcpProbe(INTERNET_PROBE_HOST, INTERNET_PROBE_PORT, PROBE_TIMEOUT_MS)
               || tcpProbe(INTERNET_FALLBACK_HOST, INTERNET_FALLBACK_PORT, PROBE_TIMEOUT_MS);
  return internet ? NET_NO_SERVER : NET_NO_INTERNET;
}

static const char* statusLabel(NetStatus s) {
  switch (s) {
    case NET_OFFLINE:     return "OFFLINE";
    case NET_OK:          return "OK";
    case NET_NO_INTERNET: return "NO_INTERNET";
    case NET_NO_SERVER:   return "NO_SERVER";
  }
  return "?";
}

static void networkMonitorTask(void *param) {
  // Sentinel value so the FIRST probe always triggers a log + badge update,
  // even if the result happens to match the (initial) NET_OFFLINE state.
  int last = -1;
  for (;;) {
    NetStatus s = probeNetworkOnce();
    if ((int)s != last) {
      if (last == -1) {
        Logger.printf("[NetMon] initial: %s\n", statusLabel(s));
      } else {
        Logger.printf("[NetMon] %s -> %s\n", statusLabel((NetStatus)last), statusLabel(s));
      }
      displaySetNetStatus(s);
      last = (int)s;
    }
    vTaskDelay(pdMS_TO_TICKS(PROBE_INTERVAL_MS));
  }
}

void networkMonitorInit() {
  Logger.println("[NetMon] init");
  // Pinned to core 1 alongside the Arduino main loop. 4 KB stack — TCP
  // probes don't allocate much, but WiFiClient + lwIP has some overhead.
  xTaskCreatePinnedToCore(networkMonitorTask, "netmon", 4096, NULL, 1, NULL, 1);
}
