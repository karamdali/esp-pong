#include <stdio.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/adc.h>
#include <ssd1306.h>
#include <math.h>
#include <time.h>
#include <esp_random.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <nvs_flash.h>


#define PI 3.14

#define MAX_SPEED 8 

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

#define paddle_WIDTH 16
#define paddle_HEIGHT 4

#define BALL_DIMENTION 4        //since the ball width equal its height it has one dimention 


#define UPDATE_LOCAL_paddle (1<<0)
#define UPDATE_BLUTOOTH_paddle (1<<1)
#define UPDATE_BALL_paddle (1<<2)



QueueHandle_t paddle_speed_queue;
QueueHandle_t paddle_postion;
QueueHandle_t ball_directions_queue;

SemaphoreHandle_t screen;

typedef struct 
{
    int x;
    int y;
}position_t;


SSD1306_t dev;


//paddle line graph
uint8_t paddle [] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};  //16x4 pixels white line



//Calculate the speed of the paddle according to joystick input reads

void paddleDirection(void * parameter){
    adc2_config_channel_atten(ADC2_CHANNEL_2,ADC_ATTEN_DB_12);
    int mid_point, joystick;
    adc2_get_raw(ADC2_CHANNEL_2,ADC_BITWIDTH_12,&mid_point);              //get the value of the mid point of the joystick
    printf("midpoint Analog value is : %d ",mid_point);
    int speeds[] = {0,1,2,3,4};                                           //how many pixels the paddle moves according to joystick value
    int analog_max = 4095;                                                //max value we get from the ADC
    int number_of_speeds = sizeof(speeds)/sizeof(speeds[0]);  
    int negative_range = (analog_max-mid_point)/number_of_speeds;
    int positive_range = mid_point/number_of_speeds;
    int chosen_speed = 0;
    int paddle_speed =0;                                                  //initail state
    paddle_speed_queue = xQueueCreate(1,sizeof(paddle_speed));
    while(1){
        vTaskDelay(20/portTICK_PERIOD_MS);
        adc2_get_raw(ADC2_CHANNEL_2,ADC_BITWIDTH_12,&joystick);

        if (joystick>=mid_point && joystick < analog_max){
            chosen_speed = (joystick - mid_point)/negative_range;
            paddle_speed = -speeds[chosen_speed];
        }else if (joystick<mid_point && joystick != 0 ){
            chosen_speed = (mid_point - joystick)/positive_range;
            paddle_speed = speeds[chosen_speed];
        }else if (joystick == 0){
            paddle_speed = speeds[number_of_speeds-1]; 
        }else if (joystick == 4095){
            paddle_speed = -speeds[number_of_speeds-1]; 
        }

        //xQueueSend(paddle_speed_queue,(void*)&paddle_speed,portMAX_DELAY);
        xQueueOverwrite(paddle_speed_queue,(void *)&paddle_speed);
    }
}



void paddleDrawer(void *parameter){
    int paddle_speed = 0;
    position_t local_paddle = {0};
    paddle_postion = xQueueCreate(1,sizeof(position_t));
    //initial postiotns
    local_paddle.x = (SCREEN_WIDTH-paddle_WIDTH)/2;
    local_paddle.y = SCREEN_HEIGHT-paddle_HEIGHT;
    
    //initail drawing
    xSemaphoreTake(screen,portMAX_DELAY);
    ssd1306_bitmaps(&dev, local_paddle.x, local_paddle.y, paddle, paddle_WIDTH, paddle_HEIGHT, false); 
    xSemaphoreGive(screen);
    //eraser
    uint8_t eraser[MAX_SPEED*paddle_HEIGHT] = {0};
    position_t old = {0};
    while (1)
    {
        vTaskDelay(20/portTICK_PERIOD_MS);
        //update loal paddle postion and drwa it then erace it from the previous postition and share    
        //xQueueReceive(paddle_speed_queue,(void *)&paddle_speed,portMAX_DELAY);      //get the paddle speed
        xQueuePeek(paddle_speed_queue,(void *)&paddle_speed,portMAX_DELAY);
        old.x = local_paddle.x;           //save paddle old postion
        local_paddle.x += paddle_speed;     //updating local paddle position_t

        if(local_paddle.x>(SCREEN_WIDTH-paddle_WIDTH)){     //ensure that paddle stays within screen boundiries
            local_paddle.x = SCREEN_WIDTH-paddle_WIDTH;
        }
        if (local_paddle.x<0){
            local_paddle.x =0;
        }

        xSemaphoreTake(screen,portMAX_DELAY);
        
        ssd1306_bitmaps(&dev, local_paddle.x, local_paddle.y, paddle, paddle_WIDTH, paddle_HEIGHT, false);   //drwaing the paddle in new location
        
        //erase any trace of the paddle in its old postion
        if(paddle_speed<0){    
            /*if(  (local_paddle.x+paddle_WIDTH)-SCREEN_WIDTH<=  MAX_SPEED        // (SCREEN_WIDTH-old.x+paddle_WIDTH-paddle_speed)<MAX_SPEED){       //at the screen boundiries*/
                ssd1306_bitmaps(&dev, local_paddle.x+paddle_WIDTH, local_paddle.y, eraser, MAX_SPEED/* SCREEN_WIDTH-(local_paddle.x+paddle_WIDTH)*/, paddle_HEIGHT, false);
           /* } else {  
                ssd1306_bitmaps(&dev, local_paddle.x+paddle_WIDTH   //(old.x+paddle_WIDTH-paddle_speed), local_paddle.y, eraser, MAX_SPEED, paddle_HEIGHT, false);  
            }  */
        }
        if(paddle_speed>0){ 
            if(local_paddle.x<MAX_SPEED){       //at the screen boundiries*/
                ssd1306_bitmaps(&dev, 0, local_paddle.y, eraser, MAX_SPEED, paddle_HEIGHT, false);
            }else{
                ssd1306_bitmaps(&dev, (local_paddle.x-MAX_SPEED), local_paddle.y, eraser, MAX_SPEED, paddle_HEIGHT, false);  
            }
        }
        xSemaphoreGive(screen);
        xQueueOverwrite(paddle_postion,(void *) &local_paddle);
        
    }  
}


//This task controls the ball
void ballDirections(void* parameter){
    position_t ball_position_t, old_ball_position_t, local_paddle;    
    bool start = true,local_player_lost = true,bt_player_lost = false;
    int x_speed=0,y_speed=0;
    unsigned int seed = esp_random();
    float speed = 4;
    int paddle_speed;
    int min = 0,max = 0;
    while (1)
    {
        vTaskDelay(20/portTICK_PERIOD_MS);

        if( start || local_player_lost || bt_player_lost){
            ball_position_t.x = (SCREEN_WIDTH-BALL_DIMENTION)/2;
            ball_position_t.y = (SCREEN_HEIGHT-BALL_DIMENTION)/2;
            xSemaphoreTake(screen,portMAX_DELAY);
            _ssd1306_circle(&dev, ball_position_t.x, ball_position_t.y, BALL_DIMENTION/2, false);
            if(local_player_lost || bt_player_lost){
                _ssd1306_circle(&dev, old_ball_position_t.x, old_ball_position_t.y, BALL_DIMENTION/2, true);
            }
            ssd1306_show_buffer(&dev);
            xSemaphoreGive(screen);
            if(local_player_lost){
                min = 45;
                max = 135;
            }
            if(bt_player_lost){
                min = -45;
                max = -135;
            }
            int random_angle = rand_r(&seed) % (max - min + 1) + min;
            
            x_speed = (int) speed*cos((double)random_angle*(PI/180));
            y_speed = (int) speed*sin((double)random_angle*(PI/180));
            
            vTaskDelay(2000/portTICK_PERIOD_MS);
            start = false;
            local_player_lost = false;
            bt_player_lost = false;
        }
        old_ball_position_t = ball_position_t;
        ball_position_t.x+=x_speed;
        ball_position_t.y+=y_speed;

        //collisions
        
        if(ball_position_t.y+(BALL_DIMENTION/2) >= SCREEN_HEIGHT-paddle_HEIGHT) {
            xQueuePeek(paddle_postion,(void *)&local_paddle,portMAX_DELAY);

            if(local_paddle.x+paddle_WIDTH>=ball_position_t.x-(BALL_DIMENTION/2) && local_paddle.x-(BALL_DIMENTION/2)<=ball_position_t.x ){
                xQueuePeek(paddle_speed_queue,(void *)&paddle_speed,portMAX_DELAY);
                x_speed += 0.25*paddle_speed;      //add an effect to the paddle speed to the ball speed
                y_speed = -y_speed;
            }else{
                local_player_lost = true;
            }
        }
        if(ball_position_t.y == 0){
            y_speed = -y_speed;
        }
        if(ball_position_t.x+BALL_DIMENTION/2 >= SCREEN_WIDTH || ball_position_t.x-(BALL_DIMENTION/2) <= 0) {
           x_speed = -x_speed;
        }
        
        xSemaphoreTake(screen,portMAX_DELAY);
        if(local_player_lost || bt_player_lost){
            _ssd1306_circle(&dev, ball_position_t.x, ball_position_t.y, BALL_DIMENTION/2, true);
        }else{
            _ssd1306_circle(&dev, ball_position_t.x, ball_position_t.y, BALL_DIMENTION/2, false);
        }
        _ssd1306_circle(&dev, old_ball_position_t.x, old_ball_position_t.y, BALL_DIMENTION/2, true);
        ssd1306_show_buffer(&dev);
        xSemaphoreGive(screen);

    }
    
}

static uint8_t peer_mac[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

typedef enum{
    BALL_POSITION,BT_PADDLE_POSITION,LOCAL_PADDLE_POSITION
}esp_now_data_type_t;

typedef struct{
    esp_now_data_type_t type;
    position_t position;
}esp_now_data_t;

void wifiInit() {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
}

void esp_now_send_cb(const uint8_t *mac_addr, esp_now_send_status_t status) {
    if (status == ESP_NOW_SEND_SUCCESS) {
        printf("\nData sent successfully");
    } else {
        printf("\nData send failed");
    }
}

void esp_now_recv_cb(const uint8_t *mac, const uint8_t *data, int len) {
    if (len == sizeof(esp_now_data_t)) {
        esp_now_data_t *received_data = (esp_now_data_t *)data;
        switch (received_data->type)
        {
        case BALL_POSITION:
            /* code */
            break;
        case BT_PADDLE_POSITION:        //only needed in the game server
            /* code */
            break;
        case LOCAL_PADDLE_POSITION:     //no need for this case since no esp will recive its own paddle position
            /* code */
            break;
        default:
            break;
        }
    } else {
        printf("\nInvalid data length");
    }
}

void espNowInit(){
    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_register_send_cb(esp_now_send_cb));
    ESP_ERROR_CHECK(esp_now_register_recv_cb(esp_now_recv_cb));                                        
    esp_now_peer_info_t peer_info = {       //add peer
        .channel = 1,
        .encrypt = false,  //disable encryption for simplicity
    };
    memcpy(peer_info.peer_addr, peer_mac, 6);
    ESP_ERROR_CHECK(esp_now_add_peer(&peer_info));
}

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    wifiInit();
    //temporary code just to get the mac address of the esp32
    //
    uint8_t mac[6];
    esp_wifi_get_mac(WIFI_IF_STA, mac);
    printf("MAC Address: %02X:%02X:%02X:%02X:%02X:%02X",mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    //
    //
    espNowInit();
    vSemaphoreCreateBinary(screen);
    i2c_master_init(&dev, CONFIG_SDA_GPIO, CONFIG_SCL_GPIO, CONFIG_RESET_GPIO);
    ssd1306_init(&dev, 128, 64); 
    ssd1306_contrast(&dev, 0xff);
    ssd1306_clear_screen(&dev, false);
    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_register_send_cb(esp_now_send_cb));
    xTaskCreate(paddleDirection,"paddleDirection",4096,NULL,1,NULL);
    xTaskCreate(paddleDrawer,"paddleDrawer",4096,NULL,1,NULL);
    xTaskCreate(ballDirections,"ballDirections",4096,NULL,1,NULL);
}
