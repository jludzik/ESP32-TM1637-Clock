#ifndef WIFI_CONNECTION_H
#define WIFI_CONNECTION_H

#include "esp_err.h"

#define WIFI_SUCCESS (1 << 0)
#define WIFI_FAILURE (1 << 1)


#define MAX_FAILURES 10 //Liczba prob ponownego laczenia

esp_err_t connect_wifi(void);

#endif // WIFI_CONNECTION_H
