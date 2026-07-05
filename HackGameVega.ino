/*
 * ╔══════════════════════════════════════════════════════╗
 * ║       HACKHELD VEGA II — HACKGAME VEGA FIRMWARE      ║
 * ║         3 games: Snake · Breakout · Shooter          ║
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
enum State { STATE_MENU, STATE_SNAKE, STATE_BREAKOUT, STATE_SHOOTER };
State state = STATE_MENU;

// Forward declarations
void initSnake();    void snakeUpdate();    void snakeDraw();
void initBreakout(); void breakoutUpdate(); void breakoutDraw();
void initShooter();  void shooterUpdate();  void shooterDraw();

// ═══════════════════════════════════════════════
//  MAIN MENU
// ═══════════════════════════════════════════════
const char* const GAME_NAMES[] = { "Snake", "Breakout", "Shooter" };
const int N_GAMES = 3;
int menuSel = 0;

void menuDraw() {
  display.clearBuffer();

  // Title
  display.setFont(u8g2_font_7x13B_tr);
  display.drawStr(14, 13, "HACKGAME VEGA");
  display.drawHLine(0, 16, 128);

  // Game list
  display.setFont(u8g2_font_6x10_tr);
  for (int i = 0; i < N_GAMES; i++) {
    int y = 30 + i * 13;
    if (i == menuSel) {
      display.drawRBox(2, y - 9, 124, 12, 2);
      display.setDrawColor(0);
    }
    display.drawStr(22, y, GAME_NAMES[i]);
    if (i == menuSel) display.setDrawColor(1);
  }

  // Hint
  display.setFont(u8g2_font_5x8_tr);
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
  }

  delay(16);   // ~60 FPS cap
}
