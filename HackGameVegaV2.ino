/*
 * ╔══════════════════════════════════════════════════════╗
 * ║       HACKHELD VEGA II — GAME CONSOLE FIRMWARE       ║
 * ║       7 games: ...Doom·Racing·Flappy·Pong           ║
 * ╚══════════════════════════════════════════════════════╝
 *
 * Hardware target:
 *   • ESP8266 Lolin D1 Mini
 *   • SH1106 128×64 OLED (I2C @ 0x3C)
 *   • 6× tactile buttons
 *   • WS2812b NeoPixel RGB LED
 *
 * ── Library dependencies (install via Arduino Library Manager) ─
 *   • U8g2 by olikraus        (display driver)
 *   • Adafruit NeoPixel       (RGB LED)
 *
 * ── Arduino IDE board settings ─────────────────────────────────
 *   Board  : LOLIN(WEMOS) D1 mini
 *   Flash  : 4MB (FS: 2MB OTA: ~1019KB) — or any other
 *   CPU    : 80 MHz
 *
 * ── HackHeld Vega II Pinout ─────────────────────────────────────
 *   OLED SCL  → D1 (GPIO5)
 *   OLED SDA  → D2 (GPIO4)
 *   Btn LEFT  → D0 (GPIO16)  ← no internal pull-up; use external
 *   Btn RIGHT → D7 (GPIO13)
 *   Btn UP    → D5 (GPIO14)
 *   Btn DOWN  → D6 (GPIO12)
 *   Btn A     → D4 (GPIO2)   ← confirm / fire
 *   Btn B     → D3 (GPIO0)   ← back / pause  (HIGH at boot = safe)
 *   NeoPixel  → D8 (GPIO15)
 *
 * ── Controls (all games) ────────────────────────────────────────
 *   ↑↓←→  Navigate / move
 *   A      Confirm / fire / launch
 *   B      Back to main menu / pause
 *
 */

#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <Adafruit_NeoPixel.h>

// ═══════════════════════════════════════════════
//  PIN DEFINITIONS
// ═══════════════════════════════════════════════
#define PIN_LEFT    16   // D0  (INPUT only; no pull-up; PCB provides external pull-up)
#define PIN_RIGHT   13   // D7
#define PIN_UP      14   // D5
#define PIN_DOWN    12   // D6
#define PIN_A        2   // D4  confirm / fire
#define PIN_B        0   // D3  back / menu
#define PIN_LED     15   // D8  NeoPixel data

// ═══════════════════════════════════════════════
//  DISPLAY  – SH1106 128×64 I2C full-buffer
// ═══════════════════════════════════════════════
U8G2_SH1106_128X64_NONAME_F_HW_I2C display(U8G2_R0, U8X8_PIN_NONE);

// ═══════════════════════════════════════════════
//  NEOPIXEL
// ═══════════════════════════════════════════════
Adafruit_NeoPixel pixel(1, PIN_LED, NEO_GRB + NEO_KHZ800);
void setLED(uint8_t r, uint8_t g, uint8_t b) {
  pixel.setPixelColor(0, pixel.Color(r, g, b));
  pixel.show();
}

// ═══════════════════════════════════════════════
//  BUTTON SYSTEM
//  Call btn.poll() once per frame; use
//  btn.held() for held-down, btn.tapped() for
//  a single rising-edge press.
// ═══════════════════════════════════════════════
struct Btn {
  uint8_t pin;
  bool    _held  = false;
  bool    _tapped = false;

  void poll() {
    bool cur = (digitalRead(pin) == LOW);
    _tapped  = cur && !_held;
    _held    = cur;
  }
  inline bool held()   const { return _held;   }
  inline bool tapped() const { return _tapped; }
};

Btn bUp    { PIN_UP    };
Btn bDown  { PIN_DOWN  };
Btn bLeft  { PIN_LEFT  };
Btn bRight { PIN_RIGHT };
Btn bA     { PIN_A     };
Btn bB     { PIN_B     };

void pollAll() {
  bUp.poll(); bDown.poll(); bLeft.poll();
  bRight.poll(); bA.poll(); bB.poll();
}

// ═══════════════════════════════════════════════
//  GAME STATE MACHINE
// ═══════════════════════════════════════════════
enum State { STATE_MENU, STATE_SNAKE, STATE_BREAKOUT, STATE_SHOOTER, STATE_DOOM, STATE_RACING, STATE_FLAPPY, STATE_PONG };
State state = STATE_MENU;

// Forward declarations
void initSnake();    void snakeUpdate();    void snakeDraw();
void initBreakout(); void breakoutUpdate(); void breakoutDraw();
void initShooter();  void shooterUpdate();  void shooterDraw();
void initDoom();     void doomUpdate();     void doomDraw();
void initRacing();   void racingUpdate();   void racingDraw();
void initFlappy();   void flappyUpdate();   void flappyDraw();
void initPong();     void pongUpdate();     void pongDraw();

// ═══════════════════════════════════════════════
//  MAIN MENU
// ═══════════════════════════════════════════════
const char* const GAME_NAMES[] = { "Snake", "Breakout", "Shooter", "Doom", "Racing", "Flappy", "Pong" };
const int N_GAMES = 7;
int menuSel    = 0;
int menuScroll = 0;   // index of the first row currently drawn
#define MENU_VISIBLE 3  // only 3 rows fit on a 64px-tall screen

void menuDraw() {
  display.clearBuffer();

  // Title
  display.setFont(u8g2_font_7x13B_tr);
  display.drawStr(14, 13, "HACKGAME VEGA");
  display.drawHLine(0, 16, 128);

  // Keep the selected entry inside the visible scroll window
  if (menuSel < menuScroll) menuScroll = menuSel;
  if (menuSel > menuScroll + MENU_VISIBLE - 1) menuScroll = menuSel - MENU_VISIBLE + 1;

  // Game list (scrolls when there are more entries than fit)
  display.setFont(u8g2_font_6x10_tr);
  for (int row = 0; row < MENU_VISIBLE && (menuScroll + row) < N_GAMES; row++) {
    int i = menuScroll + row;
    int y = 30 + row * 13;
    if (i == menuSel) {
      display.drawRBox(2, y - 9, 124, 12, 2);
      display.setDrawColor(0);
    }
    display.drawStr(22, y, GAME_NAMES[i]);
    if (i == menuSel) display.setDrawColor(1);
  }

  // Scroll indicators + hint
  display.setFont(u8g2_font_5x8_tr);
  if (menuScroll > 0)                          display.drawStr(122, 20, "^");
  if (menuScroll + MENU_VISIBLE < N_GAMES)     display.drawStr(122, 63, "v");
  display.drawStr(4, 62, "Up/Dn select  A=play");
  display.sendBuffer();
}

void menuUpdate() {
  if (bUp.tapped())   menuSel = (menuSel - 1 + N_GAMES) % N_GAMES;
  if (bDown.tapped()) menuSel = (menuSel + 1) % N_GAMES;
  if (bA.tapped()) {
    switch (menuSel) {
      case 0: initSnake();    state = STATE_SNAKE;    break;
      case 1: initBreakout(); state = STATE_BREAKOUT; break;
      case 2: initShooter();  state = STATE_SHOOTER;  break;
      case 3: initDoom();     state = STATE_DOOM;     break;
      case 4: initRacing();   state = STATE_RACING;   break;
      case 5: initFlappy();   state = STATE_FLAPPY;   break;
      case 6: initPong();     state = STATE_PONG;     break;
    }
  }
}

// ═══════════════════════════════════════════════
//  SNAKE
// ═══════════════════════════════════════════════
#define SN_CELL  4            // pixels per grid cell
#define SN_COLS  (128/SN_CELL)   // 32 columns
#define SN_ROWS  (56/SN_CELL)    // 14 rows  (top 8px reserved for HUD)
#define SN_MAX   120             // max snake length

struct Pt { int8_t x, y; };

Pt   snBody[SN_MAX];
int  snLen, snDir, snScore;
bool snDead;
Pt   snFood;
unsigned long snLastMove;
int  snSpeed;   // ms per step

void snPlaceFood() {
  bool ok;
  do {
    ok = true;
    snFood = { (int8_t)random(SN_COLS), (int8_t)random(SN_ROWS) };
    for (int i = 0; i < snLen; i++)
      if (snBody[i].x == snFood.x && snBody[i].y == snFood.y) { ok = false; break; }
  } while (!ok);
}

void initSnake() {
  snLen   = 4;
  snDir   = 0;   // 0=R 1=D 2=L 3=U
  snScore = 0;
  snDead  = false;
  snSpeed = 200;
  snLastMove = millis();
  snBody[0] = { 8, 7 };
  for (int i = 1; i < snLen; i++)
    snBody[i] = { (int8_t)(8 - i), 7 };
  snPlaceFood();
  setLED(0, 150, 0);
}

void snakeDraw() {
  display.clearBuffer();

  // HUD
  display.setFont(u8g2_font_5x8_tr);
  char buf[20]; sprintf(buf, "SNAKE   Score: %d", snScore);
  display.drawStr(2, 7, buf);
  display.drawHLine(0, 8, 128);

  // Food
  int fx = snFood.x * SN_CELL + SN_CELL / 2;
  int fy = snFood.y * SN_CELL + 8 + SN_CELL / 2;
  display.drawDisc(fx, fy, 2);

  // Snake body
  for (int i = 0; i < snLen; i++) {
    int px = snBody[i].x * SN_CELL + 1;
    int py = snBody[i].y * SN_CELL + 8 + 1;
    if (i == 0)
      display.drawBox(px, py, SN_CELL - 2, SN_CELL - 2);
    else
      display.drawFrame(px, py, SN_CELL - 2, SN_CELL - 2);
  }

  // Game-over overlay
  if (snDead) {
    display.drawBox(10, 20, 108, 26);
    display.setDrawColor(0);
    display.setFont(u8g2_font_7x13B_tr);
    display.drawStr(18, 35, "GAME  OVER");
    display.setDrawColor(1);
    display.setFont(u8g2_font_5x8_tr);
    display.drawStr(14, 48, "A=retry   B=menu");
  }
  display.sendBuffer();
}

void snakeUpdate() {
  if (snDead) {
    if (bA.tapped()) initSnake();
    if (bB.tapped()) { state = STATE_MENU; setLED(0, 0, 40); }
    return;
  }
  if (bB.tapped()) { state = STATE_MENU; setLED(0, 0, 40); return; }

  // Direction (prevent 180-turn)
  if (bUp.tapped()    && snDir != 1) snDir = 3;
  if (bDown.tapped()  && snDir != 3) snDir = 1;
  if (bLeft.tapped()  && snDir != 0) snDir = 2;
  if (bRight.tapped() && snDir != 2) snDir = 0;

  unsigned long now = millis();
  if (now - snLastMove < (unsigned long)snSpeed) return;
  snLastMove = now;

  // Shift body
  for (int i = snLen - 1; i > 0; i--) snBody[i] = snBody[i - 1];

  const int8_t dx[] = { 1, 0, -1,  0 };
  const int8_t dy[] = { 0, 1,  0, -1 };
  snBody[0].x += dx[snDir];
  snBody[0].y += dy[snDir];

  // Wall collision
  if (snBody[0].x < 0 || snBody[0].x >= SN_COLS ||
      snBody[0].y < 0 || snBody[0].y >= SN_ROWS) {
    snDead = true;
    setLED(150, 0, 0);
    return;
  }

  // Self collision
  for (int i = 1; i < snLen; i++) {
    if (snBody[i].x == snBody[0].x && snBody[i].y == snBody[0].y) {
      snDead = true;
      setLED(150, 0, 0);
      return;
    }
  }

  // Food eaten
  if (snBody[0].x == snFood.x && snBody[0].y == snFood.y) {
    if (snLen < SN_MAX) snLen++;
    snScore++;
    snSpeed = max(70, snSpeed - 5);
    snPlaceFood();
    setLED(random(80, 255), random(80, 255), random(80, 255));
  }
}

// ═══════════════════════════════════════════════
//  BREAKOUT
// ═══════════════════════════════════════════════
#define BK_W       128
#define BK_H        56   // physics height (Y: 0 → 56)
#define BK_YOFF      8   // draw offset (below HUD)
#define BK_COLS      8
#define BK_ROWS      4
#define BK_BRKW    (BK_W / BK_COLS)   // 16 px wide
#define BK_BRKH      5                 // px tall
#define BK_GAP       2                 // px gap between brick rows
#define BK_PAD_W    24
#define BK_PAD_H     3
#define BK_BALL_R    3

float bkBallX, bkBallY, bkVX, bkVY;
float bkPadX;
bool  bkBricks[BK_ROWS][BK_COLS];
int   bkScore, bkLives, bkLeft;
bool  bkDead, bkWon, bkReady;

void initBreakout() {
  bkPadX  = (BK_W - BK_PAD_W) / 2.0f;
  bkDead  = false;
  bkWon   = false;
  bkReady = true;
  bkScore = 0;
  bkLives = 3;
  bkLeft  = BK_ROWS * BK_COLS;
  bkVX    = 1.8f;
  bkVY    = -2.3f;

  for (int r = 0; r < BK_ROWS; r++)
    for (int c = 0; c < BK_COLS; c++)
      bkBricks[r][c] = true;

  // Ball rests on paddle
  bkBallX = bkPadX + BK_PAD_W / 2.0f;
  bkBallY = BK_H - BK_PAD_H - BK_BALL_R - 1;
  setLED(0, 0, 150);
}

void bkResetBall() {
  bkReady = true;
  bkBallX = bkPadX + BK_PAD_W / 2.0f;
  bkBallY = BK_H - BK_PAD_H - BK_BALL_R - 1;
  bkVX = 1.8f; bkVY = -2.3f;
}

void breakoutDraw() {
  display.clearBuffer();

  // HUD
  display.setFont(u8g2_font_5x8_tr);
  char buf[28]; sprintf(buf, "BREAKOUT  %d pts  [%d]", bkScore, bkLives);
  display.drawStr(2, 7, buf);
  display.drawHLine(0, BK_YOFF, 128);

  // Bricks
  for (int r = 0; r < BK_ROWS; r++) {
    for (int c = 0; c < BK_COLS; c++) {
      if (!bkBricks[r][c]) continue;
      int bx = c * BK_BRKW + 1;
      int by = BK_YOFF + r * (BK_BRKH + BK_GAP) + 3;
      display.drawBox(bx, by, BK_BRKW - 2, BK_BRKH);
    }
  }

  // Paddle
  display.drawBox((int)bkPadX,
                  BK_YOFF + BK_H - BK_PAD_H,
                  BK_PAD_W, BK_PAD_H);

  // Ball
  display.drawDisc((int)bkBallX,
                   BK_YOFF + (int)bkBallY,
                   BK_BALL_R);

  if (bkReady) {
    display.setFont(u8g2_font_5x8_tr);
    display.drawStr(22, 48, "Press A to launch");
  }

  // Overlay
  if (bkDead || bkWon) {
    display.drawBox(10, 18, 108, 28);
    display.setDrawColor(0);
    display.setFont(u8g2_font_7x13B_tr);
    display.drawStr(bkWon ? 26 : 18, 33, bkWon ? "YOU WIN!" : "GAME OVER");
    display.setDrawColor(1);
    display.setFont(u8g2_font_5x8_tr);
    display.drawStr(14, 46, "A=retry   B=menu");
  }
  display.sendBuffer();
}

void breakoutUpdate() {
  if (bkDead || bkWon) {
    if (bA.tapped()) initBreakout();
    if (bB.tapped()) { state = STATE_MENU; setLED(0, 0, 40); }
    return;
  }
  if (bB.tapped()) { state = STATE_MENU; setLED(0, 0, 40); return; }

  // Move paddle
  float padSpeed = 2.8f;
  if (bLeft.held())  bkPadX -= padSpeed;
  if (bRight.held()) bkPadX += padSpeed;
  bkPadX = constrain(bkPadX, 0.0f, (float)(BK_W - BK_PAD_W));

  if (bkReady) {
    // Ball follows paddle until launched
    bkBallX = bkPadX + BK_PAD_W / 2.0f;
    if (bA.tapped()) bkReady = false;
    return;
  }

  // Physics step
  bkBallX += bkVX;
  bkBallY += bkVY;

  // Side walls
  if (bkBallX - BK_BALL_R < 0) {
    bkBallX = BK_BALL_R;
    bkVX = fabs(bkVX);
  }
  if (bkBallX + BK_BALL_R > BK_W) {
    bkBallX = BK_W - BK_BALL_R;
    bkVX = -fabs(bkVX);
  }
  // Ceiling
  if (bkBallY - BK_BALL_R < 0) {
    bkBallY = BK_BALL_R;
    bkVY = fabs(bkVY);
  }

  // Paddle bounce
  float padTop = BK_H - BK_PAD_H;
  if (bkBallY + BK_BALL_R >= padTop && bkBallY < padTop + BK_PAD_H &&
      bkBallX >= bkPadX && bkBallX <= bkPadX + BK_PAD_W) {
    bkBallY = padTop - BK_BALL_R;
    bkVY    = -fabs(bkVY);
    // Angle based on hit position
    float rel = (bkBallX - bkPadX) / BK_PAD_W - 0.5f;
    bkVX = rel * 5.0f;
    if (fabs(bkVX) < 0.6f) bkVX = (bkVX < 0) ? -0.6f : 0.6f;
    setLED(0, 180, 200);
  }

  // Bottom — lose a life
  if (bkBallY - BK_BALL_R > BK_H) {
    bkLives--;
    if (bkLives <= 0) { bkDead = true; setLED(150, 0, 0); return; }
    setLED(200, 80, 0);
    bkResetBall();
    return;
  }

  // Brick collisions
  for (int r = 0; r < BK_ROWS; r++) {
    for (int c = 0; c < BK_COLS; c++) {
      if (!bkBricks[r][c]) continue;
      float bx  = c * BK_BRKW + 1;
      float bxe = bx + BK_BRKW - 2;
      float by  = r * (BK_BRKH + BK_GAP) + 3;
      float bye = by + BK_BRKH;

      if (bkBallX + BK_BALL_R > bx  && bkBallX - BK_BALL_R < bxe &&
          bkBallY + BK_BALL_R > by  && bkBallY - BK_BALL_R < bye) {
        bkBricks[r][c] = false;
        bkLeft--;
        bkScore += 10 + r * 5;  // higher bricks = more points

        // Reflect based on approach angle
        float midX = (bx + bxe) / 2.0f;
        float midY = (by + bye) / 2.0f;
        if (fabs(bkBallX - midX) > fabs(bkBallY - midY))
          bkVX = -bkVX;
        else
          bkVY = -bkVY;

        setLED(random(100, 255), random(100, 255), random(100, 255));

        if (bkLeft == 0) { bkWon = true; setLED(0, 200, 0); return; }
        goto brick_done;   // only hit one brick per frame
      }
    }
  }
  brick_done:;
}

// ═══════════════════════════════════════════════
//  SPACE SHOOTER
// ═══════════════════════════════════════════════
#define SH_W          128
#define SH_H           56
#define SH_YOFF         8
#define SH_MAX_BULLETS  6
#define SH_MAX_ENEMIES 12
#define SH_ECOLS        6
#define SH_EROWS        2

struct Ent { float x, y; bool alive; };

Ent   shPlayer;
Ent   shBullets[SH_MAX_BULLETS];
Ent   shEnemies[SH_MAX_ENEMIES];
int   shScore, shLives, shWave;
bool  shDead, shWon;
float shEDir;            // enemy horizontal direction (+1 or -1)
bool  shEDescend;        // enemies descend on next tick?
unsigned long shEMoveAt;
int   shEMoveMs;         // ms between enemy steps
unsigned long shLastFire;

void shSpawnWave() {
  for (auto& e : shEnemies) e.alive = false;
  int count = min(6 + shWave * 2, SH_MAX_ENEMIES);
  int cols  = min(count, SH_ECOLS);
  int rows  = (count + cols - 1) / cols;
  int n     = 0;
  for (int r = 0; r < rows && n < count; r++) {
    for (int c = 0; c < cols && n < count; c++) {
      shEnemies[n++] = { 16.0f + c * 18.0f, 10.0f + r * 11.0f, true };
    }
  }
  shEDir      = 1.0f;
  shEDescend  = false;
  shEMoveAt   = millis() + shEMoveMs;
}

void initShooter() {
  shPlayer   = { SH_W / 2.0f, (float)(SH_H - 8), true };
  shScore    = 0;
  shLives    = 3;
  shWave     = 1;
  shDead     = false;
  shWon      = false;
  shEMoveMs  = 500;
  shLastFire = 0;
  for (auto& b : shBullets) b.alive = false;
  shSpawnWave();
  setLED(120, 0, 120);
}

void shooterDraw() {
  display.clearBuffer();

  // HUD
  display.setFont(u8g2_font_5x8_tr);
  char buf[30]; sprintf(buf, "SHOOTER  %d  [%d]  W%d", shScore, shLives, shWave);
  display.drawStr(2, 7, buf);
  display.drawHLine(0, SH_YOFF, 128);

  // Player ship (simple triangle + nozzle)
  int px = (int)shPlayer.x;
  int py = SH_YOFF + (int)shPlayer.y;
  display.drawTriangle(px,     py - 6,
                        px - 5, py + 2,
                        px + 5, py + 2);
  display.drawBox(px - 1, py + 2, 3, 2);

  // Bullets
  for (const auto& b : shBullets) {
    if (!b.alive) continue;
    int bx = (int)b.x;
    int by = SH_YOFF + (int)b.y;
    display.drawBox(bx - 1, by - 4, 2, 4);
  }

  // Enemies (little alien sprites)
  for (const auto& e : shEnemies) {
    if (!e.alive) continue;
    int ex = (int)e.x;
    int ey = SH_YOFF + (int)e.y;
    // Body
    display.drawBox(ex - 4, ey - 3, 9, 7);
    // Eye cutouts
    display.setDrawColor(0);
    display.drawPixel(ex - 2, ey - 1);
    display.drawPixel(ex + 2, ey - 1);
    // Mouth cutout
    display.drawHLine(ex - 1, ey + 1, 3);
    display.setDrawColor(1);
  }

  // Overlay
  if (shDead || shWon) {
    display.drawBox(10, 18, 108, 28);
    display.setDrawColor(0);
    display.setFont(u8g2_font_7x13B_tr);
    display.drawStr(shWon ? 26 : 18, 33, shWon ? "YOU WIN!" : "GAME OVER");
    display.setDrawColor(1);
    display.setFont(u8g2_font_5x8_tr);
    display.drawStr(14, 46, "A=retry   B=menu");
  }
  display.sendBuffer();
}

void shooterUpdate() {
  if (shDead || shWon) {
    if (bA.tapped()) initShooter();
    if (bB.tapped()) { state = STATE_MENU; setLED(0, 0, 40); }
    return;
  }
  if (bB.tapped()) { state = STATE_MENU; setLED(0, 0, 40); return; }

  // Player movement
  if (bLeft.held())  shPlayer.x -= 2.5f;
  if (bRight.held()) shPlayer.x += 2.5f;
  shPlayer.x = constrain(shPlayer.x, 6.0f, (float)(SH_W - 6));

  // Fire (auto-repeat while A held)
  unsigned long now = millis();
  if (bA.held() && now - shLastFire > 280UL) {
    for (auto& b : shBullets) {
      if (!b.alive) {
        b = { shPlayer.x, shPlayer.y - 7.0f, true };
        shLastFire = now;
        break;
      }
    }
  }

  // Move bullets up
  for (auto& b : shBullets) {
    if (!b.alive) continue;
    b.y -= 3.8f;
    if (b.y < -4) b.alive = false;
  }

  // Enemy step
  if (now >= shEMoveAt) {
    shEMoveAt = now + shEMoveMs;
    if (shEDescend) {
      for (auto& e : shEnemies) if (e.alive) e.y += 7.0f;
      shEDir     = -shEDir;
      shEDescend = false;
    } else {
      float step = 7.0f * shEDir;
      bool  wall = false;
      for (auto& e : shEnemies) {
        if (!e.alive) continue;
        e.x += step;
        if (e.x < 8 || e.x > SH_W - 8) wall = true;
      }
      if (wall) shEDescend = true;
    }
  }

  // Bullet ↔ Enemy collision
  for (auto& b : shBullets) {
    if (!b.alive) continue;
    for (auto& e : shEnemies) {
      if (!e.alive) continue;
      if (fabs(b.x - e.x) < 6 && fabs(b.y - e.y) < 6) {
        b.alive = false;
        e.alive = false;
        shScore += 10 * shWave;
        setLED(random(100, 255), random(100, 255), 0);
      }
    }
  }

  // Enemy reaches bottom → lose a life
  for (auto& e : shEnemies) {
    if (!e.alive) continue;
    if (e.y >= SH_H - 10) {
      shLives--;
      if (shLives <= 0) { shDead = true; setLED(150, 0, 0); return; }
      setLED(200, 80, 0);
      shSpawnWave();
      return;
    }
  }

  // Enemy ↔ Player collision
  for (auto& e : shEnemies) {
    if (!e.alive) continue;
    if (fabs(e.x - shPlayer.x) < 8 && fabs(e.y - shPlayer.y) < 8) {
      shLives--;
      if (shLives <= 0) { shDead = true; setLED(150, 0, 0); return; }
      setLED(200, 80, 0);
      shSpawnWave();
      return;
    }
  }

  // All enemies cleared → next wave
  bool anyAlive = false;
  for (const auto& e : shEnemies) if (e.alive) { anyAlive = true; break; }
  if (!anyAlive) {
    shWave++;
    if (shWave > 5) {
      shWon = true;
      setLED(0, 200, 0);
      return;
    }
    shEMoveMs = max(150, shEMoveMs - 70);
    shSpawnWave();
    setLED(0, 180, 0);
  }
}

// ═══════════════════════════════════════════════
//  DOOM  (from-scratch mini raycaster FPS)
// ═══════════════════════════════════════════════
// This is NOT the real id Software DOOM engine — that needs a
// real CPU, MMU, a WAD full of assets, and far more RAM than an
// ESP8266 has. This is a small raycaster written from scratch,
// in the same spirit: walk a maze in first-person and shoot
// aliens, using the same 6 buttons as every other game here.
#define DM_MAPW    8
#define DM_MAPH    8
#define DM_FOV     1.0472f      // 60 degrees, in radians
#define DM_RAYS    64           // one ray per 2px screen column
#define DM_STEP    0.05f        // ray-march step size (perf vs. accuracy)
#define DM_MAXD    8.0f
#define DM_YOFF    8
#define DM_VH      56
#define DM_NENEMY  4

const char* const DM_MAP[DM_MAPH] = {
  "11111111",
  "10001101",
  "10101001",
  "10101101",
  "10000001",
  "11101011",
  "10000001",
  "11111111",
};

inline bool dmWall(int x, int y) {
  if (x < 0 || y < 0 || x >= DM_MAPW || y >= DM_MAPH) return true;
  return DM_MAP[y][x] == '1';
}

struct DmEnemy { float x, y; bool alive; };

float    dmX, dmY, dmAng;
int      dmScore, dmKills, dmLives;
bool     dmDead, dmWon;
float    dmZBuf[DM_RAYS];
unsigned long dmLastFire, dmLastHit;
DmEnemy  dmEnemies[DM_NENEMY];

void dmSpawnEnemies() {
  const float pos[DM_NENEMY][2] = { {5.5f,1.5f}, {2.5f,4.5f}, {6.5f,5.5f}, {3.5f,6.5f} };
  for (int i = 0; i < DM_NENEMY; i++)
    dmEnemies[i] = { pos[i][0], pos[i][1], true };
}

void initDoom() {
  dmX = 1.5f; dmY = 1.5f; dmAng = 0.0f;
  dmScore = 0; dmKills = 0; dmLives = 3;
  dmDead  = false; dmWon = false;
  dmLastFire = 0; dmLastHit = 0;
  dmSpawnEnemies();
  setLED(120, 0, 0);
}

// March a ray forward until it hits a wall cell; return the distance.
float dmCastRay(float ang) {
  float rdx = cosf(ang), rdy = sinf(ang);
  float dist = 0.0f;
  while (dist < DM_MAXD) {
    dist += DM_STEP;
    if (dmWall((int)(dmX + rdx * dist), (int)(dmY + rdy * dist))) return dist;
  }
  return DM_MAXD;
}

void doomDraw() {
  display.clearBuffer();

  // Cast one ray per column-pair, fill the z-buffer, draw walls.
  // Distance is shown with three shading bands since the OLED is
  // strictly 1-bit (no real grayscale) — solid / dithered / sparse.
  float startAng = dmAng - DM_FOV / 2.0f;
  for (int i = 0; i < DM_RAYS; i++) {
    float ang  = startAng + (DM_FOV * i) / (DM_RAYS - 1);
    float dist = dmCastRay(ang);
    dist *= cosf(ang - dmAng);              // correct fisheye distortion
    dmZBuf[i] = dist;

    int h    = (int)constrain(DM_VH / (dist + 0.0001f), 1, DM_VH);
    int x    = i * 2;
    int yTop = DM_YOFF + (DM_VH - h) / 2;

    if (dist < 1.6f) {
      display.drawBox(x, yTop, 2, h);
    } else if (dist < 3.2f) {
      for (int yy = 0; yy < h; yy += 2) display.drawBox(x, yTop + yy, 2, 1);
    } else {
      for (int yy = 0; yy < h; yy += 4) display.drawPixel(x, yTop + yy);
    }
  }

  // Enemy sprites, drawn farthest-first (simple painter's algorithm;
  // there are only DM_NENEMY of these so an O(n^2) sort is plenty fast).
  bool drawn[DM_NENEMY] = { false, false, false, false };
  for (int pass = 0; pass < DM_NENEMY; pass++) {
    int   best = -1;
    float bestSq = -1.0f;
    for (int i = 0; i < DM_NENEMY; i++) {
      if (!dmEnemies[i].alive || drawn[i]) continue;
      float ex = dmEnemies[i].x - dmX, ey = dmEnemies[i].y - dmY;
      float dSq = ex * ex + ey * ey;
      if (dSq > bestSq) { bestSq = dSq; best = i; }
    }
    if (best < 0) break;
    drawn[best] = true;

    float ex = dmEnemies[best].x - dmX, ey = dmEnemies[best].y - dmY;
    float dist   = sqrtf(bestSq);
    float angTo  = atan2f(ey, ex) - dmAng;
    while (angTo >  PI) angTo -= 2 * PI;
    while (angTo < -PI) angTo += 2 * PI;

    if (fabs(angTo) < DM_FOV / 2.0f && dist < DM_MAXD) {
      int col = (int)(((angTo + DM_FOV / 2.0f) / DM_FOV) * DM_RAYS);
      col = constrain(col, 0, DM_RAYS - 1);
      if (dist < dmZBuf[col] + 0.3f) {       // not hidden behind a wall
        int sx   = col * 2;
        int size = (int)constrain(18.0f / (dist + 0.2f), 3, 28);
        int sy   = DM_YOFF + DM_VH / 2 - size / 2;
        display.drawBox(sx - size / 2, sy, size, size);
        display.setDrawColor(0);
        display.drawPixel(sx - size / 4, sy + size / 3);
        display.drawPixel(sx + size / 4, sy + size / 3);
        display.drawHLine(sx - size / 4, sy + 2 * size / 3, size / 2);
        display.setDrawColor(1);
      }
    }
  }

  // HUD
  display.setFont(u8g2_font_5x8_tr);
  char buf[26]; sprintf(buf, "DOOM K:%d/%d  Lives:%d", dmKills, DM_NENEMY, dmLives);
  display.drawStr(2, 7, buf);
  display.drawHLine(0, DM_YOFF, 128);

  // Crosshair
  int cx = 64, cy = DM_YOFF + DM_VH / 2;
  display.drawPixel(cx - 1, cy); display.drawPixel(cx + 1, cy);
  display.drawPixel(cx, cy - 1); display.drawPixel(cx, cy + 1);

  // Overlay
  if (dmDead || dmWon) {
    display.drawBox(10, 18, 108, 28);
    display.setDrawColor(0);
    display.setFont(u8g2_font_7x13B_tr);
    display.drawStr(dmWon ? 20 : 18, 33, dmWon ? "LEVEL CLEAR" : "GAME OVER");
    display.setDrawColor(1);
    display.setFont(u8g2_font_5x8_tr);
    display.drawStr(14, 46, "A=retry   B=menu");
  }
  display.sendBuffer();
}

void doomUpdate() {
  if (dmDead || dmWon) {
    if (bA.tapped()) initDoom();
    if (bB.tapped()) { state = STATE_MENU; setLED(0, 0, 40); }
    return;
  }
  if (bB.tapped()) { state = STATE_MENU; setLED(0, 0, 40); return; }

  const float TURN = 0.06f;
  const float MOVE = 0.06f;
  if (bLeft.held())  dmAng -= TURN;
  if (bRight.held()) dmAng += TURN;

  float dx = cosf(dmAng) * MOVE, dy = sinf(dmAng) * MOVE;
  // Look a few steps ahead on each axis independently so the
  // player can slide along walls instead of sticking to them.
  if (bUp.held()) {
    if (!dmWall((int)(dmX + dx * 3), (int)dmY)) dmX += dx;
    if (!dmWall((int)dmX, (int)(dmY + dy * 3))) dmY += dy;
  }
  if (bDown.held()) {
    if (!dmWall((int)(dmX - dx * 3), (int)dmY)) dmX -= dx;
    if (!dmWall((int)dmX, (int)(dmY - dy * 3))) dmY -= dy;
  }

  unsigned long now = millis();

  // Fire: hit the nearest alive enemy within a narrow cone dead ahead.
  if (bA.tapped() && now - dmLastFire > 200UL) {
    dmLastFire = now;
    int   hit = -1;
    float bestDist = 999.0f;
    for (int i = 0; i < DM_NENEMY; i++) {
      if (!dmEnemies[i].alive) continue;
      float ex = dmEnemies[i].x - dmX, ey = dmEnemies[i].y - dmY;
      float dist  = sqrtf(ex * ex + ey * ey);
      float angTo = atan2f(ey, ex) - dmAng;
      while (angTo >  PI) angTo -= 2 * PI;
      while (angTo < -PI) angTo += 2 * PI;
      if (fabs(angTo) < 0.15f && dist < bestDist) { bestDist = dist; hit = i; }
    }
    if (hit >= 0) {
      dmEnemies[hit].alive = false;
      dmKills++;
      dmScore += 50;
      setLED(random(100, 255), 0, 0);
      if (dmKills >= DM_NENEMY) { dmWon = true; setLED(0, 200, 0); }
    }
  }

  // Enemy contact damage (with a cooldown so one touch = one hit)
  if (!dmWon && now - dmLastHit > 800UL) {
    for (int i = 0; i < DM_NENEMY; i++) {
      if (!dmEnemies[i].alive) continue;
      float ex = dmEnemies[i].x - dmX, ey = dmEnemies[i].y - dmY;
      if (ex * ex + ey * ey < 0.35f * 0.35f) {
        dmLastHit = now;
        dmLives--;
        setLED(200, 80, 0);
        if (dmLives <= 0) { dmDead = true; setLED(150, 0, 0); }
        break;
      }
    }
  }
}

// ═══════════════════════════════════════════════
//  RACING  (pseudo-3D lane-dodge road racer)
// ═══════════════════════════════════════════════
#define RC_LANES     3
#define RC_MAXCARS   5
#define RC_ROAD_TOP  9     // horizon y, just under the top HUD line
#define RC_ROAD_BOT  63    // bottom of screen
#define RC_TOP_HW    6.0f  // road half-width at the horizon
#define RC_BOT_HW    58.0f // road half-width at the bottom (near the player)

struct RcCar { float lane; float t; bool active; }; // t: 0=just spawned (far) .. 1=at player
RcCar rcCars[RC_MAXCARS];

float rcPlayerLane;         // smoothed 0..(RC_LANES-1)
int   rcPlayerTargetLane;
float rcSpeed;              // base t-advance per frame; ramps up over time
float rcDist;               // score, in "meters"
bool  rcCrashed;
unsigned long rcLastSpawn;

// Road-fraction of each lane center, from -1 (left edge) to +1 (right edge)
inline float rcLaneFrac(float lane) { return (lane - (RC_LANES - 1) / 2.0f) * (0.66f); }
inline float rcHalfWidth(float t)   { return RC_TOP_HW + (RC_BOT_HW - RC_TOP_HW) * t * t; }
inline int   rcScreenX(float t, float lane) {
  return 64 + (int)(rcLaneFrac(lane) * rcHalfWidth(t));
}

void initRacing() {
  for (int i = 0; i < RC_MAXCARS; i++) rcCars[i].active = false;
  rcPlayerLane = 1.0f; rcPlayerTargetLane = 1;
  rcSpeed  = 0.010f;
  rcDist   = 0;
  rcCrashed = false;
  rcLastSpawn = millis();
  setLED(0, 150, 0);
}

void racingDraw() {
  display.clearBuffer();
  display.drawHLine(0, RC_ROAD_TOP, 128);

  // Road edges + a scrolling dashed centerline, drawn scanline by
  // scanline so the road edges converge toward the horizon.
  for (int y = RC_ROAD_TOP; y <= RC_ROAD_BOT; y += 2) {
    float t  = (float)(y - RC_ROAD_TOP) / (RC_ROAD_BOT - RC_ROAD_TOP);
    float hw = rcHalfWidth(t);
    display.drawPixel(64 - (int)hw, y);
    display.drawPixel(64 + (int)hw, y);
    if (((int)(t * 20.0f + rcDist * 0.15f)) % 2 == 0) {
      display.drawPixel(64 - (int)(hw * 0.33f), y);
      display.drawPixel(64 + (int)(hw * 0.33f), y);
    }
  }

  // Opponent cars, farthest first so nearer ones draw on top
  bool drawn[RC_MAXCARS] = { false, false, false, false, false };
  for (int pass = 0; pass < RC_MAXCARS; pass++) {
    int best = -1; float bestT = 2.0f;
    for (int i = 0; i < RC_MAXCARS; i++) {
      if (!rcCars[i].active || drawn[i]) continue;
      if (rcCars[i].t < bestT) { bestT = rcCars[i].t; best = i; }
    }
    if (best < 0) break;
    drawn[best] = true;
    float t  = rcCars[best].t;
    int   cx = rcScreenX(t, rcCars[best].lane);
    int   cy = RC_ROAD_TOP + (int)((RC_ROAD_BOT - RC_ROAD_TOP) * t);
    int   w  = (int)constrain(6 + t * 20.0f, 4, 26);
    int   h  = (int)(w * 0.6f);
    display.drawBox(cx - w / 2, cy - h / 2, w, h);
  }

  // Player's car, always at the bottom of the road
  int px = rcScreenX(1.0f, rcPlayerLane);
  int py = RC_ROAD_BOT - 2, pw = 20, ph = 12;
  display.drawBox(px - pw / 2, py - ph, pw, ph);
  display.setDrawColor(0);
  display.drawBox(px - pw / 2 + 3, py - ph + 2, pw - 6, 4);  // windshield cutout
  display.setDrawColor(1);

  // HUD
  display.setFont(u8g2_font_5x8_tr);
  char buf[20]; sprintf(buf, "DIST:%d m", (int)rcDist);
  display.drawStr(2, 7, buf);

  if (rcCrashed) {
    display.drawBox(10, 20, 108, 28);
    display.setDrawColor(0);
    display.setFont(u8g2_font_7x13B_tr);
    display.drawStr(24, 35, "CRASH!");
    display.setDrawColor(1);
    display.setFont(u8g2_font_5x8_tr);
    display.drawStr(14, 45, "A=retry   B=menu");
  }
  display.sendBuffer();
}

void racingUpdate() {
  if (rcCrashed) {
    if (bA.tapped()) initRacing();
    if (bB.tapped()) { state = STATE_MENU; setLED(0, 0, 40); }
    return;
  }
  if (bB.tapped()) { state = STATE_MENU; setLED(0, 0, 40); return; }

  if (bLeft.tapped())  rcPlayerTargetLane = max(0, rcPlayerTargetLane - 1);
  if (bRight.tapped()) rcPlayerTargetLane = min(RC_LANES - 1, rcPlayerTargetLane + 1);
  rcPlayerLane += (rcPlayerTargetLane - rcPlayerLane) * 0.35f;

  // Up = accelerate (faster score, riskier), Down = brake (safer, slower)
  float accelMul = bUp.held() ? 1.6f : (bDown.held() ? 0.55f : 1.0f);
  rcSpeed = constrain(rcSpeed + 0.00002f, 0.010f, 0.05f);  // gradual difficulty ramp
  float advance = rcSpeed * accelMul;
  rcDist += advance * 100.0f;

  // Spawn new traffic; interval shrinks as base speed rises
  unsigned long now = millis();
  unsigned long interval = (unsigned long)constrain(1200.0f - rcSpeed * 15000.0f, 400.0f, 1200.0f);
  if (now - rcLastSpawn > interval) {
    rcLastSpawn = now;
    for (int i = 0; i < RC_MAXCARS; i++) {
      if (!rcCars[i].active) {
        rcCars[i] = { (float)random(0, RC_LANES), 0.0f, true };
        break;
      }
    }
  }

  // Advance traffic, check for a collision near the player
  for (int i = 0; i < RC_MAXCARS; i++) {
    if (!rcCars[i].active) continue;
    rcCars[i].t += advance;
    if (rcCars[i].t > 1.05f) { rcCars[i].active = false; continue; }
    if (rcCars[i].t > 0.85f && fabs(rcCars[i].lane - rcPlayerLane) < 0.5f) {
      rcCrashed = true;
      setLED(200, 0, 0);
      break;
    }
  }
}

// ═══════════════════════════════════════════════
//  FLAPPY  (tap-to-flap side-scroller)
// ═══════════════════════════════════════════════
#define FB_PIPE_W    12
#define FB_GAP       20
#define FB_MAXPIPES  3
#define FB_SPACING   46
#define FB_GRAVITY   0.35f
#define FB_FLAP     -3.0f
#define FB_BIRD_X    30
#define FB_CEIL_Y    9
#define FB_GND_Y     63

struct FbPipe { float x; int gapY; bool scored; };
FbPipe fbPipes[FB_MAXPIPES];
float  fbBirdY, fbVel;
int    fbScore;
bool   fbDead, fbStarted;

void initFlappy() {
  fbBirdY = 32.0f; fbVel = 0.0f;
  fbScore = 0; fbDead = false; fbStarted = false;
  for (int i = 0; i < FB_MAXPIPES; i++) {
    fbPipes[i].x      = 128.0f + i * FB_SPACING;
    fbPipes[i].gapY   = random(13, 40);
    fbPipes[i].scored = false;
  }
  setLED(0, 150, 150);
}

void flappyDraw() {
  display.clearBuffer();
  display.drawHLine(0, FB_CEIL_Y - 1, 128);
  display.drawHLine(0, FB_GND_Y, 128);

  for (int i = 0; i < FB_MAXPIPES; i++) {
    int x = (int)fbPipes[i].x, gapY = fbPipes[i].gapY;
    display.drawBox(x, FB_CEIL_Y, FB_PIPE_W, gapY - FB_CEIL_Y);
    display.drawBox(x, gapY + FB_GAP, FB_PIPE_W, FB_GND_Y - (gapY + FB_GAP));
  }

  int by = (int)fbBirdY;
  display.drawBox(FB_BIRD_X - 4, by - 4, 8, 8);
  display.setDrawColor(0);
  display.drawPixel(FB_BIRD_X + 1, by - 1);   // eye
  display.setDrawColor(1);

  display.setFont(u8g2_font_5x8_tr);
  char buf[16]; sprintf(buf, "SCORE:%d", fbScore);
  display.drawStr(2, 7, buf);

  if (!fbStarted && !fbDead) display.drawStr(22, 40, "A to flap!");

  if (fbDead) {
    display.drawBox(10, 20, 108, 28);
    display.setDrawColor(0);
    display.setFont(u8g2_font_7x13B_tr);
    display.drawStr(18, 35, "GAME OVER");
    display.setDrawColor(1);
    display.setFont(u8g2_font_5x8_tr);
    display.drawStr(14, 45, "A=retry   B=menu");
  }
  display.sendBuffer();
}

void flappyUpdate() {
  if (fbDead) {
    if (bA.tapped()) initFlappy();
    if (bB.tapped()) { state = STATE_MENU; setLED(0, 0, 40); }
    return;
  }
  if (bB.tapped()) { state = STATE_MENU; setLED(0, 0, 40); return; }

  if (bA.tapped()) {
    fbVel = FB_FLAP;
    fbStarted = true;
    setLED(0, 200, 200);
  }

  if (!fbStarted) return;   // hover in place until the first flap

  fbVel += FB_GRAVITY;
  fbBirdY += fbVel;
  if (fbBirdY < FB_CEIL_Y) { fbBirdY = FB_CEIL_Y; fbVel = 0; }
  if (fbBirdY > FB_GND_Y)  { fbDead = true; setLED(200, 0, 0); return; }

  for (int i = 0; i < FB_MAXPIPES; i++) {
    fbPipes[i].x -= 1.6f;
    if (fbPipes[i].x < -FB_PIPE_W) {
      fbPipes[i].x += FB_MAXPIPES * FB_SPACING;
      fbPipes[i].gapY   = random(13, 40);
      fbPipes[i].scored = false;
    }
    if (!fbPipes[i].scored && fbPipes[i].x + FB_PIPE_W < FB_BIRD_X) {
      fbPipes[i].scored = true;
      fbScore++;
      setLED(0, 200, 0);
    }
    bool inPipeX  = FB_BIRD_X + 4 > fbPipes[i].x && FB_BIRD_X - 4 < fbPipes[i].x + FB_PIPE_W;
    bool outOfGap = fbBirdY - 3 < fbPipes[i].gapY || fbBirdY + 3 > fbPipes[i].gapY + FB_GAP;
    if (inPipeX && outOfGap) {
      fbDead = true;
      setLED(200, 0, 0);
    }
  }
}

// ═══════════════════════════════════════════════
//  PONG  (vs. CPU paddle)
// ═══════════════════════════════════════════════
#define PG_PADDLE_H   16
#define PG_PADDLE_W   3
#define PG_PLAYER_X   4
#define PG_AI_X       (128 - 4 - PG_PADDLE_W)
#define PG_TOP        11    // leaves room for the scoreboard above
#define PG_BOT        63
#define PG_BALL       3
#define PG_WIN_SCORE  7
#define PG_MAXSPEED   4.5f

float pgPlayerY, pgAiY;
float pgBallX, pgBallY, pgBallVX, pgBallVY;
int   pgPlayerScore, pgAiScore;
bool  pgStarted, pgOver;

void pgResetBall(int dir) {  // dir: -1 serves toward the player, +1 toward the CPU
  pgBallX = 64.0f; pgBallY = 36.0f;
  float angle = random(-25, 25) * (PI / 180.0f);
  pgBallVX = dir * 1.7f * cosf(angle);
  pgBallVY = 1.7f * sinf(angle);
}

void initPong() {
  pgPlayerY = pgAiY = (PG_TOP + PG_BOT) / 2.0f - PG_PADDLE_H / 2.0f;
  pgPlayerScore = 0; pgAiScore = 0;
  pgOver = false; pgStarted = false;
  pgBallX = 64.0f; pgBallY = 36.0f; pgBallVX = 0; pgBallVY = 0;
  setLED(0, 100, 200);
}

void pongDraw() {
  display.clearBuffer();

  display.setFont(u8g2_font_6x10_tr);
  char buf[8];
  sprintf(buf, "%d", pgPlayerScore); display.drawStr(48, 8, buf);
  sprintf(buf, "%d", pgAiScore);     display.drawStr(74, 8, buf);
  display.drawHLine(0, PG_TOP - 1, 128);

  for (int y = PG_TOP; y < PG_BOT; y += 6) display.drawVLine(64, y, 3);

  display.drawBox(PG_PLAYER_X, (int)pgPlayerY, PG_PADDLE_W, PG_PADDLE_H);
  display.drawBox(PG_AI_X,     (int)pgAiY,     PG_PADDLE_W, PG_PADDLE_H);
  display.drawBox((int)pgBallX, (int)pgBallY, PG_BALL, PG_BALL);

  display.setFont(u8g2_font_5x8_tr);
  if (!pgStarted && !pgOver) display.drawStr(28, 40, "A to serve");

  if (pgOver) {
    display.drawBox(10, 20, 108, 28);
    display.setDrawColor(0);
    display.setFont(u8g2_font_7x13B_tr);
    display.drawStr(pgPlayerScore > pgAiScore ? 26 : 20, 35,
                     pgPlayerScore > pgAiScore ? "YOU WIN" : "CPU WINS");
    display.setDrawColor(1);
    display.setFont(u8g2_font_5x8_tr);
    display.drawStr(14, 45, "A=retry   B=menu");
  }
  display.sendBuffer();
}

void pongUpdate() {
  if (pgOver) {
    if (bA.tapped()) initPong();
    if (bB.tapped()) { state = STATE_MENU; setLED(0, 0, 40); }
    return;
  }
  if (bB.tapped()) { state = STATE_MENU; setLED(0, 0, 40); return; }

  const float PADDLE_SPEED = 2.2f;
  if (bUp.held())   pgPlayerY -= PADDLE_SPEED;
  if (bDown.held()) pgPlayerY += PADDLE_SPEED;
  pgPlayerY = constrain(pgPlayerY, PG_TOP, PG_BOT - PG_PADDLE_H);

  if (!pgStarted) {
    if (bA.tapped()) {
      pgStarted = true;
      pgResetBall(random(0, 2) == 0 ? -1 : 1);
    }
    return;
  }

  // Simple CPU paddle: chase the ball's y with a capped speed (beatable)
  float aiCenter = pgAiY + PG_PADDLE_H / 2.0f;
  float target   = pgBallY + PG_BALL / 2.0f;
  const float AI_SPEED = 1.7f;
  if (aiCenter < target - 2) pgAiY += AI_SPEED;
  else if (aiCenter > target + 2) pgAiY -= AI_SPEED;
  pgAiY = constrain(pgAiY, PG_TOP, PG_BOT - PG_PADDLE_H);

  pgBallX += pgBallVX;
  pgBallY += pgBallVY;

  if (pgBallY <= PG_TOP || pgBallY + PG_BALL >= PG_BOT) {
    pgBallVY = -pgBallVY;
    pgBallY  = constrain(pgBallY, PG_TOP, PG_BOT - PG_BALL);
  }

  // Player paddle collision
  if (pgBallVX < 0 && pgBallX <= PG_PLAYER_X + PG_PADDLE_W && pgBallX + PG_BALL >= PG_PLAYER_X &&
      pgBallY + PG_BALL >= pgPlayerY && pgBallY <= pgPlayerY + PG_PADDLE_H) {
    pgBallVX = constrain(-pgBallVX * 1.05f, -PG_MAXSPEED, PG_MAXSPEED);
    float rel = ((pgBallY + PG_BALL / 2.0f) - (pgPlayerY + PG_PADDLE_H / 2.0f)) / (PG_PADDLE_H / 2.0f);
    pgBallVY = rel * 2.0f;
    pgBallX  = PG_PLAYER_X + PG_PADDLE_W;
    setLED(0, 150, 255);
  }
  // CPU paddle collision
  if (pgBallVX > 0 && pgBallX + PG_BALL >= PG_AI_X && pgBallX <= PG_AI_X + PG_PADDLE_W &&
      pgBallY + PG_BALL >= pgAiY && pgBallY <= pgAiY + PG_PADDLE_H) {
    pgBallVX = constrain(-pgBallVX * 1.05f, -PG_MAXSPEED, PG_MAXSPEED);
    float rel = ((pgBallY + PG_BALL / 2.0f) - (pgAiY + PG_PADDLE_H / 2.0f)) / (PG_PADDLE_H / 2.0f);
    pgBallVY = rel * 2.0f;
    pgBallX  = PG_AI_X - PG_BALL;
    setLED(255, 150, 0);
  }

  // Scoring
  if (pgBallX < 0) {
    pgAiScore++;
    setLED(200, 0, 0);
    if (pgAiScore >= PG_WIN_SCORE) pgOver = true;
    else pgResetBall(1);
  } else if (pgBallX > 128) {
    pgPlayerScore++;
    setLED(0, 200, 0);
    if (pgPlayerScore >= PG_WIN_SCORE) pgOver = true;
    else pgResetBall(-1);
  }
}

// ═══════════════════════════════════════════════
//  BOOT SPLASH
// ═══════════════════════════════════════════════
void showSplash() {
  display.clearBuffer();
  display.setFont(u8g2_font_7x13B_tr);
  display.drawStr(20, 22, "HackGame");
  display.drawStr(12, 38, "on HackHeld VEGA");
  display.setFont(u8g2_font_5x8_tr);
  display.drawStr(18, 54, "by fafka");
  display.sendBuffer();

  // LED rainbow
  for (int i = 0; i < 60; i++) {
    uint8_t r = (sin(i * 0.15f + 0.0f) * 0.5f + 0.5f) * 180;
    uint8_t g = (sin(i * 0.15f + 2.1f) * 0.5f + 0.5f) * 180;
    uint8_t b = (sin(i * 0.15f + 4.2f) * 0.5f + 0.5f) * 180;
    setLED(r, g, b);
    delay(30);
  }
  setLED(0, 0, 40);
  delay(300);
}

// ═══════════════════════════════════════════════
//  SETUP & LOOP
// ═══════════════════════════════════════════════
void setup() {
  // Buttons (most use internal pull-up; GPIO16 has none)
  pinMode(PIN_UP,    INPUT_PULLUP);
  pinMode(PIN_DOWN,  INPUT_PULLUP);
  pinMode(PIN_RIGHT, INPUT_PULLUP);
  pinMode(PIN_A,     INPUT_PULLUP);
  pinMode(PIN_B,     INPUT_PULLUP);
  // GPIO16 (Left): no internal pull-up on this chip.
  // The HackHeld PCB routes an external pull-up to this pin.
  // If the button reads inverted, swap to INPUT and check wiring.
  pinMode(PIN_LEFT, INPUT);

  // I2C: SDA=GPIO4 (D2), SCL=GPIO5 (D1)
  Wire.begin(4, 5);
  Wire.setClock(400000);  // 400 kHz fast mode

  // OLED
  display.begin();
  display.setContrast(220);

  // NeoPixel
  pixel.begin();
  pixel.setBrightness(50);
  setLED(0, 0, 0);

  // Random seed
  randomSeed(analogRead(A0) ^ micros());

  showSplash();
}

void loop() {
  pollAll();   // ← must be first; reads all buttons once per frame

  switch (state) {
    case STATE_MENU:
      menuDraw();
      menuUpdate();
      break;

    case STATE_SNAKE:
      snakeDraw();
      snakeUpdate();
      break;

    case STATE_BREAKOUT:
      breakoutDraw();
      breakoutUpdate();
      break;

    case STATE_SHOOTER:
      shooterDraw();
      shooterUpdate();
      break;

    case STATE_DOOM:
      doomDraw();
      doomUpdate();
      break;

    case STATE_RACING:
      racingDraw();
      racingUpdate();
      break;

    case STATE_FLAPPY:
      flappyDraw();
      flappyUpdate();
      break;

    case STATE_PONG:
      pongDraw();
      pongUpdate();
      break;
  }

  delay(16);   // ~60 FPS cap
}
