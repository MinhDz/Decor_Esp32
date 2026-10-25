#include "SpaceOS_GameCenter.h"
#include <Preferences.h>

namespace SpaceOS_GameCenter {

  static DrawChromeFn    s_drawChrome = nullptr;
  static DrawThemedBoxFn s_drawThemedBox = nullptr;
  static ExitCallbackFn  s_onExit = nullptr;

  void setShellCallbacks(DrawChromeFn chromeFn, DrawThemedBoxFn boxFn, ExitCallbackFn exitFn) {
    s_drawChrome = chromeFn;
    s_drawThemedBox = boxFn;
    s_onExit = exitFn;
  }

  uint8_t        s40GameActiveId   = 0; // 0 = Sảnh chọn Game, 1 = Rắn Săn Mồi, 2 = Flappy Bird, 3 = Xếp Gạch (Tetris)
  static int     s40GameMenuCursor = 0; // 0..2 trong Sảnh chọn Game
  static unsigned long s40GameLastTickMs = 0;

  uint8_t  getActiveGameId() { return s40GameActiveId; }
  void     setActiveGameId(uint8_t id) { s40GameActiveId = id; }
  uint8_t& getActiveGameIdRef() { return s40GameActiveId; }

  // --- TRẠNG THÁI GAME 1: RẮN SĂN MỒI (SNAKE XENZIA) ---
  static const int SNAKE_COLS = 20;
  static const int SNAKE_ROWS = 17;
  static int8_t  snakeX[80], snakeY[80];
  static int     snakeLen = 4;
  static int8_t  snakeDirX = 1, snakeDirY = 0;
  static int8_t  snakeNextDirX = 1, snakeNextDirY = 0;
  static int8_t  snakeFoodX = 14, snakeFoodY = 8;
  static int     snakeScore = 0, snakeBestScore = 0;
  static bool    snakeGameOver = false, snakePaused = false;

  static void spawnSnakeFood() {
    for (int tries = 0; tries < 50; tries++) {
      int fx = random(0, SNAKE_COLS);
      int fy = random(0, SNAKE_ROWS);
      bool onBody = false;
      for (int i = 0; i < snakeLen; i++) {
        if (snakeX[i] == fx && snakeY[i] == fy) { onBody = true; break; }
      }
      if (!onBody) {
        snakeFoodX = fx;
        snakeFoodY = fy;
        return;
      }
    }
  }

  static void initSnakeGame() {
    snakeLen = 4;
    for (int i = 0; i < snakeLen; i++) {
      snakeX[i] = 8 - i;
      snakeY[i] = 8;
    }
    snakeDirX = 1; snakeDirY = 0;
    snakeNextDirX = 1; snakeNextDirY = 0;
    snakeScore = 0;
    snakeGameOver = false;
    snakePaused = false;
    spawnSnakeFood();
    s40GameLastTickMs = millis();
  }

  // --- TRẠNG THÁI GAME 2: CHIM VỖ CÁNH (FLAPPY CYBER BIRD) ---
  static float   flappyY = 100.0f, flappyVy = 0.0f;
  static float   flappyPipeX[2] = { 160.0f, 276.0f };
  static int16_t flappyPipeGapY[2] = { 96, 128 };
  static bool    flappyPipePassed[2] = { false, false };
  static int     flappyScore = 0, flappyBestScore = 0;
  static bool    flappyGameOver = false, flappyPaused = false;
  static uint8_t flappyGroundOffset = 0;

  static void initFlappyGame() {
    flappyY = 102.0f;
    flappyVy = -1.5f;
    flappyPipeX[0] = 168.0f;
    flappyPipeGapY[0] = random(68, 148);
    flappyPipePassed[0] = false;
    flappyPipeX[1] = 284.0f;
    flappyPipeGapY[1] = random(68, 148);
    flappyPipePassed[1] = false;
    flappyScore = 0;
    flappyGameOver = false;
    flappyPaused = false;
    s40GameLastTickMs = millis();
  }

  // --- TRẠNG THÁI GAME 3: XẾP GẠCH CỔ ĐIỂN (TETRIS 10x18) ---
  static const int TETRIS_COLS = 10;
  static const int TETRIS_ROWS = 18;
  static uint8_t tetrisBoard[TETRIS_ROWS][TETRIS_COLS];
  static int     tetrisCurType = 0, tetrisCurRot = 0, tetrisCurX = 3, tetrisCurY = 0;
  static int     tetrisNextType = 1;
  static int     tetrisScore = 0, tetrisBestScore = 0, tetrisLines = 0;
  static bool    tetrisGameOver = false, tetrisPaused = false;
  static unsigned long tetrisLastFallMs = 0;

  static bool s40GameScoresLoaded = false;
  static void loadGameScoresPrefsIfNeeded() {
    if (s40GameScoresLoaded) return;
    s40GameScoresLoaded = true;
    Preferences p;
    if (p.begin("s40_games", true)) {
      snakeBestScore  = p.getInt("snake", 0);
      flappyBestScore = p.getInt("flappy", 0);
      tetrisBestScore = p.getInt("tetris", 0);
      p.end();
    }
  }

  static void saveGameScoresPrefs() {
    Preferences p;
    if (p.begin("s40_games", false)) {
      p.putInt("snake", snakeBestScore);
      p.putInt("flappy", flappyBestScore);
      p.putInt("tetris", tetrisBestScore);
      p.end();
    }
  }

  void init() {
    loadGameScoresPrefsIfNeeded();
  }

  void resetHighScores() {
    snakeBestScore  = 0;
    flappyBestScore = 0;
    tetrisBestScore = 0;
    saveGameScoresPrefs();
  }

  int getSnakeBestScore()  { loadGameScoresPrefsIfNeeded(); return snakeBestScore; }
  int getFlappyBestScore() { loadGameScoresPrefsIfNeeded(); return flappyBestScore; }
  int getTetrisBestScore() { loadGameScoresPrefsIfNeeded(); return tetrisBestScore; }

  // Bảng ma trận 4x4 (16-bit) cho 7 khối gạch chuẩn (I, O, T, S, Z, J, L) x 4 góc xoay
  static const uint16_t TETRIS_SHAPES[7][4] = {
    { 0x0F00, 0x2222, 0x00F0, 0x4444 }, // I
    { 0x6600, 0x6600, 0x6600, 0x6600 }, // O
    { 0x0E40, 0x4C40, 0x4E00, 0x4640 }, // T
    { 0x06C0, 0x8C40, 0x6C00, 0x4620 }, // S
    { 0x0C60, 0x4C80, 0xC600, 0x2640 }, // Z
    { 0x0E20, 0x44C0, 0x8E00, 0x6440 }, // J
    { 0x0E80, 0xC440, 0x2E00, 0x4460 }  // L
  };
  static const uint16_t TETRIS_COLORS[8] = {
    0x0000, 0x07FF, 0xFFE0, 0xA2B7, 0x07E0, 0xF292, 0x3DF7, 0xFD20
  };

  static bool tetrisCollision(int type, int rot, int px, int py) {
    uint16_t mask = TETRIS_SHAPES[type % 7][rot % 4];
    for (int r = 0; r < 4; r++) {
      for (int c = 0; c < 4; c++) {
        if ((mask >> (15 - (r * 4 + c))) & 1) {
          int bx = px + c;
          int by = py + r;
          if (bx < 0 || bx >= TETRIS_COLS || by >= TETRIS_ROWS) return true;
          if (by >= 0 && tetrisBoard[by][bx] != 0) return true;
        }
      }
    }
    return false;
  }

  static void spawnNextTetrisPiece() {
    tetrisCurType = tetrisNextType;
    tetrisNextType = random(0, 7);
    tetrisCurRot = 0;
    tetrisCurX = 3;
    tetrisCurY = 0;
    if (tetrisCollision(tetrisCurType, tetrisCurRot, tetrisCurX, tetrisCurY)) {
      tetrisGameOver = true;
      saveGameScoresPrefs();
    }
  }

  static void initTetrisGame() {
    memset(tetrisBoard, 0, sizeof(tetrisBoard));
    tetrisScore = 0;
    tetrisLines = 0;
    tetrisGameOver = false;
    tetrisPaused = false;
    tetrisNextType = random(0, 7);
    spawnNextTetrisPiece();
    tetrisLastFallMs = millis();
    s40GameLastTickMs = millis();
  }

  static void lockAndClearTetrisPiece() {
    uint16_t mask = TETRIS_SHAPES[tetrisCurType % 7][tetrisCurRot % 4];
    for (int r = 0; r < 4; r++) {
      for (int c = 0; c < 4; c++) {
        if ((mask >> (15 - (r * 4 + c))) & 1) {
          int bx = tetrisCurX + c;
          int by = tetrisCurY + r;
          if (by >= 0 && by < TETRIS_ROWS && bx >= 0 && bx < TETRIS_COLS) {
            tetrisBoard[by][bx] = (uint8_t)(tetrisCurType + 1);
          }
        }
      }
    }
    // Kiểm tra và xóa các hàng đã lấp đầy
    int cleared = 0;
    for (int r = TETRIS_ROWS - 1; r >= 0; r--) {
      bool full = true;
      for (int c = 0; c < TETRIS_COLS; c++) {
        if (tetrisBoard[r][c] == 0) { full = false; break; }
      }
      if (full) {
        cleared++;
        for (int kr = r; kr > 0; kr--) {
          for (int c = 0; c < TETRIS_COLS; c++) {
            tetrisBoard[kr][c] = tetrisBoard[kr - 1][c];
          }
        }
        for (int c = 0; c < TETRIS_COLS; c++) tetrisBoard[0][c] = 0;
        r++; // Kiểm tra lại hàng vừa dồn xuống
      }
    }
    if (cleared > 0) {
      const int bonus[5] = { 0, 100, 300, 500, 800 };
      tetrisLines += cleared;
      tetrisScore += bonus[min(4, cleared)];
      if (tetrisScore > tetrisBestScore) tetrisBestScore = tetrisScore;
    }
    spawnNextTetrisPiece();
  }

  // Vẽ nội dung khung chơi Game (224 x 216) với độ lệch dải yOff (0, 72, 144) lên mediaVisCanvas
  static void renderGameBandToCanvas(SpaceOS::VnCanvas16* cv, int yOff) {
    cv->fillScreen(0x0842);
    cv->drawRoundRect(0, -yOff, 224, 216, 6, C_NEON_CYAN);

    if (s40GameActiveId == 1) {
      // BĂNG DẢI CHO GAME 1: RẮN SĂN MỒI
      // Thanh điểm số trên đỉnh khung chơi
      cv->fillRect(2, 2 - yOff, 220, 20, 0x10A4);
      cv->drawFastHLine(2, 22 - yOff, 220, C_NEON_CYAN);
      cv->setTextSize(1);
      cv->setTextColor(C_YELLOW);
      cv->setCursor(8, 8 - yOff);
      cv->printf("RẮN SĂN MỒI | Điểm:%d", snakeScore);
      cv->setTextColor(C_NEON_GREEN);
      cv->setCursor(150, 8 - yOff);
      cv->printf("Kỷ lục:%d", snakeBestScore);

      // Ma trận bàn chơi Rắn (20 cột x 17 hàng, ô 10x10px)
      int gx0 = 12, gy0 = 26, cell = 10;
      cv->fillRect(gx0 - 1, gy0 - 1 - yOff, SNAKE_COLS * cell + 2, SNAKE_ROWS * cell + 2, C_BLACK);
      cv->drawRect(gx0 - 1, gy0 - 1 - yOff, SNAKE_COLS * cell + 2, SNAKE_ROWS * cell + 2, 0x2965);

      // Chấm lưới mờ
      for (int r = 0; r < SNAKE_ROWS; r += 2) {
        for (int c = 0; c < SNAKE_COLS; c += 2) {
          cv->drawPixel(gx0 + c * cell + 4, gy0 + r * cell + 4 - yOff, 0x1082);
        }
      }

      // Vẽ mồi năng lượng nhấp nháy phát sáng
      int fx = gx0 + snakeFoodX * cell;
      int fy = gy0 + snakeFoodY * cell;
      if (fy + cell >= yOff && fy <= yOff + 72) {
        cv->fillCircle(fx + 4, fy + 4 - yOff, 4, C_NEON_PINK);
        cv->drawCircle(fx + 4, fy + 4 - yOff, 4, C_YELLOW);
        cv->fillRect(fx + 4, fy - 1 - yOff, 2, 2, C_NEON_GREEN);
      }

      // Vẽ thân và đầu Rắn
      for (int i = 0; i < snakeLen; i++) {
        int sx = gx0 + snakeX[i] * cell;
        int sy = gy0 + snakeY[i] * cell - yOff;
        if (sy + cell >= 0 && sy <= 72) {
          if (i == 0) {
            // Đầu Rắn Cyberpunk với 2 mắt LED
            cv->fillRoundRect(sx, sy, cell - 1, cell - 1, 3, C_YELLOW);
            cv->fillCircle(sx + 3, sy + 3, 1, C_BLACK);
            cv->fillCircle(sx + 6, sy + 3, 1, C_BLACK);
          } else {
            uint16_t bCol = (i % 2 == 0) ? C_NEON_GREEN : C_NEON_CYAN;
            cv->fillRoundRect(sx + 1, sy + 1, cell - 2, cell - 2, 2, bCol);
          }
        }
      }

      // Hộp thoại Game Over / Tạm dừng
      if (snakeGameOver || snakePaused) {
        cv->fillRoundRect(26, 82 - yOff, 172, 54, 6, 0x1949);
        cv->drawRoundRect(26, 82 - yOff, 172, 54, 6, C_YELLOW);
        cv->setTextColor(snakeGameOver ? C_NEON_PINK : C_YELLOW);
        cv->setCursor(44, 94 - yOff);
        cv->print(snakeGameOver ? "THUA CUỘC! (GAME OVER)" : "ĐANG TẠM DỪNG GAME");
        cv->setTextColor(C_WHITE);
        cv->setCursor(38, 114 - yOff);
        cv->print(snakeGameOver ? "Bấm [OK] để Chơi lại ngay" : "Bấm [OK] để Tiếp tục chơi");
      }
    } else if (s40GameActiveId == 2) {
      // BĂNG DẢI CHO GAME 2: CHIM VỖ CÁNH (FLAPPY BIRD)
      // Bầu trời đêm Cyberpunk với các ngôi sao nhỏ
      for (int s = 0; s < 18; s++) {
        int sx = (s * 41 + 17) % 220 + 2;
        int sy = (s * 33 + 11) % 170 + 24 - yOff;
        if (sy >= 0 && sy < 72) {
          cv->drawPixel(sx, sy, (s % 3 == 0) ? C_YELLOW : C_WHITE);
        }
      }

      // Vẽ 2 cặp cột chướng ngại vật (Pipes)
      const int pipeW = 26, gapH = 58;
      for (int p = 0; p < 2; p++) {
        int px = (int)flappyPipeX[p];
        if (px + pipeW < 2 || px > 222) continue;
        int topH = flappyPipeGapY[p] - gapH / 2;
        int botY = flappyPipeGapY[p] + gapH / 2;
        int botH = 198 - botY;

        // Cột trên
        if (topH > 24) {
          cv->fillRect(px, 24 - yOff, pipeW, topH - 24, 0x05E8);
          cv->drawRect(px, 24 - yOff, pipeW, topH - 24, C_NEON_GREEN);
          cv->fillRect(px - 2, topH - 8 - yOff, pipeW + 4, 8, C_NEON_GREEN);
        }
        // Cột dưới
        if (botH > 0) {
          cv->fillRect(px, botY - yOff, pipeW, botH, 0x05E8);
          cv->drawRect(px, botY - yOff, pipeW, botH, C_NEON_GREEN);
          cv->fillRect(px - 2, botY - yOff, pipeW + 4, 8, C_NEON_GREEN);
        }
      }

      // Mặt đất di chuyển (Scrolling Ground Grid)
      cv->fillRect(2, 198 - yOff, 220, 16, 0x2124);
      cv->drawFastHLine(2, 198 - yOff, 220, C_YELLOW);
      for (int gx = 2 - flappyGroundOffset; gx < 222; gx += 14) {
        if (gx >= 2) cv->drawLine(gx, 200 - yOff, gx + 6, 212 - yOff, C_NEON_AMBER);
      }

      // Vẽ chú Chim Vỗ Cánh Cyber Bird (x = 40..56, y = flappyY)
      int by = (int)flappyY - yOff;
      if (by >= -15 && by <= 85) {
        cv->fillRoundRect(40, by - 7, 16, 14, 5, C_YELLOW);
        cv->drawRoundRect(40, by - 7, 16, 14, 5, C_WHITE);
        // Cánh chim đập theo vận tốc
        int wingOff = (flappyVy < 0) ? -3 : 2;
        cv->fillRoundRect(42, by + wingOff - 2, 8, 5, 2, C_WHITE);
        // Mắt và mỏ
        cv->fillCircle(52, by - 3, 2, C_WHITE);
        cv->drawPixel(53, by - 3, C_BLACK);
        cv->fillRect(55, by - 1, 5, 4, C_NEON_PINK);
      }

      // Thanh điểm số trên đỉnh
      cv->fillRect(2, 2 - yOff, 220, 20, 0x10A4);
      cv->drawFastHLine(2, 22 - yOff, 220, C_NEON_CYAN);
      cv->setTextSize(1);
      cv->setTextColor(C_YELLOW);
      cv->setCursor(8, 8 - yOff);
      cv->printf("CHIM VỖ CÁNH | Điểm:%d", flappyScore);
      cv->setTextColor(C_NEON_GREEN);
      cv->setCursor(150, 8 - yOff);
      cv->printf("Kỷ lục:%d", flappyBestScore);

      // Hộp thoại Game Over / Tạm dừng
      if (flappyGameOver || flappyPaused) {
        cv->fillRoundRect(26, 82 - yOff, 172, 54, 6, 0x1949);
        cv->drawRoundRect(26, 82 - yOff, 172, 54, 6, C_YELLOW);
        cv->setTextColor(flappyGameOver ? C_NEON_PINK : C_YELLOW);
        cv->setCursor(44, 94 - yOff);
        cv->print(flappyGameOver ? "VA CHẠM! (GAME OVER)" : "ĐANG TẠM DỪNG GAME");
        cv->setTextColor(C_WHITE);
        cv->setCursor(38, 114 - yOff);
        cv->print(flappyGameOver ? "Bấm [OK/LÊN] để Chơi lại" : "Bấm [OK] để Tiếp tục bay");
      }
    } else if (s40GameActiveId == 3) {
      // BĂNG DẢI CHO GAME 3: XẾP GẠCH CỔ ĐIỂN (TETRIS 10x18)
      int cell = 11;
      int bx0 = 10, by0 = 6;
      int bw = TETRIS_COLS * cell;
      int bh = TETRIS_ROWS * cell;

      // Khung bảng chơi Tetris (110x198)
      cv->fillRect(bx0 - 2, by0 - 2 - yOff, bw + 4, bh + 4, C_BLACK);
      cv->drawRect(bx0 - 2, by0 - 2 - yOff, bw + 4, bh + 4, C_NEON_CYAN);

      // Vẽ các ô gạch đã cố định trên bảng chơi
      for (int r = 0; r < TETRIS_ROWS; r++) {
        int py = by0 + r * cell - yOff;
        if (py + cell < 0 || py > 72) continue;
        for (int c = 0; c < TETRIS_COLS; c++) {
          int px = bx0 + c * cell;
          uint8_t val = tetrisBoard[r][c];
          if (val == 0) {
            // Chấm mờ trong ô trống
            cv->drawPixel(px + 5, py + 5, 0x1082);
          } else {
            uint16_t col = TETRIS_COLORS[val % 8];
            cv->fillRect(px + 1, py + 1, cell - 2, cell - 2, col);
            cv->drawRect(px, py, cell - 1, cell - 1, C_WHITE);
          }
        }
      }

      // Vẽ bóng rơi (Ghost Piece) cho khối hiện tại
      if (!tetrisGameOver) {
        int ghostY = tetrisCurY;
        while (!tetrisCollision(tetrisCurType, tetrisCurRot, tetrisCurX, ghostY + 1)) {
          ghostY++;
        }
        uint16_t mask = TETRIS_SHAPES[tetrisCurType % 7][tetrisCurRot % 4];
        for (int r = 0; r < 4; r++) {
          int gy = by0 + (ghostY + r) * cell - yOff;
          if (gy + cell < 0 || gy > 72) continue;
          for (int c = 0; c < 4; c++) {
            if ((mask >> (15 - (r * 4 + c))) & 1) {
              int gx = bx0 + (tetrisCurX + c) * cell;
              cv->drawRect(gx + 1, gy + 1, cell - 3, cell - 3, 0x4208);
            }
          }
        }

        // Vẽ khối gạch đang rơi của người chơi
        uint16_t pCol = TETRIS_COLORS[(tetrisCurType % 7) + 1];
        for (int r = 0; r < 4; r++) {
          int py = by0 + (tetrisCurY + r) * cell - yOff;
          if (py + cell < 0 || py > 72) continue;
          for (int c = 0; c < 4; c++) {
            if ((mask >> (15 - (r * 4 + c))) & 1) {
              int px = bx0 + (tetrisCurX + c) * cell;
              cv->fillRect(px + 1, py + 1, cell - 2, cell - 2, pCol);
              cv->drawRect(px, py, cell - 1, cell - 1, C_WHITE);
            }
          }
        }
      }

      // Cột thông tin bên phải (Khối tiếp theo, Điểm, Hàng, Cấp)
      cv->fillRoundRect(126, 8 - yOff, 90, 200, 5, 0x10A4);
      cv->drawRoundRect(126, 8 - yOff, 90, 200, 5, C_CARD_BORDER);
      cv->setTextSize(1);
      cv->setTextColor(C_YELLOW);
      cv->setCursor(138, 15 - yOff);
      cv->print("XẾP GẠCH");

      cv->setTextColor(C_NEON_CYAN);
      cv->setCursor(134, 30 - yOff);
      cv->print("KHỐI TIẾP:");
      cv->fillRect(142, 42 - yOff, 54, 46, C_BLACK);
      cv->drawRect(142, 42 - yOff, 54, 46, 0x2965);

      // Khối gạch tiếp theo dạng mini 8x8px
      uint16_t nMask = TETRIS_SHAPES[tetrisNextType % 7][0];
      uint16_t nCol = TETRIS_COLORS[(tetrisNextType % 7) + 1];
      for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
          if ((nMask >> (15 - (r * 4 + c))) & 1) {
            cv->fillRect(149 + c * 10, 47 + r * 10 - yOff, 8, 8, nCol);
            cv->drawRect(149 + c * 10, 47 + r * 10 - yOff, 8, 8, C_WHITE);
          }
        }
      }

      cv->setTextColor(C_WHITE);
      cv->setCursor(132, 98 - yOff);
      cv->print("ĐIỂM SỐ:");
      cv->setTextColor(C_NEON_GREEN);
      cv->setCursor(132, 110 - yOff);
      cv->printf("%d", tetrisScore);

      cv->setTextColor(C_WHITE);
      cv->setCursor(132, 128 - yOff);
      cv->print("KỶ LỤC:");
      cv->setTextColor(C_YELLOW);
      cv->setCursor(132, 140 - yOff);
      cv->printf("%d", tetrisBestScore);

      cv->setTextColor(C_WHITE);
      cv->setCursor(132, 158 - yOff);
      cv->printf("HÀNG: %d", tetrisLines);
      cv->setTextColor(C_NEON_PINK);
      cv->setCursor(132, 174 - yOff);
      cv->printf("CẤP : %d", 1 + (tetrisLines / 5));

      cv->setTextColor(C_SLATE);
      cv->setCursor(130, 192 - yOff);
      cv->print("OK/LÊN: Xoay");

      // Hộp thoại Game Over / Tạm dừng Tetris
      if (tetrisGameOver || tetrisPaused) {
        cv->fillRoundRect(16, 82 - yOff, 192, 54, 6, 0x1949);
        cv->drawRoundRect(16, 82 - yOff, 192, 54, 6, C_YELLOW);
        cv->setTextColor(tetrisGameOver ? C_NEON_PINK : C_YELLOW);
        cv->setCursor(44, 94 - yOff);
        cv->print(tetrisGameOver ? "ĐẦY CỘT! (GAME OVER)" : "ĐANG TẠM DỪNG GAME");
        cv->setTextColor(C_WHITE);
        cv->setCursor(38, 114 - yOff);
        cv->print(tetrisGameOver ? "Bấm [OK] để Chơi ván mới" : "Bấm [MENU/OK] Tiếp tục");
      }
    }
  }

  void drawPlayfieldOnly(Adafruit_SPITFT* tft, SpaceOS::VnCanvas16*& canvas) {
    if (!tft || s40GameActiveId == 0) return;
    if (!canvas) {
      canvas = new SpaceOS::VnCanvas16(224, 82);
    }
    if (!canvas) return;

    // Kết xuất 3 dải ngang (3 x 72px = 224 x 216px) chống nháy 100% và không tốn thêm RAM
    for (int band = 0; band < 3; band++) {
      int yOff = band * 72;
      renderGameBandToCanvas(canvas, yOff);
      tft->drawRGBBitmap(8, 54 + yOff, canvas->getBuffer(), 224, 72);
    }
  }

  static int lastDrawnGameMenuCursor = -1;

  static void drawSingleGameMenuCard(Adafruit_SPITFT* tft, int i, bool sel) {
    if (!tft || i < 0 || i >= 3) return;
    int W = tft->width();
    int ry = 56 + i * 78;
    if (s_drawThemedBox) {
      s_drawThemedBox(8, ry, W - 16, 72, sel);
    } else {
      tft->fillRoundRect(8, ry, W - 16, 72, 6, sel ? C_CARD_SEL : C_CARD_BG);
      tft->drawRoundRect(8, ry, W - 16, 72, 6, sel ? C_NEON_CYAN : C_CARD_BORDER);
    }

    // Vẽ biểu tượng đại diện từng Game ở góc trái thẻ
    int icx = 30, icy = ry + 36;
    if (i == 0) {
      // Icon Rắn Săn Mồi
      tft->fillRoundRect(icx - 14, icy - 14, 28, 28, 5, C_BLACK);
      tft->drawRoundRect(icx - 14, icy - 14, 28, 28, 5, C_NEON_GREEN);
      tft->fillRect(icx - 8, icy, 12, 5, C_NEON_GREEN);
      tft->fillRect(icx, icy - 8, 5, 13, C_NEON_CYAN);
      tft->fillRect(icx, icy - 8, 9, 5, C_YELLOW);
      tft->fillCircle(icx - 6, icy - 6, 3, C_NEON_PINK);
    } else if (i == 1) {
      // Icon Flappy Bird
      tft->fillRoundRect(icx - 14, icy - 14, 28, 28, 5, C_BLACK);
      tft->drawRoundRect(icx - 14, icy - 14, 28, 28, 5, C_YELLOW);
      tft->fillRoundRect(icx - 7, icy - 5, 14, 10, 4, C_YELLOW);
      tft->fillCircle(icx + 3, icy - 2, 2, C_WHITE);
      tft->fillRect(icx + 6, icy - 1, 4, 3, C_NEON_PINK);
    } else {
      // Icon Xếp Gạch Tetris
      tft->fillRoundRect(icx - 14, icy - 14, 28, 28, 5, C_BLACK);
      tft->drawRoundRect(icx - 14, icy - 14, 28, 28, 5, C_NEON_CYAN);
      tft->fillRect(icx - 9, icy + 1, 6, 6, C_NEON_PINK);
      tft->fillRect(icx - 3, icy + 1, 6, 6, C_NEON_PINK);
      tft->fillRect(icx + 3, icy + 1, 6, 6, C_NEON_PINK);
      tft->fillRect(icx - 3, icy - 5, 6, 6, C_YELLOW);
    }

    const char* gTitles[3] = {
      "1. Rắn Săn Mồi (Snake Xenzia)",
      "2. Chim Vỗ Cánh (Flappy Bird)",
      "3. Xếp Gạch Cổ Điển (Tetris)"
    };
    const char* gSub[3] = {
      "Điều khiển 4 hướng ăn mồi năng lượng",
      "Bấm [OK / LÊN] vỗ cánh vượt ống nước",
      "Xoay khối [OK/LÊN], dịch Trái/Phải/Xuống"
    };
    int bests[3] = { snakeBestScore, flappyBestScore, tetrisBestScore };

    uint16_t bg = sel ? C_CARD_SEL : C_CARD_BG;
    tft->setTextSize(1);
    tft->setTextColor(sel ? C_YELLOW : C_WHITE, bg);
    tft->setCursor(52, ry + 12);
    tft->print(gTitles[i]);

    tft->setTextColor(C_NEON_CYAN, bg);
    tft->setCursor(52, ry + 30);
    tft->print(gSub[i]);

    tft->setTextColor(C_NEON_GREEN, bg);
    tft->setCursor(52, ry + 48);
    tft->printf("Kỷ lục cao nhất: %d điểm", bests[i]);
  }

  void draw(Adafruit_SPITFT* tft, SpaceOS::VnCanvas16*& canvas, bool fullRedraw) {
    if (!tft) return;
    loadGameScoresPrefsIfNeeded();
    int W = tft->width();

    if (s40GameActiveId == 0) {
      // SẢNH CHỌN 3 TRÒ CHƠI GIẢI TRÍ (GAME HUB)
      if (fullRedraw) {
        tft->fillRect(0, 22, W, tft->height() - 44, C_DARK_BG);
        if (s_drawChrome) {
          s_drawChrome("TRUNG TÂM TRÒ CHƠI", "[OK:Chơi]", "Lên/Xuống", "[EXIT:Menu]");
        }

        tft->fillRoundRect(6, 26, W - 12, 24, 5, 0x1949);
        tft->drawRoundRect(6, 26, W - 12, 24, 5, C_NEON_PINK);
        tft->setTextSize(1);
        tft->setTextColor(C_YELLOW, 0x1949);
        tft->setCursor(14, 34);
        tft->print("CHỌN TRÒ CHƠI GIẢI TRÍ (3 GAME):");

        for (int i = 0; i < 3; i++) {
          drawSingleGameMenuCard(tft, i, i == s40GameMenuCursor);
        }
        lastDrawnGameMenuCursor = s40GameMenuCursor;
      } else {
        if (lastDrawnGameMenuCursor != s40GameMenuCursor) {
          if (lastDrawnGameMenuCursor >= 0 && lastDrawnGameMenuCursor < 3) {
            drawSingleGameMenuCard(tft, lastDrawnGameMenuCursor, false);
          }
          drawSingleGameMenuCard(tft, s40GameMenuCursor, true);
          lastDrawnGameMenuCursor = s40GameMenuCursor;
        }
      }
      return;
    }

    // ĐANG CHƠI 1 TRONG 3 GAME (s40GameActiveId == 1, 2, 3)
    if (fullRedraw) {
      tft->fillRect(0, 22, W, tft->height() - 44, C_DARK_BG);
      const char* gHdr[4] = { "", "GAME 1: RẮN SĂN MỒI", "GAME 2: CHIM VỖ CÁNH", "GAME 3: XẾP GẠCH" };
      if (s_drawChrome) {
        s_drawChrome(gHdr[s40GameActiveId % 4], "[MENU:Dừng]", "[OK:ThaoTác]", "[EXIT:Sảnh]");
      }

      tft->fillRoundRect(8, 26, W - 16, 24, 4, C_CARD_BG);
      tft->drawRoundRect(8, 26, W - 16, 24, 4, C_CARD_BORDER);
      tft->setTextSize(1);
      tft->setTextColor(C_NEON_AMBER, C_CARD_BG);
      tft->setCursor(14, 34);
      if (s40GameActiveId == 1)      tft->print("4 Hướng: Lái Rắn | OK: Dừng/Chơi lại");
      else if (s40GameActiveId == 2) tft->print("Bấm [OK] hoặc [LÊN]: Vỗ cánh bay lên!");
      else                           tft->print("Trái/Phải: Dịch | OK/Lên: Xoay | Xuống: Hạ");

      tft->fillRoundRect(8, 273, W - 16, 20, 4, C_CARD_BG);
      tft->setTextColor(C_SLATE, C_CARD_BG);
      tft->setCursor(16, 279);
      tft->print("Bấm [EXIT] để quay về Sảnh chọn Trò chơi");
    }
    drawPlayfieldOnly(tft, canvas);
  }

  void loop(Adafruit_SPITFT* tft, SpaceOS::VnCanvas16*& canvas, unsigned long now) {
    if (s40GameActiveId == 0) return;

    if (s40GameActiveId == 1) {
      // Vòng lặp Rắn Săn Mồi (tốc độ 130ms -> 75ms tùy theo điểm)
      unsigned long stepMs = max(75UL, 130UL - (unsigned long)(snakeScore / 2));
      if (now - s40GameLastTickMs >= stepMs) {
        s40GameLastTickMs = now;
        if (!snakeGameOver && !snakePaused) {
          snakeDirX = snakeNextDirX;
          snakeDirY = snakeNextDirY;
          int nx = (snakeX[0] + snakeDirX + SNAKE_COLS) % SNAKE_COLS;
          int ny = (snakeY[0] + snakeDirY + SNAKE_ROWS) % SNAKE_ROWS;

          // Kiểm tra tự cắn đuôi
          for (int i = 0; i < snakeLen; i++) {
            if (snakeX[i] == nx && snakeY[i] == ny) {
              snakeGameOver = true;
              saveGameScoresPrefs();
              drawPlayfieldOnly(tft, canvas);
              return;
            }
          }

          // Dịch chuyển thân rắn
          if (nx == snakeFoodX && ny == snakeFoodY) {
            if (snakeLen < 78) snakeLen++;
            for (int i = snakeLen - 1; i > 0; i--) {
              snakeX[i] = snakeX[i - 1];
              snakeY[i] = snakeY[i - 1];
            }
            snakeX[0] = nx;
            snakeY[0] = ny;
            snakeScore += 10;
            if (snakeScore > snakeBestScore) snakeBestScore = snakeScore;
            spawnSnakeFood();
          } else {
            for (int i = snakeLen - 1; i > 0; i--) {
              snakeX[i] = snakeX[i - 1];
              snakeY[i] = snakeY[i - 1];
            }
            snakeX[0] = nx;
            snakeY[0] = ny;
          }
          drawPlayfieldOnly(tft, canvas);
        }
      }
    } else if (s40GameActiveId == 2) {
      // Vòng lặp Vật lý Chim Vỗ Cánh Flappy Bird (55ms = ~18 FPS mượt)
      if (now - s40GameLastTickMs >= 55) {
        s40GameLastTickMs = now;
        if (!flappyGameOver && !flappyPaused) {
          flappyGroundOffset = (flappyGroundOffset + 3) % 14;
          flappyVy += 0.46f;
          if (flappyVy > 5.2f) flappyVy = 5.2f;
          flappyY += flappyVy;

          // Kiểm tra chạm trần hoặc chạm đất
          if (flappyY < 30.0f) { flappyY = 30.0f; flappyVy = 0.0f; }
          if (flappyY >= 191.0f) {
            flappyY = 191.0f;
            flappyGameOver = true;
            saveGameScoresPrefs();
          }

          // Di chuyển 2 cặp ống nước & kiểm tra va chạm
          const int pipeW = 26, gapH = 58;
          for (int p = 0; p < 2; p++) {
            flappyPipeX[p] -= 2.5f;
            if (!flappyPipePassed[p] && (flappyPipeX[p] + pipeW) < 40.0f) {
              flappyPipePassed[p] = true;
              flappyScore++;
              if (flappyScore > flappyBestScore) flappyBestScore = flappyScore;
            }
            if (flappyPipeX[p] < -30.0f) {
              int other = 1 - p;
              flappyPipeX[p] = max(224.0f, flappyPipeX[other] + 118.0f);
              flappyPipeGapY[p] = random(66, 150);
              flappyPipePassed[p] = false;
            }

            // Kiểm tra khung va chạm với chú chim (x = 40..55, y = flappyY-6..flappyY+6)
            if (55.0f >= flappyPipeX[p] && 40.0f <= (flappyPipeX[p] + pipeW)) {
              float topEdge = (float)(flappyPipeGapY[p] - gapH / 2);
              float botEdge = (float)(flappyPipeGapY[p] + gapH / 2);
              if ((flappyY - 5.0f) < topEdge || (flappyY + 5.0f) > botEdge) {
                flappyGameOver = true;
                saveGameScoresPrefs();
              }
            }
          }
          drawPlayfieldOnly(tft, canvas);
        }
      }
    } else if (s40GameActiveId == 3) {
      // Vòng lặp Xếp Gạch Tetris
      unsigned long fallInterval = max(160UL, 520UL - (unsigned long)(tetrisLines / 5) * 45UL);
      if (!tetrisGameOver && !tetrisPaused && (now - tetrisLastFallMs >= fallInterval)) {
        tetrisLastFallMs = now;
        if (!tetrisCollision(tetrisCurType, tetrisCurRot, tetrisCurX, tetrisCurY + 1)) {
          tetrisCurY++;
        } else {
          lockAndClearTetrisPiece();
        }
        drawPlayfieldOnly(tft, canvas);
      }
    }
  }

  void handleKey(int keyIndex, Adafruit_SPITFT* tft, SpaceOS::VnCanvas16*& canvas) {
    if (s40GameActiveId == 0) {
      // Đang ở Sảnh chọn 3 Game
      if (keyIndex == 1 || keyIndex == 3) {
        s40GameMenuCursor = (s40GameMenuCursor + 2) % 3;
        draw(tft, canvas, false);
      } else if (keyIndex == 2 || keyIndex == 4) {
        s40GameMenuCursor = (s40GameMenuCursor + 1) % 3;
        draw(tft, canvas, false);
      } else if (keyIndex == 0) {
        s40GameActiveId = (uint8_t)(s40GameMenuCursor + 1);
        if (s40GameActiveId == 1)      initSnakeGame();
        else if (s40GameActiveId == 2) initFlappyGame();
        else if (s40GameActiveId == 3) initTetrisGame();
        draw(tft, canvas, true);
      } else if (keyIndex == 6 || keyIndex == 5) {
        if (s_onExit) s_onExit();
      }
      return;
    }

    // Phím EXIT (keyIndex == 6) khi đang trong Game -> Quay lại Sảnh chọn 3 Game
    if (keyIndex == 6) {
      saveGameScoresPrefs();
      s40GameActiveId = 0;
      draw(tft, canvas, true);
      return;
    }

    if (s40GameActiveId == 1) {
      // Điều khiển Rắn Săn Mồi
      if (keyIndex == 5) {
        snakePaused = !snakePaused;
        drawPlayfieldOnly(tft, canvas);
      } else if (keyIndex == 0) {
        if (snakeGameOver) initSnakeGame();
        else snakePaused = !snakePaused;
        drawPlayfieldOnly(tft, canvas);
      } else if (keyIndex == 1 && snakeDirY == 0) { // UP
        snakeNextDirX = 0; snakeNextDirY = -1;
      } else if (keyIndex == 2 && snakeDirY == 0) { // DOWN
        snakeNextDirX = 0; snakeNextDirY = 1;
      } else if (keyIndex == 3 && snakeDirX == 0) { // LEFT
        snakeNextDirX = -1; snakeNextDirY = 0;
      } else if (keyIndex == 4 && snakeDirX == 0) { // RIGHT
        snakeNextDirX = 1; snakeNextDirY = 0;
      }
    } else if (s40GameActiveId == 2) {
      // Điều khiển Chim Vỗ Cánh (Flappy Bird)
      if (keyIndex == 5) {
        flappyPaused = !flappyPaused;
        drawPlayfieldOnly(tft, canvas);
      } else if (keyIndex == 0 || keyIndex == 1) { // OK hoặc LÊN: Vỗ cánh!
        if (flappyGameOver) {
          initFlappyGame();
        } else if (flappyPaused) {
          flappyPaused = false;
        } else {
          flappyVy = -4.1f;
        }
        drawPlayfieldOnly(tft, canvas);
      }
    } else if (s40GameActiveId == 3) {
      // Điều khiển Xếp Gạch (Tetris)
      if (keyIndex == 5) {
        tetrisPaused = !tetrisPaused;
        drawPlayfieldOnly(tft, canvas);
      } else if (tetrisGameOver) {
        if (keyIndex == 0) {
          initTetrisGame();
          drawPlayfieldOnly(tft, canvas);
        }
      } else if (tetrisPaused) {
        if (keyIndex == 0) {
          tetrisPaused = false;
          drawPlayfieldOnly(tft, canvas);
        }
      } else {
        if (keyIndex == 3) { // LEFT
          if (!tetrisCollision(tetrisCurType, tetrisCurRot, tetrisCurX - 1, tetrisCurY)) {
            tetrisCurX--;
            drawPlayfieldOnly(tft, canvas);
          }
        } else if (keyIndex == 4) { // RIGHT
          if (!tetrisCollision(tetrisCurType, tetrisCurRot, tetrisCurX + 1, tetrisCurY)) {
            tetrisCurX++;
            drawPlayfieldOnly(tft, canvas);
          }
        } else if (keyIndex == 1 || keyIndex == 0) { // UP hoặc OK: Xoay khối gạch 90 độ
          int nextRot = (tetrisCurRot + 1) % 4;
          if (!tetrisCollision(tetrisCurType, nextRot, tetrisCurX, tetrisCurY)) {
            tetrisCurRot = nextRot;
          } else if (!tetrisCollision(tetrisCurType, nextRot, tetrisCurX - 1, tetrisCurY)) {
            tetrisCurX--;
            tetrisCurRot = nextRot;
          } else if (!tetrisCollision(tetrisCurType, nextRot, tetrisCurX + 1, tetrisCurY)) {
            tetrisCurX++;
            tetrisCurRot = nextRot;
          }
          drawPlayfieldOnly(tft, canvas);
        } else if (keyIndex == 2) { // DOWN: Hạ nhanh
          if (!tetrisCollision(tetrisCurType, tetrisCurRot, tetrisCurX, tetrisCurY + 1)) {
            tetrisCurY++;
            tetrisScore += 1;
            if (tetrisScore > tetrisBestScore) tetrisBestScore = tetrisScore;
          } else {
            lockAndClearTetrisPiece();
          }
          tetrisLastFallMs = millis();
          drawPlayfieldOnly(tft, canvas);
        }
      }
    }
  }

} // namespace SpaceOS_GameCenter
