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

//#define HOST 1 
#define SLAVE 1

#define PI 3.14

#define MAX_SPEED 8 

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

#define paddle_WIDTH 16
#define paddle_HEIGHT 4

#define BALL_DIMENTION 4        //since the ball width equal its height it has one dimention 



QueueHandle_t paddle_position;
QueueHandle_t paddle_speed_queue;

#ifdef HOST

TaskHandle_t ball_direction_task;


QueueHandle_t ball_directions_queue;
QueueHandle_t slave_paddle_speed;
QueueHandle_t esp_now_slave_paddle;

#endif


#ifdef SLAVE

QueueHandle_t esp_now_host_data_ball;
QueueHandle_t esp_now_host_data_paddle;

#endif

SemaphoreHandle_t screen;



#ifdef HOST
//MAC Address fo the slave
static uint8_t peer_mac[] = {0xE0,0xE2,0xE6,0xAC,0xA0,0x9C};

#endif


#ifdef SLAVE
//MAAC Address for host
static uint8_t peer_mac[6] = {0xB0,0xB2,0x1C,0x97,0x79,0xEC};
#endif


typedef struct 
{
    int x;
    int y;
}position_t;


typedef enum{
    BALL_POSITION,SLAVE_PADDLE_POSITION,HOST_PADDLE_POSITION
}esp_now_data_type_t;

typedef struct{
    esp_now_data_type_t type;
    position_t position;
}esp_now_data_t;


SSD1306_t dev;


//paddle line graph
uint8_t paddle [] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};  //16x4 pixels white line




#ifdef HOST

//Calculate the speed of the paddle according to joystick input reads

void paddleDirection(void * parameter){
    adc1_config_channel_atten(ADC1_CHANNEL_4,ADC_ATTEN_DB_12);
    int mid_point, joystick;
    mid_point=adc1_get_raw(ADC1_CHANNEL_4);              //get the value of the mid point of the joystick
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
        joystick=adc1_get_raw(ADC1_CHANNEL_4);

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
    esp_now_data_t data;    //this variable is used to send local paddle location (x,y) to the slave via esp-now
    int paddle_speed = 0;
    position_t local_paddle = {0};
    paddle_position = xQueueCreate(1,sizeof(position_t));
    //initial postiotns
    local_paddle.x = (SCREEN_WIDTH-paddle_WIDTH)/2;
    local_paddle.y = SCREEN_HEIGHT-paddle_HEIGHT;
    bool start =true;
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
        //update loal paddle position and drwa it then erace it from the previous postition and share    
        //xQueueReceive(paddle_speed_queue,(void *)&paddle_speed,portMAX_DELAY);      //get the paddle speed
        xQueuePeek(paddle_speed_queue,(void *)&paddle_speed,portMAX_DELAY);
        if(paddle_speed == 0 && !start){      //to optimize the performance
            continue;
        }
        old.x = local_paddle.x;           //save paddle old position
        local_paddle.x += paddle_speed;     //updating local paddle position_t

        if(local_paddle.x>(SCREEN_WIDTH-paddle_WIDTH)){     //ensure that paddle stays within screen boundiries
            local_paddle.x = SCREEN_WIDTH-paddle_WIDTH;
        }
        if (local_paddle.x<0){
            local_paddle.x =0;
        }

        xSemaphoreTake(screen,portMAX_DELAY);
        
        ssd1306_bitmaps(&dev, local_paddle.x, local_paddle.y, paddle, paddle_WIDTH, paddle_HEIGHT, false);   //drwaing the paddle in new location
        
        //erase any traces of the paddle in its old position
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
        xQueueOverwrite(paddle_position,(void *) &local_paddle);
        if(local_paddle.x != old.x){
            data.position.x = SCREEN_WIDTH -(local_paddle.x + paddle_WIDTH); 
            data.position.y = 0;            // we want to show it on the top of the slave's screen
            data.type = HOST_PADDLE_POSITION;
            ESP_ERROR_CHECK(esp_now_send(peer_mac,(uint8_t * )&data,sizeof(data)));        //I'm NOT sure if esp_now_send is thread safe more research on it needed
        }
        start = false;
    }  
}


//This task controls the ball
void ballDirections(void* parameter){
    esp_now_data_t data;    //this variable is used to send ball location (x,y) to the slave via esp-now
    position_t ball_position, old_ball_position, local_paddle, slave_paddle;    
    bool start = true,local_player_lost = true, slave_player_lost = false;
    int x_speed=0,y_speed=0;        
    unsigned int seed = esp_random();   //to generate random seeds
    float speed = 4;            //max initial ball speed 
    int paddle_speed, slave_speed = 0;
    int min = 0,max = 0;        //initial max and min angle of the ball movement
    while (1)
    {
        vTaskDelay(20/portTICK_PERIOD_MS);

        if( start || local_player_lost ||  slave_player_lost){
            ball_position.x = (SCREEN_WIDTH-BALL_DIMENTION)/2;
            ball_position.y = (SCREEN_HEIGHT-BALL_DIMENTION)/2;
            data.position = ball_position;
            data.type = BALL_POSITION;
            ESP_ERROR_CHECK(esp_now_send(peer_mac,(uint8_t *)&data,sizeof(data)));        //share the ball position with the slave
            xSemaphoreTake(screen,portMAX_DELAY);
            _ssd1306_circle(&dev, ball_position.x, ball_position.y, BALL_DIMENTION/2, false);
            if(local_player_lost ||  slave_player_lost){
                _ssd1306_circle(&dev, old_ball_position.x, old_ball_position.y, BALL_DIMENTION/2, true);        //erase the ball from last position after one of the players defeated
            }
            ssd1306_show_buffer(&dev);

            

            xSemaphoreGive(screen);
            /*if(local_player_lost){
               
            }
            */
            min = 45;
            max = 135;
           /* if( slave_player_lost){
                min = -45;
                max = -135;
            }
                */
            int random_angle = rand_r(&seed) % (max - min + 1) + min;
            
            x_speed = (int) speed*cos((double)random_angle*(PI/180));
            if(local_player_lost){
                y_speed = (int) speed*sin((double)random_angle*(PI/180));
            }
            if(slave_player_lost){
                y_speed = - ((int) speed*sin((double)random_angle*(PI/180)));
            }
            
            if(start){
                xTaskNotifyWait(0,0,NULL,portMAX_DELAY); //wait for the other player to connect
            }

            start = false;
            local_player_lost = false;
            slave_player_lost = false;
            vTaskDelay(2000/portTICK_PERIOD_MS); 
        }
        old_ball_position = ball_position;
        ball_position.x+=x_speed;
        ball_position.y+=y_speed;

        //collisions
        
        if(ball_position.y+(BALL_DIMENTION/2) >= SCREEN_HEIGHT-paddle_HEIGHT) {
            xQueuePeek(paddle_position,(void *)&local_paddle,portMAX_DELAY);

            if(local_paddle.x+paddle_WIDTH>=ball_position.x-(BALL_DIMENTION/2) && local_paddle.x-(BALL_DIMENTION/2)<=ball_position.x ){
                xQueuePeek(paddle_speed_queue,(void *)&paddle_speed,portMAX_DELAY);
                x_speed += 0.25*paddle_speed;      //add an effect to the paddle speed to the ball speed
                y_speed = -y_speed;
            }else{
                local_player_lost = true;
            }
        }
        if(ball_position.y-(BALL_DIMENTION/2) <= paddle_HEIGHT/2){
            xQueuePeek(esp_now_slave_paddle,&slave_paddle,portMAX_DELAY);
            if(slave_paddle.x+paddle_WIDTH>=ball_position.x-(BALL_DIMENTION/2) && slave_paddle.x-(BALL_DIMENTION/2)<=ball_position.x ){
                if (xQueuePeek(slave_paddle_speed,(void *)&slave_speed,20) == pdTRUE){
                    printf("\nSlave Speed is : %d",slave_speed);
                    x_speed += 0.25*slave_speed;
                }
                y_speed = -y_speed;
            }else{
                slave_player_lost = true;

            }
        }
        if(ball_position.x+BALL_DIMENTION/2 >= SCREEN_WIDTH || ball_position.x-(BALL_DIMENTION/2) <= 0) {
           
           x_speed = -x_speed;
        }
        data.position.x = SCREEN_WIDTH - ball_position.x;
        data.position.y = SCREEN_HEIGHT - ball_position.y;
        data.type = BALL_POSITION;
        ESP_ERROR_CHECK(esp_now_send(peer_mac,(uint8_t *)&data,sizeof(data)));
        xSemaphoreTake(screen,portMAX_DELAY);
        if(local_player_lost ||  slave_player_lost){
            _ssd1306_circle(&dev, ball_position.x, ball_position.y, BALL_DIMENTION/2, true);
        }else{
            _ssd1306_circle(&dev, ball_position.x, ball_position.y, BALL_DIMENTION/2, false);
        }
        _ssd1306_circle(&dev, old_ball_position.x, old_ball_position.y, BALL_DIMENTION/2, true);
        ssd1306_show_buffer(&dev);
        xSemaphoreGive(screen);
        //if(ball_position.x != old_ball_position.x){         //avoid sending replicated data
           
        //}

    }
    
}


//this task will get the data from the esp_now slave and drwa its paddle on top of the screen
void slavePaddle(void* parameter){
    
    position_t data;    //contains the new position of the slave paddle
    position_t old_slave_paddle = {0};
    int slave_speed;
    bool start = true;
    uint8_t eraser[MAX_SPEED*paddle_HEIGHT] = {0};
    while(1){
        if(start){
            xQueuePeek(esp_now_slave_paddle,&data,portMAX_DELAY);
            xTaskNotify(ball_direction_task,0,eNoAction);
            //printf("\n Start Data recived \n X: %d",data.x);
            
        }else{
            xQueuePeek(esp_now_slave_paddle,&data,20/portTICK_PERIOD_MS);
            //printf("\nPlaying Data recived \n X: %d",data.x);
        }
        if(old_slave_paddle.x == data.x && !start){    //avoid redrawing if no update is provided.
            
            vTaskDelay(20/portTICK_PERIOD_MS);
            //printf("\nno new data recived");
            continue;
        }
        //drwa bt_paddle based on the recived data
        xSemaphoreTake(screen,portMAX_DELAY);
        ssd1306_bitmaps(&dev, data.x, data.y, paddle, paddle_WIDTH, paddle_HEIGHT, false);
        //printf("\nSlave paddle drawn!");
        //erase bt_paddle traces in its old position
        if(!start){     //on the start of the game there is no trace of the paddle yet
            if(old_slave_paddle.x>data.x){    
                    ssd1306_bitmaps(&dev, data.x+paddle_WIDTH, data.y, eraser, MAX_SPEED, paddle_HEIGHT, false);
            }else if (old_slave_paddle.x<data.x){ 
                if(data.x<MAX_SPEED){       //at the screen boundiries*/
                    ssd1306_bitmaps(&dev, 0, data.y, eraser, MAX_SPEED, paddle_HEIGHT, false);
                }else{
                    ssd1306_bitmaps(&dev, (data.x-MAX_SPEED), data.y, eraser, MAX_SPEED, paddle_HEIGHT, false);  
                }
            }
        }
        xSemaphoreGive(screen);
        if(!start){
            slave_speed = data.x - old_slave_paddle.x;
        }else{
            slave_speed = 0;
        }
        xQueueOverwrite(slave_paddle_speed,(void*) &slave_speed);
        old_slave_paddle = data;
        start = false;
    }
}


#endif

#ifdef SLAVE
void slavePaddleDirection(void * parameter){
    adc1_config_channel_atten(ADC1_CHANNEL_4,ADC_ATTEN_DB_12);
    int mid_point, joystick;
    mid_point=adc1_get_raw(ADC1_CHANNEL_4);              //get the value of the mid point of the joystick
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
        joystick=adc1_get_raw(ADC1_CHANNEL_4);

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



void slavePaddleDrawer(void *parameter){
    esp_now_data_t data;    //this variable is used to send local paddle location (x,y) to the slave via esp-now
    int paddle_speed = 0;
    position_t local_paddle = {0};
    paddle_position = xQueueCreate(1,sizeof(position_t));
    //initial postiotns
    local_paddle.x = (SCREEN_WIDTH-paddle_WIDTH)/2;
    local_paddle.y = SCREEN_HEIGHT-paddle_HEIGHT;
    bool start = true;
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
        //update loal paddle position and drwa it then erace it from the previous postition and share    
        //xQueueReceive(paddle_speed_queue,(void *)&paddle_speed,portMAX_DELAY);      //get the paddle speed
        xQueuePeek(paddle_speed_queue,(void *)&paddle_speed,portMAX_DELAY);
        if(paddle_speed ==0){
            continue;
        }
        old.x = local_paddle.x;           //save paddle old position
        local_paddle.x += paddle_speed;     //updating local paddle position_t

        if(local_paddle.x>(SCREEN_WIDTH-paddle_WIDTH)){     //ensure that paddle stays within screen boundiries
            local_paddle.x = SCREEN_WIDTH-paddle_WIDTH;
        }
        if (local_paddle.x<0){
            local_paddle.x =0;
        }

        xSemaphoreTake(screen,portMAX_DELAY);
        
        ssd1306_bitmaps(&dev, local_paddle.x, local_paddle.y, paddle, paddle_WIDTH, paddle_HEIGHT, false);   //drwaing the paddle in new location
        
        //erase any traces of the paddle in its old position
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
        xQueueOverwrite(paddle_position,(void *) &local_paddle);
        if( local_paddle.x != old.x){
            data.position.x = SCREEN_WIDTH -(local_paddle.x + paddle_WIDTH); 
            printf("\nx sent to host is %d",data.position.x);
            data.position.y = 0;            // we want to show it on the top of the slave's screen
            data.type = SLAVE_PADDLE_POSITION;
            ESP_ERROR_CHECK(esp_now_send(peer_mac,(uint8_t *)&data,sizeof(data)));        //I'm NOT sure if esp_now_send is thread safe more research on it needed
        }
        start = false;
    }  
}


//this task draw the ball and hosp padlle according to the data comming from esp32 game host.
void ballDrwaing(void * parameter){
    position_t old_host_paddle = {0};
    esp_now_host_data_ball = xQueueCreate(1,sizeof(position_t));
    position_t data,old_ball = {0};
    bool start = true;
    uint8_t eraser[MAX_SPEED*paddle_HEIGHT] = {0};
    while(1){
        vTaskDelay(20/portTICK_PERIOD_MS);
        xQueuePeek(esp_now_host_data_ball,&data,portMAX_DELAY);
        xSemaphoreTake(screen,portMAX_DELAY);
        printf("\nBALL : %d, %d",data.x,data.y);
        _ssd1306_circle(&dev,data.x, data.y, BALL_DIMENTION/2, false);
        if(!start){
            if(data.x!=old_ball.x && data.y!=old_ball.y){
                _ssd1306_circle(&dev, old_ball.x, old_ball.y, BALL_DIMENTION/2, true);
                printf("\nBALL Eraser : %d, %d",old_ball.x,old_ball.y);   
            }else if(data.x==old_ball.x && data.y!=old_ball.y){
                _ssd1306_circle(&dev, old_ball.x, old_ball.y, BALL_DIMENTION/2, true);
            }
        }
        ssd1306_show_buffer(&dev);
        xSemaphoreGive(screen);
        old_ball = data;
        start = false;
    }
}

void paddleDrawing(void* parameter){
    position_t data={0},old_host_paddle = {0};
    esp_now_host_data_paddle = xQueueCreate(1,sizeof(position_t)); 
    bool start = true;
    uint8_t eraser[MAX_SPEED*paddle_HEIGHT] = {0};
    while(1){
        vTaskDelay(20/portTICK_PERIOD_MS);
        xQueuePeek(esp_now_host_data_paddle,&data,portMAX_DELAY);
        xSemaphoreTake(screen,portMAX_DELAY);
        //printf("drwa paddle");
        ssd1306_bitmaps(&dev, data.x, data.y, paddle, paddle_WIDTH, paddle_HEIGHT, false);
        //printf("\nSlave paddle drawn!");
        //erase bt_paddle traces in its old position
        if(!start){     //on the start of the game there is no trace of the paddle yet
            if(old_host_paddle.x>data.x){    
                ssd1306_bitmaps(&dev, data.x+paddle_WIDTH, data.y, eraser, MAX_SPEED, paddle_HEIGHT, false);
            }else if (old_host_paddle.x<=data.x){ 
                if(data.x<MAX_SPEED){       //at the screen boundiries*/
                    ssd1306_bitmaps(&dev, 0, data.y, eraser, MAX_SPEED, paddle_HEIGHT, false);
                }else{
                    ssd1306_bitmaps(&dev, (data.x-MAX_SPEED), data.y, eraser, MAX_SPEED, paddle_HEIGHT, false);  
                }
            }
        }
        xSemaphoreGive(screen);
        start = false;
        old_host_paddle = data;
       
    }
}


#endif




void wifiInit() {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
}

void esp_now_send_cb(const uint8_t *mac_addr, esp_now_send_status_t status) {
    /*
    if (status == ESP_NOW_SEND_SUCCESS) {
        printf("\nData sent successfully");
    } else {
        printf("\nData send failed");
    }
        */
}


void esp_now_recv_cb(const uint8_t *mac, const uint8_t *data, int len) {
    if (len == sizeof(esp_now_data_t)) {
        esp_now_data_t *received_data = (esp_now_data_t *)data;
        switch (received_data->type)
        {
    
        #ifdef SLAVE        
        case BALL_POSITION:         //We could use "default case" instead of "BALL_POSITION" and "HOST_PADDLE_POSITION" since they do the same job
            xQueueOverwrite(esp_now_host_data_ball,(void*) &received_data->position);
           
            break;
        case HOST_PADDLE_POSITION:     //no need for this case since no esp will recive its own paddle position
            xQueueOverwrite(esp_now_host_data_paddle,(void*) &received_data->position);
            
            break;
        #endif
        
        #ifdef HOST
        case SLAVE_PADDLE_POSITION:        //only needed in the esp which host the game
            xQueueOverwrite(esp_now_slave_paddle,(void*) &received_data->position);
            break;
        #endif

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
    #ifdef HOST
    esp_now_slave_paddle = xQueueCreate(1,sizeof(position_t));
    #endif
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
    #ifdef HOST
    slave_paddle_speed = xQueueCreate(1,sizeof(int));
    xTaskCreate(paddleDirection,"paddleDirection",4096,NULL,1,NULL);
    xTaskCreate(paddleDrawer,"paddleDrawer",4096,NULL,1,NULL);
    xTaskCreate(ballDirections,"ballDirections",4096,NULL,1,&ball_direction_task);
    xTaskCreate(slavePaddle,"slavePaddle",4096,NULL,1,NULL);
    #endif
    #ifdef SLAVE
    xTaskCreate(slavePaddleDirection,"paddleDirection",4096,NULL,1,NULL);
    xTaskCreate(slavePaddleDrawer,"paddleDrawer",4096,NULL,1,NULL);
    xTaskCreate(ballDrwaing,"ballDrwaing",4096,NULL,1,NULL);
    xTaskCreate(paddleDrawing,"paddleDrawing",4096,NULL,1,NULL);
    #endif
}
