#ifndef NETWORK_MONITOR_H
#define NETWORK_MONITOR_H

// Background task that periodically probes:
//   1. WiFi association (event-driven, free)
//   2. Internet reachability (TCP connect to a public IP — no DNS dependency)
//   3. Our data server reachability (TCP connect to gestion.aircarto.fr:443)
// and updates the matrix connectivity badge accordingly. Runs on core 1 so
// the probes (which can block briefly on timeouts) don't interfere with
// the matrix scan task or the WiFi stack on core 0.
//
// Pattern inspired by ESPHome's status_binary_sensor (each network layer
// is its own boolean check) combined with Tasmota's 15-30s poll cadence.
void networkMonitorInit();

// Remontee du resultat des VRAIS envois de donnees (data_sender). La sonde
// TCP ne voit que le chemin reseau ; un module etrangle en interne (ex. plus
// assez de heap contigu pour le handshake TLS) envoie zero donnee avec un
// badge au vert. Apres 2 echecs consecutifs le badge force NET_NO_SERVER ;
// un succes le remet a NET_OK immediatement. A appeler uniquement pour une
// tentative reelle (internet deja verifie) — pas pour un envoi saute.
void networkMonitorReportSendResult(bool ok);

#endif
