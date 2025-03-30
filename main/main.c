#include <stdio.h>
#include <stdbool.h>
#include <time.h>
#include <stdlib.h>
#include <sys/time.h>
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "inttypes.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_sntp.h"
#include "nvs_flash.h"
#include "tm1637.h"
#include "wifi-connection.h"

//-------------------------------------------------------
#define Buzzer 15               //Buzzer
#define SyncButton 23           //Button0
#define ModeButton 19           //Button1
#define UpButton 5              //Button2
#define DownButton 4            //Button3
#define ButtonsNumber 4         //Number of buttons
#define DelaySyncButton 5000    //ms
#define DelayModeButton 700     //ms
#define DelayUpDownButton 190   //ms
#define LED_CLK 21              //CLK pin TM1637
#define LED_DTA 22              //DTA pin TM1637
//--------------------------------------------------------

//ALARM GLOBAL VARIABLES
volatile uint8_t setAlarmMode = 0;
volatile bool alarmTrigered = false;
volatile bool setAlarmEnable = false;
volatile struct tm timeinfoAlarm = {
        .tm_hour = 00,
        .tm_min = 00,
        .tm_sec = 0
};

volatile uint16_t ButtonDelayCounter[ButtonsNumber] = {0,0,0,0};
//DelayButtonDebounceCountDown
void timer0_callback(void* param)
{
    for(size_t i=0; i<ButtonsNumber ;i++)
    {
        (ButtonDelayCounter[i] > 0) ? ButtonDelayCounter[i]-- : 0;
    }
}

volatile bool SyncButtonFlag = false;
//Button0
static void IRAM_ATTR SyncButton_handler(void* arg)
{
    if(ButtonDelayCounter[0] == 0)
    {
        SyncButtonFlag = true;
        ButtonDelayCounter[0] = DelaySyncButton;
    }

    gpio_isr_handler_add(SyncButton, SyncButton_handler, NULL);
    gpio_intr_enable(SyncButton);
}

volatile bool ModeButtonFlag = false;
//Button1
static void IRAM_ATTR ModeButton_handler(void* arg)
{
    if(ButtonDelayCounter[1] == 0)
    {
        ModeButtonFlag = true;
        
        ButtonDelayCounter[1] = DelayModeButton;
    }

    gpio_isr_handler_add(ModeButton, ModeButton_handler, NULL);
    gpio_intr_enable(ModeButton);
}

volatile bool UpButtonFlag = false;
//Button2
static void IRAM_ATTR UpButton_handler(void* arg)
{
    if(ButtonDelayCounter[2] == 0)
    {
        if((setAlarmMode>0)||(alarmTrigered==true))
        {
            UpButtonFlag = true;    
        }

        ButtonDelayCounter[2] = DelayUpDownButton;
    }

    gpio_isr_handler_add(UpButton, UpButton_handler, NULL);
    gpio_intr_enable(UpButton);
}

volatile bool DownButtonFlag = false;
//Button3
static void IRAM_ATTR DownButton_handler(void* arg)
{
    if(ButtonDelayCounter[3] == 0)
    {
        if((setAlarmMode>0)||(alarmTrigered==true))
        {
            DownButtonFlag = true;
        }
        ButtonDelayCounter[3] = DelayUpDownButton;
    }

    gpio_isr_handler_add(DownButton, DownButton_handler, NULL);
    gpio_intr_enable(DownButton);
}

TaskHandle_t waitingTaskHandle = NULL;
void waitingTask(void* arg)
{
    tm1637_led_t *lcd = (tm1637_led_t *)arg;

    while(1)
    {
        //Display animation
        uint8_t segData[] = {0b00000001, 0b00000010, 0b00000100, 0b00001000, 0b00010000, 0b00100000};
        for(unsigned long long i=0ULL; i < (6*sizeof(segData)) ;i++)
        {
            tm1637_set_segment_raw(lcd, 0, segData[i%6]);
            tm1637_set_segment_raw(lcd, 1, segData[i%6]);
            tm1637_set_segment_raw(lcd, 2, segData[i%6]);
            tm1637_set_segment_raw(lcd, 3, segData[i%6]);
            vTaskDelay(100 / portTICK_PERIOD_MS);
        }
    }
}

TaskHandle_t displayTimeTaskHandle = NULL;
void displayTimeTask(void* arg)
{
    tm1637_led_t *lcd = (tm1637_led_t *)arg;
    
    while (1)
    {
        time_t now = time(NULL);
        struct tm timeinfo;
        localtime_r(&now, &timeinfo);

        uint16_t timeNumber = 100*timeinfo.tm_hour + timeinfo.tm_min;
        for(uint8_t i=0; i<2 ;i++)
        {
            tm1637_set_number_lead_dot(lcd, timeNumber, true, (i%2 ? 0xFF : 0x00));
            vTaskDelay(500 / portTICK_PERIOD_MS);
        }

        //char strftime_buf[64];
        //strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
        //ESP_LOGI("TIME", "Current time: %s", strftime_buf);

        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

TaskHandle_t setAlarmTaskMinutesHandle = NULL;
void setAlarmTaskMinutes(void* arg)
{
    tm1637_led_t *lcd = (tm1637_led_t *)arg;

    //ALARM TIME number
    uint16_t timeNumberAlarm = 100*timeinfoAlarm.tm_hour + timeinfoAlarm.tm_min;

    while(1)
    {
        for(uint8_t i=0; i<2 ;i++)
        {
            if(i%2)
            {
                int readN = 0;
                while(readN < 100)
                {
                    tm1637_set_segment_number(lcd, 3, timeNumberAlarm % 10, true);
                    tm1637_set_segment_number(lcd, 2, (timeNumberAlarm / 10) % 10, true);
                    tm1637_set_segment_number(lcd, 1, (timeNumberAlarm / 100) % 10, true);
                    tm1637_set_segment_number(lcd, 0, (timeNumberAlarm / 1000) % 10, true);

                    timeNumberAlarm = 100*timeinfoAlarm.tm_hour + timeinfoAlarm.tm_min;
                    readN++;
                    vTaskDelay(10 / portTICK_PERIOD_MS);
                }  
            }
            else
            {
                int readN = 0;
                while(readN < 100)
                {
                    tm1637_set_segment_raw(lcd, 3, 0x00);
                    tm1637_set_segment_raw(lcd, 2, 0x00);
                    tm1637_set_segment_number(lcd, 1, (timeNumberAlarm / 100) % 10, true);
                    tm1637_set_segment_number(lcd, 0, (timeNumberAlarm / 1000) % 10, true);

                    timeNumberAlarm = 100*timeinfoAlarm.tm_hour + timeinfoAlarm.tm_min;
                    readN++;
                    vTaskDelay(4 / portTICK_PERIOD_MS);
                }
            }
        }
    }
}

TaskHandle_t setAlarmTaskHoursHandle = NULL;
void setAlarmTaskHours(void* arg)
{
    tm1637_led_t *lcd = (tm1637_led_t *)arg;

    //ALARM TIME number
    uint16_t timeNumberAlarm = 100*timeinfoAlarm.tm_hour + timeinfoAlarm.tm_min;

    while(1)
    {
        for(uint8_t i=0; i<2 ;i++)
        {
            if(i%2)
            {
                int readN = 0;
                while(readN < 100)
                {
                    tm1637_set_segment_number(lcd, 3, timeNumberAlarm % 10, true);
                    tm1637_set_segment_number(lcd, 2, (timeNumberAlarm / 10) % 10, true);
                    tm1637_set_segment_number(lcd, 1, (timeNumberAlarm / 100) % 10, true);
                    tm1637_set_segment_number(lcd, 0, (timeNumberAlarm / 1000) % 10, true);

                    timeNumberAlarm = 100*timeinfoAlarm.tm_hour + timeinfoAlarm.tm_min;
                    readN++;
                    vTaskDelay(10 / portTICK_PERIOD_MS);
                }
            }
            else
            {
                int readN = 0;
                while(readN < 100)
                {
                    tm1637_set_segment_number(lcd, 3, timeNumberAlarm % 10, true);
                    tm1637_set_segment_number(lcd, 2, (timeNumberAlarm / 10) % 10, true);
                    tm1637_set_segment_raw(lcd, 1, 0x80);
                    tm1637_set_segment_raw(lcd, 0, 0x00);

                    timeNumberAlarm = 100*timeinfoAlarm.tm_hour + timeinfoAlarm.tm_min;
                    readN++;
                    vTaskDelay(4 / portTICK_PERIOD_MS);
                }
            }
        }
    }
}

TaskHandle_t setAlarmTaskEnableHandle = NULL;
void setAlarmTaskEnable(void* arg)
{
    tm1637_led_t *lcd = (tm1637_led_t *)arg;
    
    while(1)
    {
        for(uint8_t i=0; i<2 ;i++)
        {
            if(i%2)
            {
                int readN = 0;
                while(readN < 100)
                {
                    if(setAlarmEnable)
                    {   
                        tm1637_set_segment_raw(lcd, 3, 0x76);
                        tm1637_set_segment_raw(lcd, 2, 0x40);
                        tm1637_set_segment_raw(lcd, 1, 0x38);
                        tm1637_set_segment_raw(lcd, 0, 0x77);   
                    }
                    else
                    {
                        tm1637_set_segment_raw(lcd, 3, 0x08);
                        tm1637_set_segment_raw(lcd, 2, 0x40);
                        tm1637_set_segment_raw(lcd, 1, 0x38);
                        tm1637_set_segment_raw(lcd, 0, 0x77);   
                    }

                    readN++;
                    vTaskDelay(10 / portTICK_PERIOD_MS);    
                }
            }
            else
            {
                int readN = 0;
                while(readN < 100)
                {
                    tm1637_set_segment_raw(lcd, 3, 0x00);
                    tm1637_set_segment_raw(lcd, 2, 0x40);
                    tm1637_set_segment_raw(lcd, 1, 0x38);
                    tm1637_set_segment_raw(lcd, 0, 0x77);   
                    
                    readN++;
                    vTaskDelay(4 / portTICK_PERIOD_MS);
                }
                
            }                    
        }
    }    
}

TaskHandle_t buzzerAlarmHandle = NULL;
void buzzerAlarm(void* arg)
{
    tm1637_led_t *lcd = (tm1637_led_t *)arg;

    while(1)
    {
        time_t now = time(NULL);
        struct tm timeinfo;
        localtime_r(&now, &timeinfo);

        uint16_t timeNumber = 100*timeinfo.tm_hour + timeinfo.tm_min;
    
        for(uint8_t i=0;i<2;i++)
        {
            if(i%2)
            {
                tm1637_set_number_lead_dot(lcd, timeNumber, true, 0xFF);

                gpio_set_level(Buzzer ,0);
            }
            else
            {
                tm1637_set_segment_raw(lcd, 0, 0x00);
                tm1637_set_segment_raw(lcd, 1, 0x00);
                tm1637_set_segment_raw(lcd, 2, 0x00);
                tm1637_set_segment_raw(lcd, 3, 0x00);

                gpio_set_level(Buzzer, 1);
            }
            ESP_LOGI("ALARM","ACTIVE!!!");

            vTaskDelay(500 / portTICK_PERIOD_MS);
        }
    }
}

void resetAlarmTrigered(void* arg)
{
    vTaskDelay((60*1000) / portTICK_PERIOD_MS);    //60 000 ms = 60 s
    alarmTrigered = false;
    ESP_LOGI("ALARM","alarmTrigered = %d", alarmTrigered);
    vTaskDelete(NULL);
}


void timeSyncNotificationCallback(struct timeval* tv)
{
    ESP_LOGI("SNTP", "Time synchronized successfully!");
}

void nvsFlashInit(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret); 
}

void wifiInit(void)
{
    esp_err_t status = WIFI_FAILURE;

	status = connect_wifi();
	if (WIFI_SUCCESS != status)
	{
		ESP_LOGI("WIFI", "Failed to associate to AP");
		return;
	}
}

void sntpConnect(void)
{
    ESP_LOGI("SNTP", "Initializing SNTP...");
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "194.146.251.100");   //tempus1.gum.gov.pl
    sntp_set_time_sync_notification_cb(timeSyncNotificationCallback);
    esp_sntp_init();
}

void waitForSync(void)
{
    time_t now = 0;
    struct tm timeinfo = { 0 };

    while (timeinfo.tm_year < (2016 - 1900))
    {
        ESP_LOGI("SNTP", "Waiting for time synchronization...");
        vTaskDelay(pdMS_TO_TICKS(2000));
        time(&now);
        localtime_r(&now, &timeinfo);
    }

    ESP_LOGI("SNTP", "Time synchronized: %s", asctime(&timeinfo));
}

void app_main(void)
{
    //TM1637 init
    tm1637_led_t* lcd = tm1637_init(LED_CLK, LED_DTA);

    //start animation while waiting for connection download time
    xTaskCreate(*waitingTask, "waitingTask", 4096, lcd, 5, &waitingTaskHandle);

    //enable gpio interrupt
    gpio_install_isr_service(0);

    //Buzzer - output
    gpio_reset_pin(Buzzer);
    gpio_set_direction(Buzzer, GPIO_MODE_OUTPUT);
    gpio_set_level(Buzzer, 1);

    //SyncButton - input/interrupt posedge
    gpio_reset_pin(SyncButton);
    gpio_set_pull_mode(SyncButton, GPIO_PULLUP_ONLY);
    gpio_set_direction(SyncButton, GPIO_MODE_INPUT);

    gpio_set_intr_type(SyncButton, GPIO_INTR_POSEDGE);
    gpio_isr_handler_add(SyncButton, SyncButton_handler, NULL);
    gpio_intr_enable(SyncButton);

    //ModeButton - input/interrupt posedge
    gpio_reset_pin(ModeButton);
    gpio_set_pull_mode(ModeButton, GPIO_PULLUP_ONLY);
    gpio_set_direction(ModeButton, GPIO_MODE_INPUT);

    gpio_set_intr_type(ModeButton, GPIO_INTR_POSEDGE);
    gpio_isr_handler_add(ModeButton, ModeButton_handler, NULL);
    gpio_intr_enable(ModeButton);

    //UpButton - input/interrupt posedge
    gpio_reset_pin(UpButton);
    gpio_set_pull_mode(UpButton, GPIO_PULLUP_ONLY);
    gpio_set_direction(UpButton, GPIO_MODE_INPUT);

    gpio_set_intr_type(UpButton, GPIO_INTR_POSEDGE);
    gpio_isr_handler_add(UpButton, UpButton_handler, NULL);
    gpio_intr_enable(UpButton);

    //DownButton - input/interrupt posedge
    gpio_reset_pin(DownButton);
    gpio_set_pull_mode(DownButton, GPIO_PULLUP_ONLY);
    gpio_set_direction(DownButton, GPIO_MODE_INPUT);

    gpio_set_intr_type(DownButton, GPIO_INTR_POSEDGE);
    gpio_isr_handler_add(DownButton, DownButton_handler, NULL);
    gpio_intr_enable(DownButton);

    //timer0 init
    const esp_timer_create_args_t timer0_struct = {
        .name = "timer0",
        .callback = &timer0_callback
    };
    esp_timer_handle_t timer0_handler;
    esp_timer_create(&timer0_struct, &timer0_handler);
    esp_timer_start_periodic(timer0_handler, 1000);     //1 ms

    //set Poland time zone
    setenv("TZ", "CET-1CEST,M3.5.0/2,M10.5.0/3", 1);
    tzset();

    nvsFlashInit();
    wifiInit();

    //connect to server
    sntpConnect();

    //waiting for time sync
    waitForSync();
    
    //end of animation display
    vTaskDelete(waitingTaskHandle);

    //display time
    xTaskCreate(*displayTimeTask, "displayTimeTask", 4096, lcd, 5, &displayTimeTaskHandle);

    while(1)
    {
        if((SyncButtonFlag)&&(!setAlarmMode))
        {
            ESP_LOGI("SyncButton","Pressed! \n\r");
            vTaskDelete(displayTimeTaskHandle);
            xTaskCreate(*waitingTask, "waitingTask", 4096, lcd, 5, &waitingTaskHandle);

            sntpConnect();

            waitForSync();

            vTaskDelete(waitingTaskHandle);
            xTaskCreate(*displayTimeTask, "displayTimeTask", 4096, lcd, 5, &displayTimeTaskHandle);

            SyncButtonFlag = false;
        }

        if(ModeButtonFlag)
        {
            ESP_LOGI("ModeButton", "Pressed!");

            if(setAlarmMode == 0)           //NormalMode -> setAlarmModeHours
            {
                setAlarmMode++;
                vTaskDelete(displayTimeTaskHandle);
                xTaskCreate(*setAlarmTaskHours, "setAlarmTaskHours", 4096, lcd, 5, &setAlarmTaskHoursHandle);
                ESP_LOGI("ModeButton", "NormalMode -> setAlarmModeHours");
            }
            else if(setAlarmMode == 1)       //setAlarmModeHours -> setAlarmModeMinutes
            {
                setAlarmMode++;
                vTaskDelete(setAlarmTaskHoursHandle);
                xTaskCreate(*setAlarmTaskMinutes, "setAlarmTaskMinutes", 4096, lcd, 5, &setAlarmTaskMinutesHandle);
                ESP_LOGI("ModeButton", "setAlarmModeHours -> setAlarmModeMinutes");
            }
            else if(setAlarmMode == 2)      //setAlarmModeMinutes -> setAlarmModeEnable
            {
                setAlarmMode++;
                vTaskDelete(setAlarmTaskMinutesHandle);
                xTaskCreate(*setAlarmTaskEnable, "setAlarmTaskEnable", 4096, lcd, 5, &setAlarmTaskEnableHandle);
                ESP_LOGI("ModeButton", "setAlarmModeMinutes -> setAlarmModeEnable");
            }
            else if(setAlarmMode == 3)      //setAlarmModeEnable -> NormalMode
            {
                setAlarmMode = 0;
                vTaskDelete(setAlarmTaskEnableHandle);
                xTaskCreate(*displayTimeTask, "displayTimeTask", 4096, lcd, 5, &displayTimeTaskHandle);
                ESP_LOGI("ModeButton", "setAlarmModeEnable -> NormalMode");
            }
            
            ModeButtonFlag = false;
        }

        //setHoursUp
        if((UpButtonFlag)&&(setAlarmMode == 1))
        {
            timeinfoAlarm.tm_hour=(timeinfoAlarm.tm_hour < 23 ? timeinfoAlarm.tm_hour+1 : 0);
            UpButtonFlag = false;
        }

        //setMinutesUp
        if((UpButtonFlag)&&(setAlarmMode == 2))
        {
            timeinfoAlarm.tm_min=(timeinfoAlarm.tm_min < 59 ? timeinfoAlarm.tm_min+1 : 0);
            UpButtonFlag = false;
        }

        //setHoursDown
        if((DownButtonFlag)&&(setAlarmMode == 1))
        {
            timeinfoAlarm.tm_hour=(timeinfoAlarm.tm_hour > 0 ? timeinfoAlarm.tm_hour-1 : 23);
            DownButtonFlag = false;
        }

        //setMinutesDown
        if((DownButtonFlag)&&(setAlarmMode == 2))
        {
            timeinfoAlarm.tm_min=(timeinfoAlarm.tm_min > 0 ? timeinfoAlarm.tm_min-1 : 59);
            DownButtonFlag = false;
        }

        //setAlarmEnableUp
        if((DownButtonFlag)&&(setAlarmMode == 3))
        {
            setAlarmEnable=(!setAlarmEnable);
            DownButtonFlag = false;
        }

        //setAlarmEnableDown
        if((UpButtonFlag)&&(setAlarmMode == 3))
        {
            setAlarmEnable=(!setAlarmEnable);
            UpButtonFlag = false;
        }

        //Alarm
        if((setAlarmEnable)&&(setAlarmMode==0))
        {
            time_t now = time(NULL);
            struct tm timeinfo;
            localtime_r(&now, &timeinfo);

            if((timeinfoAlarm.tm_hour == timeinfo.tm_hour)&&(timeinfoAlarm.tm_min == timeinfo.tm_min)&&(!alarmTrigered))
            {
                alarmTrigered = true;
                vTaskDelete(displayTimeTaskHandle);
                xTaskCreate(*buzzerAlarm, "buzzerAlarm", 4096, lcd, 5, &buzzerAlarmHandle);

                while(1)
                {
                    if(DownButtonFlag||UpButtonFlag)
                    {
                        vTaskDelete(buzzerAlarmHandle);
                        xTaskCreate(*displayTimeTask, "displayTimeTask", 4096, lcd, 5, &displayTimeTaskHandle);
                        xTaskCreate(*resetAlarmTrigered, "resetAlarmTrigered", 1024, NULL, 5, NULL);
                        gpio_set_level(Buzzer, 1);
                        UpButtonFlag = false;
                        DownButtonFlag = false;
                        break;
                    }
                    vTaskDelay(10 / portTICK_PERIOD_MS);
                }

            }
        }

        vTaskDelay(10 / portTICK_PERIOD_MS);
    }

}