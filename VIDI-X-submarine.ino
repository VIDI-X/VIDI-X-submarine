#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <FastLED.h>

class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_ILI9341 _panel_instance;
  lgfx::Bus_SPI _bus_instance;
public:
  LGFX(void) {
    auto cfg = _bus_instance.config();
    cfg.spi_host = VSPI_HOST;
    cfg.spi_mode = 0;
    cfg.freq_write = 40000000;
    cfg.freq_read = 16000000;
    cfg.spi_3wire = true;
    cfg.use_lock = true;
    cfg.dma_channel = 1;
    cfg.pin_sclk = 18;
    cfg.pin_mosi = 23;
    cfg.pin_miso = 19;
    cfg.pin_dc = 21;
    _bus_instance.config(cfg);
    _panel_instance.setBus(&_bus_instance);

    auto pcfg = _panel_instance.config();
    pcfg.pin_cs = 5;
    pcfg.pin_rst = -1;
    pcfg.pin_busy = -1;
    pcfg.memory_width = 240;
    pcfg.memory_height = 320;
    pcfg.panel_width = 240;
    pcfg.panel_height = 320;
    pcfg.offset_x = 0;
    pcfg.offset_y = 0;
    pcfg.offset_rotation = 1;
    pcfg.dummy_read_pixel = 8;
    pcfg.dummy_read_bits = 1;
    pcfg.readable = true;
    pcfg.invert = false;
    pcfg.rgb_order = false;
    pcfg.dlen_16bit = false;
    pcfg.bus_shared = true;
    _panel_instance.config(pcfg);
    setPanel(&_panel_instance);
  }
};

static LGFX lcd;
static LGFX_Sprite canvas(&lcd);

#define SCREEN_WIDTH  320
#define SCREEN_HEIGHT 240
#define UI_HEIGHT     20

#define BTN_LR    34
#define BTN_UD    35
#define BTN_FIRE  32
#define BTN_SPEED_DOWN 0
#define BTN_SPEED_UP   13
#define BTN_TORP_LEFT  27
#define BTN_TORP_RIGHT 39

const float MAX_SPEED = 100.0f;
const float TORPEDO_SPEED = 200.0f;

struct Torpedo {
  float x, y;
  float angle;
  bool active;
};

struct Submarine {
  float x, y;
  float angle; // degrees
  int speedLevel; // -1..3
  bool alive;
  int torpedoes;
  Torpedo torp;
};

Submarine player;
Submarine enemies[10];

unsigned long lastEnemyFire = 0;
const unsigned long ENEMY_FIRE_COOLDOWN = 2000;

void resetPlayer() {
  player.x = SCREEN_WIDTH/2;
  player.y = SCREEN_HEIGHT/2;
  player.angle = 0;
  player.speedLevel = 0;
  player.alive = true;
  player.torpedoes = 99; // unlimited for player
  player.torp.active = false;
}

void resetEnemies() {
  for(int i=0;i<10;i++) {
    enemies[i].x = random(20, SCREEN_WIDTH-20);
    enemies[i].y = random(20, SCREEN_HEIGHT-20);
    enemies[i].angle = random(0,360);
    enemies[i].speedLevel = random(1,4); // forward speeds
    enemies[i].alive = true;
    enemies[i].torpedoes = 10;
    enemies[i].torp.active = false;
  }
}

float deg2rad(float a){ return a * 0.0174532925f; }

void firePlayerTorpedo(){
  if(player.torp.active) return;
  player.torp.active = true;
  player.torp.x = player.x;
  player.torp.y = player.y;
  player.torp.angle = player.angle;
}

void fireEnemyTorpedo(int idx){
  if(!enemies[idx].alive || enemies[idx].torpedoes<=0 || enemies[idx].torp.active) return;
  // Predict simple intercept
  float px = player.x;
  float py = player.y;
  float pv = player.speedLevel * 25.0f;
  float pa = deg2rad(player.angle);
  float pvx = cos(pa)*pv;
  float pvy = sin(pa)*pv;
  float dx = px - enemies[idx].x;
  float dy = py - enemies[idx].y;
  float dist = sqrt(dx*dx+dy*dy);
  float t = dist / TORPEDO_SPEED;
  float tx = px + pvx * t - enemies[idx].x;
  float ty = py + pvy * t - enemies[idx].y;
  float angle = atan2(ty, tx) * 57.2957795f;
  enemies[idx].torp.active = true;
  enemies[idx].torp.x = enemies[idx].x;
  enemies[idx].torp.y = enemies[idx].y;
  enemies[idx].torp.angle = angle;
  enemies[idx].torpedoes--;
}

void setup(){
  Serial.begin(115200);
  lcd.init();
  lcd.setRotation(0);
  canvas.setPsram(true);
  canvas.createSprite(SCREEN_WIDTH, SCREEN_HEIGHT);
  pinMode(BTN_LR, INPUT);
  pinMode(BTN_UD, INPUT);
  pinMode(BTN_FIRE, INPUT_PULLUP);
  pinMode(BTN_SPEED_DOWN, INPUT_PULLUP);
  pinMode(BTN_SPEED_UP, INPUT_PULLUP);
  pinMode(BTN_TORP_LEFT, INPUT_PULLUP);
  pinMode(BTN_TORP_RIGHT, INPUT_PULLUP);
  randomSeed(analogRead(33));
  resetPlayer();
  resetEnemies();
}

void updatePlayerInput(){
  int lr = analogRead(BTN_LR);
  int ud = analogRead(BTN_UD);
  if(lr > 4000) player.angle -= 5;
  if(lr > 1800 && lr < 2200) player.angle += 5;
  if(ud > 4000) player.y += 0.5f; // dive slowly
  if(ud > 1800 && ud < 2200) player.y -= 0.5f; // surface

  static unsigned long lastSpeed = 0;
  if(digitalRead(BTN_SPEED_UP)==LOW && millis()-lastSpeed>200){
    if(player.speedLevel<3) player.speedLevel++;
    lastSpeed=millis();
  }
  if(digitalRead(BTN_SPEED_DOWN)==LOW && millis()-lastSpeed>200){
    if(player.speedLevel>-1) player.speedLevel--;
    lastSpeed=millis();
  }

  static bool lastFire = HIGH;
  bool nowFire = digitalRead(BTN_FIRE);
  if(lastFire==HIGH && nowFire==LOW) firePlayerTorpedo();
  lastFire = nowFire;

  if(player.torp.active){
    if(digitalRead(BTN_TORP_LEFT)==LOW) player.torp.angle -= 5;
    if(digitalRead(BTN_TORP_RIGHT)==LOW) player.torp.angle += 5;
  }
}

void moveSubmarine(Submarine &s){
  float spd = s.speedLevel * 25.0f; // step
  float a = deg2rad(s.angle);
  s.x += cos(a)*spd*0.016f;
  s.y += sin(a)*spd*0.016f;
  if(s.x<5) s.x=5;
  if(s.x>SCREEN_WIDTH-5) s.x=SCREEN_WIDTH-5;
  if(s.y<UI_HEIGHT+5) s.y=UI_HEIGHT+5;
  if(s.y>SCREEN_HEIGHT-5) s.y=SCREEN_HEIGHT-5;
}

void updateTorpedo(Torpedo &t){
  if(!t.active) return;
  float a = deg2rad(t.angle);
  t.x += cos(a)*TORPEDO_SPEED*0.016f;
  t.y += sin(a)*TORPEDO_SPEED*0.016f;
  if(t.x<0 || t.x>SCREEN_WIDTH || t.y<0 || t.y>SCREEN_HEIGHT)
    t.active=false;
}

void checkCollisions(){
  // enemy vs enemy collisions
  for(int i=0;i<10;i++) if(enemies[i].alive)
    for(int j=i+1;j<10;j++) if(enemies[j].alive){
      float dx=enemies[i].x-enemies[j].x;
      float dy=enemies[i].y-enemies[j].y;
      if(dx*dx+dy*dy<16){
        enemies[i].alive=false;
        enemies[j].alive=false;
      }
    }

  // player torpedo vs enemies
  if(player.torp.active){
    for(int i=0;i<10;i++) if(enemies[i].alive){
      float dx=enemies[i].x-player.torp.x;
      float dy=enemies[i].y-player.torp.y;
      if(dx*dx+dy*dy<16){
        enemies[i].alive=false;
        player.torp.active=false;
      }
    }
  }

  // enemy torpedoes vs player
  for(int i=0;i<10;i++) if(enemies[i].torp.active){
    float dx=player.x-enemies[i].torp.x;
    float dy=player.y-enemies[i].torp.y;
    if(dx*dx+dy*dy<16){
      player.alive=false;
    }
  }

  // enemy torpedo hits enemy sub
  for(int i=0;i<10;i++) if(enemies[i].torp.active)
    for(int j=0;j<10;j++) if(i!=j && enemies[j].alive){
      float dx=enemies[j].x-enemies[i].torp.x;
      float dy=enemies[j].y-enemies[i].torp.y;
      if(dx*dx+dy*dy<16){
        enemies[j].alive=false;
        enemies[i].torp.active=false;
      }
    }

  // torpedo vs torpedo
  if(player.torp.active)
    for(int i=0;i<10;i++) if(enemies[i].torp.active){
      float dx=enemies[i].torp.x-player.torp.x;
      float dy=enemies[i].torp.y-player.torp.y;
      if(dx*dx+dy*dy<9){
        enemies[i].torp.active=false;
        player.torp.active=false;
      }
    }
  for(int i=0;i<10;i++) if(enemies[i].torp.active)
    for(int j=i+1;j<10;j++) if(enemies[j].torp.active){
      float dx=enemies[j].torp.x-enemies[i].torp.x;
      float dy=enemies[j].torp.y-enemies[i].torp.y;
      if(dx*dx+dy*dy<9){
        enemies[i].torp.active=false;
        enemies[j].torp.active=false;
      }
    }
}

void updateEnemies(){
  for(int i=0;i<10;i++) if(enemies[i].alive){
    moveSubmarine(enemies[i]);
    if(random(1000)<5) enemies[i].angle+=random(-30,31);
  }
  if(millis()-lastEnemyFire>ENEMY_FIRE_COOLDOWN){
    int tries=0;
    while(tries<10){
      int idx=random(0,10);
      if(enemies[idx].alive && enemies[idx].torpedoes>0 && !enemies[idx].torp.active){
        fireEnemyTorpedo(idx);
        lastEnemyFire=millis();
        break;
      }
      tries++;
    }
  }
}

void drawGame(){
  canvas.fillScreen(0x0000);
  canvas.drawRect(0,UI_HEIGHT,SCREEN_WIDTH,SCREEN_HEIGHT-UI_HEIGHT,0x07E0);

  // draw player
  canvas.setTextColor(0xFFFF);
  canvas.drawCentreString("X",player.x,player.y-4,1);
  float a=deg2rad(player.angle);
  int lx=player.x+cos(a)*8;
  int ly=player.y+sin(a)*8;
  canvas.drawLine(player.x,player.y,lx,ly,0xFFE0);

  if(player.torp.active){
    float ax=player.torp.x+cos(deg2rad(player.torp.angle))*4;
    float ay=player.torp.y+sin(deg2rad(player.torp.angle))*4;
    canvas.drawLine(player.torp.x,player.torp.y,ax,ay,0xFFFF);
  }

  for(int i=0;i<10;i++) if(enemies[i].alive){
    canvas.drawCentreString("O",enemies[i].x,enemies[i].y-4,1);
    float ex=enemies[i].x+cos(deg2rad(enemies[i].angle))*6;
    float ey=enemies[i].y+sin(deg2rad(enemies[i].angle))*6;
    canvas.drawLine(enemies[i].x,enemies[i].y,ex,ey,0x07FF);
  }
  for(int i=0;i<10;i++) if(enemies[i].torp.active){
    float ax=enemies[i].torp.x+cos(deg2rad(enemies[i].torp.angle))*4;
    float ay=enemies[i].torp.y+sin(deg2rad(enemies[i].torp.angle))*4;
    canvas.drawLine(enemies[i].torp.x,enemies[i].torp.y,ax,ay,0xF800);
  }

  canvas.setCursor(0,0);
  canvas.printf("SPD:%d ANG:%d",player.speedLevel, (int)player.angle);
  canvas.pushSprite(0,0);
}

void loop(){
  if(!player.alive){
    canvas.fillScreen(0x0000);
    canvas.setTextColor(0xFFFF);
    canvas.setCursor(90,120);
    canvas.print("GAME OVER");
    canvas.pushSprite(0,0);
    return;
  }
  updatePlayerInput();
  moveSubmarine(player);
  if(player.torp.active) updateTorpedo(player.torp);
  for(int i=0;i<10;i++) if(enemies[i].torp.active) updateTorpedo(enemies[i].torp);
  updateEnemies();
  checkCollisions();
  drawGame();
  delay(16);
}
