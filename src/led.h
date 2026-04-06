#ifndef LED_H
#define LED_H

void ledInit();
void ledUpdate();
void ledTriggerPulse();
void ledSetApMode(bool ap);
void ledStartConnectedAnim();
void ledSetNoInternet(bool noInternet);
void ledSetServerDown(bool serverDown);
void ledSetWifiLost(bool lost);
void ledSetEnabled(bool enabled);
bool ledIsEnabled();
void ledSetBrightness(uint8_t brightness);
uint8_t ledGetBrightness();

#endif
