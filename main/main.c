#include <stdio.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/adc.h"
#include "ssd1306.h"
#include <math.h>
#include <time.h>
#include <esp_random.h>

#define PI 3.14

#define MAX_SPEED 8 

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

#define PONG_WIDTH 16
#define PONG_HEIGHT 4

#define BALL_DIMENTION 4        //since the ball width equal its height it has one dimention 


#define UPDATE_LOCAL_PONG (1<<0)
#define UPDATE_BLUTOOTH_PONG (1<<1)
#define UPDATE_BALL_PONG (1<<2)

EventGroupHandle_t update_position;

QueueHandle_t pong_speed_queue;
QueueHandle_t pong_postion;
QueueHandle_t ball_directions_queue;

SemaphoreHandle_t screen;

typedef struct 
{
    int x;
    int y;
}position;


SSD1306_t dev;


//Pong line graph
uint8_t pong [] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};  //16x4 pixels white line

//Ball graph
uint8_t ball [] = {0x00, 0x18, 0x3c, 0x7e, 0x7e, 0x3c, 0x18, 0x00};  //8x8 pixels ball


//Calculate the speed of the pong according to joystick input reads

void pongDirection(void * parameter){
    adc2_config_channel_atten(ADC2_CHANNEL_2,ADC_ATTEN_DB_12);
    int mid_point, joystick;
    adc2_get_raw(ADC2_CHANNEL_2,ADC_BITWIDTH_12,&mid_point);              //get the value of the mid point of the joystick
    printf("midpoint Analog value is : %d ",mid_point);
    int speeds[] = {0,1,2,3,4};                                           //how many pixels the pong moves according to joystick value
    int analog_max = 4095;                                                //max value we get from the ADC
    int number_of_speeds = sizeof(speeds)/sizeof(speeds[0]);  
    int negative_range = (analog_max-mid_point)/number_of_speeds;
    int positive_range = mid_point/number_of_speeds;
    int chosen_speed = 0;
    int pong_speed =0;                                                  //initail state
    pong_speed_queue = xQueueCreate(1,sizeof(pong_speed));
    while(1){
        vTaskDelay(20/portTICK_PERIOD_MS);
        adc2_get_raw(ADC2_CHANNEL_2,ADC_BITWIDTH_12,&joystick);

        if (joystick>=mid_point && joystick < analog_max){
            chosen_speed = (joystick - mid_point)/negative_range;
            pong_speed = -speeds[chosen_speed];
        }else if (joystick<mid_point && joystick != 0 ){
            chosen_speed = (mid_point - joystick)/positive_range;
            pong_speed = speeds[chosen_speed];
        }else if (joystick == 0){
            pong_speed = speeds[number_of_speeds-1]; 
        }else if (joystick == 4095){
            pong_speed = -speeds[number_of_speeds-1]; 
        }

        //xQueueSend(pong_speed_queue,(void*)&pong_speed,portMAX_DELAY);
        xQueueOverwrite(pong_speed_queue,(void *)&pong_speed);
    }
}



void pong_drawer(void *parameter){
    int pong_speed = 0;
    position local_pong = {0};
    pong_postion = xQueueCreate(1,sizeof(position));
    //initial postiotns
    local_pong.x = (SCREEN_WIDTH-PONG_WIDTH)/2;
    local_pong.y = SCREEN_HEIGHT-PONG_HEIGHT;
    
    //initail drawing
    xSemaphoreTake(screen,portMAX_DELAY);
    ssd1306_bitmaps(&dev, local_pong.x, local_pong.y, pong, PONG_WIDTH, PONG_HEIGHT, false); 
    xSemaphoreGive(screen);
    //eraser
    uint8_t eraser[MAX_SPEED*PONG_HEIGHT] = {0};
    
    position old = {0};
    while (1)
    {
        vTaskDelay(20/portTICK_PERIOD_MS);
        //update loal pong postion and drwa it then erace it from the previous postition and share    
        //xQueueReceive(pong_speed_queue,(void *)&pong_speed,portMAX_DELAY);      //get the pong speed
        xQueuePeek(pong_speed_queue,(void *)&pong_speed,portMAX_DELAY);
        old.x = local_pong.x;           //save pong old postion
        local_pong.x += pong_speed;     //updating local pong position

        if(local_pong.x>(SCREEN_WIDTH-PONG_WIDTH)){     //ensure that pong stays within screen boundiries
            local_pong.x = SCREEN_WIDTH-PONG_WIDTH;
        }
        if (local_pong.x<0){
            local_pong.x =0;
        }

        xSemaphoreTake(screen,portMAX_DELAY);
        ssd1306_bitmaps(&dev, local_pong.x, local_pong.y, pong, PONG_WIDTH, PONG_HEIGHT, false);   //drwaing the paddle in new location

        //erase any trace of the pong in its old postion
        if(pong_speed<0){    
            if((SCREEN_WIDTH-old.x+PONG_WIDTH-pong_speed)<MAX_SPEED){
                ssd1306_bitmaps(&dev, local_pong.x+PONG_WIDTH, local_pong.y, eraser, SCREEN_WIDTH-(local_pong.x+PONG_WIDTH), PONG_HEIGHT, false);
            } else {  
                ssd1306_bitmaps(&dev, (old.x+PONG_WIDTH-pong_speed), local_pong.y, eraser, MAX_SPEED, PONG_HEIGHT, false);  
            }  
        }
        if(pong_speed>0){ 
            if(local_pong.x-MAX_SPEED<0){       //at the screen boundiries
                ssd1306_bitmaps(&dev, 0, local_pong.y, eraser, MAX_SPEED, PONG_HEIGHT, false);
            }else{
                ssd1306_bitmaps(&dev, (local_pong.x-MAX_SPEED), local_pong.y, eraser, MAX_SPEED, PONG_HEIGHT, false);  
            }  
        }
        xSemaphoreGive(screen);
        xQueueOverwrite(pong_postion,(void *) &local_pong);
        
    }  
}


//This task controls the ball
void ballDirections(void* parameter){
    position ball_position, old_ball_position, local_pong;    
    bool start = true,local_player_lost = true,bt_player_lost = false;
    int x_speed=0,y_speed=0;
    unsigned int seed = esp_random();
    float speed = 4;
    int pong_speed;
    int min = 0,max = 0;
    while (1)
    {
        vTaskDelay(20/portTICK_PERIOD_MS);

        if( start || local_player_lost || bt_player_lost){
            ball_position.x = (SCREEN_WIDTH-BALL_DIMENTION)/2;
            ball_position.y = (SCREEN_HEIGHT-BALL_DIMENTION)/2;
            xSemaphoreTake(screen,portMAX_DELAY);
            _ssd1306_circle(&dev, ball_position.x, ball_position.y, BALL_DIMENTION/2, false);
            if(local_player_lost || bt_player_lost){
                _ssd1306_circle(&dev, old_ball_position.x, old_ball_position.y, BALL_DIMENTION/2, true);
            }
            ssd1306_show_buffer(&dev);
            xSemaphoreGive(screen);
            if(local_player_lost){
                min = 30;
                max = 150;
            }
            if(bt_player_lost){
                min = -30;
                max = -150;
            }
            int random_angle = rand_r(&seed) % (max - min + 1) + min;
            
            x_speed = (int) speed*cos((double)random_angle*(PI/180));
            y_speed = (int) speed*sin((double)random_angle*(PI/180));
            
            vTaskDelay(2000/portTICK_PERIOD_MS);
            start = false;
            local_player_lost = false;
            bt_player_lost = false;
        }
        old_ball_position = ball_position;
        ball_position.x+=x_speed;
        ball_position.y+=y_speed;

        //collisions
        if(ball_position.y+BALL_DIMENTION/2 >= SCREEN_HEIGHT-PONG_HEIGHT) {
            xQueuePeek(pong_postion,(void *)&local_pong,portMAX_DELAY);

            if(local_pong.x+PONG_WIDTH>=ball_position.x-BALL_DIMENTION/2 && local_pong.x<=ball_position.x+BALL_DIMENTION/2 ){
                xQueuePeek(pong_speed_queue,(void *)&pong_speed,portMAX_DELAY);
                x_speed += 0.1*pong_speed;      //add an effect to the paddle speed to the ball speed
                y_speed = -y_speed;
            }else{
                local_player_lost = true;
            }
        }
        if(ball_position.y == 0){
            y_speed = -y_speed;
        }
        if(ball_position.x+BALL_DIMENTION/2 >= SCREEN_WIDTH || ball_position.x-1 <= 0) {
           x_speed = -x_speed;
        }
        
        xSemaphoreTake(screen,portMAX_DELAY);
        if(local_player_lost || bt_player_lost){
            _ssd1306_circle(&dev, ball_position.x, ball_position.y, BALL_DIMENTION/2, true);
        }else{
            _ssd1306_circle(&dev, ball_position.x, ball_position.y, BALL_DIMENTION/2, false);
        }
        _ssd1306_circle(&dev, old_ball_position.x, old_ball_position.y, BALL_DIMENTION/2, true);
        ssd1306_show_buffer(&dev);
        xSemaphoreGive(screen);
        

    }
    
}



void app_main(void)
{
    vSemaphoreCreateBinary(screen);
    i2c_master_init(&dev, CONFIG_SDA_GPIO, CONFIG_SCL_GPIO, CONFIG_RESET_GPIO);
    ssd1306_init(&dev, 128, 64); 
    ssd1306_contrast(&dev, 0xff);
    ssd1306_clear_screen(&dev, false);
    update_position = xEventGroupCreate();  //Event group for notifying about position change
    xTaskCreate(pongDirection,"pongDirection",4096,NULL,1,NULL);
    xTaskCreate(pong_drawer,"pong_drawer",4096,NULL,1,NULL);
    xTaskCreate(ballDirections,"ballDirections",4096,NULL,1,NULL);
}
