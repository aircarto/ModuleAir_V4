#ifndef DATA_SENDER_H
#define DATA_SENDER_H

enum SendResult { SEND_OK, SEND_NO_INTERNET, SEND_SERVER_DOWN };

// A appeler une fois au setup(), apres settingsInit(). Deux roles :
//   - binaire de provisioning (-DFACTORY_ATMOSUD=1) : stampe la NVS au 1er boot
//   - tous les binaires : migre l'ancien flag "server/atmosud" == 1 vers le
//     stamp (capteurs AtmoSud deployes avant l'architecture write-once)
void dataSenderInit();

SendResult dataSenderSend();

#ifdef ATMOSUD_SERVER_URL
// True si CE capteur doit envoyer vers AtmoSud. Seul critere : le stamp NVS
// (provisioning usine via l'env atmosud_provision, ou migration de l'ancien
// flag). Cache au 1er appel. Jamais modifiable depuis l'UI ni par OTA.
bool dataSenderIsAtmosudDevice();
#endif

#endif
