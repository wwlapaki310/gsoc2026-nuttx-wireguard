/****************************************************************************
 * apps/examples/stackchan/stackchan_main.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.  The
 * ASF licenses this file to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 ****************************************************************************/

/* StackChan (M5Stack CoreS3 + servo base) demo for NuttX.
 *
 * Everything goes through the generic character drivers, so the board only
 * needs pin configuration:
 *
 *   /dev/i2c0  internal I2C (SCL=G11, SDA=G12): AXP2101, AW9523B, PY32
 *   /dev/spi3  ILI9342 LCD (SCK=G36, MOSI=G37, CS=G3, DC=G35 via CMDDATA)
 *   UART1      Feetech SCS servos (TX=G6, RX=G7, 1 Mbps), /dev/ttyS0 when
 *              the console is on USB-Serial/JTAG
 *
 * The register sequences are the ones verified on the hardware with the
 * MicroPython scripts in scripts/stackchan/ (see
 * docs/development/stackchan-hardware-check-2026-10-08.md).  The three
 * things that made the previous NuttX build fail are handled here:
 *
 *   - the PY32 in the base only answers at 100 kHz, so every I2C message
 *     carries its own frequency (struct i2c_msg_s.frequency);
 *   - the servos need ~500 ms after VM_EN before they answer;
 *   - servo replies are read with poll() and a deadline, so a missing
 *     servo is an error instead of a hung shell.
 */

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <sys/ioctl.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <sched.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include <nuttx/i2c/i2c_master.h>
#include <nuttx/spi/spi.h>
#include <nuttx/spi/spi_transfer.h>

#ifdef CONFIG_SYSTEM_NXPLAYER
#  include <nuttx/audio/audio.h>
#  include "system/nxplayer.h"
#endif

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define I2C_DEV       "/dev/i2c0"
#define SPI_DEV       "/dev/spi3"
#define SERVO_DEV     CONFIG_EXAMPLES_STACKCHAN_SERVO_DEVPATH

#define ADDR_AXP2101  0x34
#define ADDR_AW9523   0x58
#define ADDR_PY32     0x6f
#define ADDR_AW88298  0x36     /* speaker amplifier on the CoreS3 */

#define FREQ_PY32     100000   /* does not answer at 400 kHz */

/* Everything else on the bus would run at 400 kHz, but the PY32 shares
 * the bus and only ever worked when nothing used 400 kHz while it was
 * starting (MicroPython ran the whole bus at 100 kHz).  Keep the whole
 * bus at 100 kHz.
 */

#define FREQ_DEFAULT  100000
#define I2C_RETRIES   5
#define I2C_RETRY_US  10000

/* PY32 I/O expander registers (low byte = pins 0-7, high = 8-15) */

#define PY32_VERSION  0x02
#define PY32_DIR_L    0x03
#define PY32_DIR_H    0x04
#define PY32_OUT_L    0x05
#define PY32_PULL_L   0x09
#define PY32_PULL_H   0x0a
#define PY32_OD_H     0x14
#define PY32_LEDCFG   0x24
#define PY32_LEDRAM   0x30
#define PY32_VM_EN    0        /* servo power, pin 0 */
#define PY32_LED_PIN  5        /* WS2812 data, pin 13 = high byte bit 5 */
#define NLEDS         12

#define LCD_W         320
#define LCD_H         240
#define LCD_FREQ      20000000
#define LCD_CHUNK     2048

#define SERVO_PAN     1
#define SERVO_TILT    2
#define SERVO_REPLY_MS 25

#define COLOR_BLACK   0x0000
#define COLOR_WHITE   0xffff

/* Face geometry (landscape 320x240) */

#define EYE_R         16
#define EYE_LX        96
#define EYE_RX        224
#define EYE_Y         100
#define MOUTH_W       90
#define MOUTH_H       8
#define MOUTH_Y       168

/* Expressions */

#define EXPR_NEUTRAL   0
#define EXPR_HAPPY     1
#define EXPR_SAD       2
#define EXPR_ANGRY     3
#define EXPR_SLEEPY    4
#define EXPR_SURPRISED 5
#define NEXPR          6

/****************************************************************************
 * Private Types
 ****************************************************************************/

/* Per-task state.  File descriptors belong to the task group that opened
 * them, so the background demo task and each NSH invocation keep their own.
 */

struct stackchan_s
{
  int i2c;
  int spi;
  int servo;
  uint8_t pixbuf[LCD_CHUNK];
  struct spi_trans_s trans[2 + (LCD_W * LCD_H * 2) / LCD_CHUNK];
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

/* The demo loop runs as its own task; in a flat build these globals are
 * shared with the "stackchan stop" invocation.
 */

static volatile bool g_running;
static volatile pid_t g_demo_pid = -1;

/* Current expression.  While the demo runs, "stackchan face <expr>" only
 * sets these and the demo task redraws, so two tasks never draw at once.
 */

static volatile int g_expr = EXPR_NEUTRAL;
static volatile bool g_redraw;
static volatile bool g_py32_warned;

/* Set by "stackchan say" while audio plays: the demo task moves the
 * mouth instead of the command, so only one task draws.
 */

static volatile bool g_talking;

static FAR const char * const g_expr_names[NEXPR] =
{
  "neutral", "happy", "sad", "angry", "sleepy", "surprised"
};

/* Safe ranges from the official BSP: pan zero 460 +/-128 deg, tilt
 * 620 (0 deg) .. 908 (90 deg).  Kept a little inside.
 */

static const int g_limit[3][2] =
{
  {
    0, 0
  },
  {
    51, 869
  },
  {
    620, 900
  }
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int open_once(FAR int *fd, FAR const char *path, int flags)
{
  if (*fd < 0)
    {
      *fd = open(path, flags);
      if (*fd < 0)
        {
          fprintf(stderr, "stackchan: open %s: %d\n", path, errno);
          return -errno;
        }
    }

  return OK;
}

static uint32_t sc_i2c_freq(uint8_t addr)
{
  return addr == ADDR_PY32 ? FREQ_PY32 : FREQ_DEFAULT;
}

/* The PY32 stops answering for a while when it is busy, for example while
 * it clocks out the WS2812 data after an LED refresh.  Retry a few times
 * before giving up.
 */

static int sc_i2c_transfer(FAR struct stackchan_s *sc,
                           FAR struct i2c_transfer_s *xfer)
{
  int ret = -EIO;
  int retry;

  for (retry = 0; retry < I2C_RETRIES; retry++)
    {
      if (ioctl(sc->i2c, I2CIOC_TRANSFER, (unsigned long)xfer) >= 0)
        {
          return OK;
        }

      ret = -errno;
      usleep(I2C_RETRY_US);
    }

  return ret;
}

static int sc_i2c_write(FAR struct stackchan_s *sc, uint8_t addr,
                        uint8_t reg, FAR const uint8_t *data, size_t len)
{
  struct i2c_transfer_s xfer;
  struct i2c_msg_s msg;
  uint8_t buf[1 + 2 * NLEDS];

  if (len + 1 > sizeof(buf) || open_once(&sc->i2c, I2C_DEV, O_RDWR) < 0)
    {
      return -EINVAL;
    }

  buf[0] = reg;
  memcpy(&buf[1], data, len);

  msg.frequency = sc_i2c_freq(addr);
  msg.addr      = addr;
  msg.flags     = 0;
  msg.buffer    = buf;
  msg.length    = len + 1;

  xfer.msgv = &msg;
  xfer.msgc = 1;

  return sc_i2c_transfer(sc, &xfer);
}

static int sc_i2c_read(FAR struct stackchan_s *sc, uint8_t addr,
                       uint8_t reg, FAR uint8_t *data, size_t len)
{
  struct i2c_transfer_s xfer;
  struct i2c_msg_s msg[2];

  if (open_once(&sc->i2c, I2C_DEV, O_RDWR) < 0)
    {
      return -ENODEV;
    }

  msg[0].frequency = sc_i2c_freq(addr);
  msg[0].addr      = addr;
  msg[0].flags     = 0;
  msg[0].buffer    = &reg;
  msg[0].length    = 1;

  msg[1].frequency = sc_i2c_freq(addr);
  msg[1].addr      = addr;
  msg[1].flags     = I2C_M_READ;
  msg[1].buffer    = data;
  msg[1].length    = len;

  xfer.msgv = msg;
  xfer.msgc = 2;

  return sc_i2c_transfer(sc, &xfer);
}

static int sc_i2c_w8(FAR struct stackchan_s *sc, uint8_t addr,
                     uint8_t reg, uint8_t val)
{
  return sc_i2c_write(sc, addr, reg, &val, 1);
}

static int sc_i2c_setbits(FAR struct stackchan_s *sc, uint8_t addr,
                          uint8_t reg, uint8_t mask, bool on)
{
  uint8_t val;
  int ret;

  ret = sc_i2c_read(sc, addr, reg, &val, 1);
  if (ret < 0)
    {
      return ret;
    }

  val = on ? (val | mask) : (val & ~mask);
  return sc_i2c_w8(sc, addr, reg, val);
}

/* The official BSP waits for the PY32 to boot: it polls the version
 * register every 200 ms and gives up after 1.2 s.  Do the same (2 s).
 */

static int py32_wait(FAR struct stackchan_s *sc)
{
  uint8_t ver = 0;
  int i;

  for (i = 0; i < 10; i++)
    {
      if (sc_i2c_read(sc, ADDR_PY32, PY32_VERSION, &ver, 1) >= 0 &&
          ver != 0 && ver != 0xff)
        {
          return OK;
        }

      usleep(200 * 1000);
    }

  return -ETIMEDOUT;
}

/* Servo power */

static int servo_power(FAR struct stackchan_s *sc, bool on)
{
  int ret;

  ret = sc_i2c_setbits(sc, ADDR_PY32, PY32_DIR_L, 1 << PY32_VM_EN, true);
  if (ret >= 0)
    {
      ret = sc_i2c_setbits(sc, ADDR_PY32, PY32_PULL_L, 1 << PY32_VM_EN,
                           true);
    }

  if (ret >= 0)
    {
      ret = sc_i2c_setbits(sc, ADDR_PY32, PY32_OUT_L, 1 << PY32_VM_EN, on);
    }

  if (ret < 0)
    {
      /* Say it once; the demo keeps retrying and would flood the
       * console otherwise.
       */

      if (!g_py32_warned)
        {
          fprintf(stderr, "stackchan: PY32 (0x%02x) not answering: %d "
                  "(reported once)\n", ADDR_PY32, ret);
          g_py32_warned = true;
        }

      return ret;
    }

  g_py32_warned = false;

  if (on)
    {
      /* 300 ms was not enough on the hardware; 500 ms was. */

      usleep(500 * 1000);
    }

  return OK;
}

/* LEDs (WS2812 x12 driven by the PY32) */

static int led_set(FAR struct stackchan_s *sc, uint8_t r, uint8_t g,
                   uint8_t b)
{
  uint8_t ram[2 * NLEDS];
  uint16_t c;
  int ret;
  int i;

  ret = sc_i2c_setbits(sc, ADDR_PY32, PY32_DIR_H, 1 << PY32_LED_PIN, true);
  if (ret >= 0)
    {
      ret = sc_i2c_setbits(sc, ADDR_PY32, PY32_PULL_H, 1 << PY32_LED_PIN,
                           true);
    }

  if (ret >= 0)
    {
      ret = sc_i2c_setbits(sc, ADDR_PY32, PY32_OD_H, 1 << PY32_LED_PIN,
                           false);
    }

  if (ret >= 0)
    {
      ret = sc_i2c_w8(sc, ADDR_PY32, PY32_LEDCFG, NLEDS);
    }

  if (ret < 0)
    {
      return ret;
    }

  c = ((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3);
  for (i = 0; i < NLEDS; i++)
    {
      ram[2 * i]     = c & 0xff;
      ram[2 * i + 1] = c >> 8;
    }

  ret = sc_i2c_write(sc, ADDR_PY32, PY32_LEDRAM, ram, sizeof(ram));
  if (ret >= 0)
    {
      /* Latch the new colours */

      ret = sc_i2c_setbits(sc, ADDR_PY32, PY32_LEDCFG, 0x40, true);
      usleep(50 * 1000);
    }

  return ret;
}

/* LCD (ILI9342, landscape) */

static int lcd_xfer(FAR struct stackchan_s *sc,
                    FAR struct spi_trans_s *trans, int ntrans)
{
  struct spi_sequence_s seq;

  seq.dev       = SPIDEV_DISPLAY(0);
  seq.mode      = SPIDEV_MODE0;
  seq.nbits     = 8;
  seq.ntrans    = ntrans;
  seq.frequency = LCD_FREQ;
  seq.trans     = trans;

  if (ioctl(sc->spi, SPIIOC_TRANSFER, (unsigned long)&seq) < 0)
    {
      return -errno;
    }

  return OK;
}

static int lcd_cmd(FAR struct stackchan_s *sc, uint8_t cmd,
                   FAR const uint8_t *data, size_t len)
{
  struct spi_trans_s trans[2];

  memset(trans, 0, sizeof(trans));
  trans[0].cmd      = true;
  trans[0].nwords   = 1;
  trans[0].txbuffer = &cmd;
  trans[0].deselect = len == 0;

  trans[1].cmd      = false;
  trans[1].nwords   = len;
  trans[1].txbuffer = data;
  trans[1].deselect = true;

  return lcd_xfer(sc, trans, len > 0 ? 2 : 1);
}

static int lcd_fill(FAR struct stackchan_s *sc, int x, int y, int w,
                    int h, uint16_t color)
{
  FAR struct spi_trans_s *trans = sc->trans;
  uint8_t win[4];
  uint8_t ramwr = 0x2c;
  size_t total;
  size_t i;
  int n;
  int ret;

  if (w <= 0 || h <= 0)
    {
      return OK;
    }

  win[0] = x >> 8;
  win[1] = x & 0xff;
  win[2] = (x + w - 1) >> 8;
  win[3] = (x + w - 1) & 0xff;
  ret = lcd_cmd(sc, 0x2a, win, 4);

  win[0] = y >> 8;
  win[1] = y & 0xff;
  win[2] = (y + h - 1) >> 8;
  win[3] = (y + h - 1) & 0xff;
  if (ret >= 0)
    {
      ret = lcd_cmd(sc, 0x2b, win, 4);
    }

  if (ret < 0)
    {
      return ret;
    }

  for (i = 0; i < LCD_CHUNK; i += 2)
    {
      sc->pixbuf[i]     = color >> 8;
      sc->pixbuf[i + 1] = color & 0xff;
    }

  memset(sc->trans, 0, sizeof(sc->trans));
  trans[0].cmd      = true;
  trans[0].nwords   = 1;
  trans[0].txbuffer = &ramwr;

  total = (size_t)w * h * 2;
  for (n = 1; total > 0; n++)
    {
      size_t len = total > LCD_CHUNK ? LCD_CHUNK : total;

      trans[n].nwords   = len;
      trans[n].txbuffer = sc->pixbuf;
      total            -= len;
    }

  trans[n - 1].deselect = true;
  return lcd_xfer(sc, trans, n);
}

static void lcd_circle(FAR struct stackchan_s *sc, int cx, int cy, int r,
                       uint16_t color)
{
  int dy;
  int dx;

  for (dy = -r; dy <= r; dy++)
    {
      for (dx = r; dx * dx + dy * dy > r * r; dx--)
        {
        }

      lcd_fill(sc, cx - dx, cy + dy, 2 * dx + 1, 1, color);
    }
}

static int lcd_init(FAR struct stackchan_s *sc)
{
  static const uint8_t colmod = 0x55;  /* RGB565 */
  static const uint8_t madctl = 0x08;  /* BGR, landscape as mounted */
  static const uint8_t bright = 24;    /* M5GFX uses 20..28 */
  int ret;

  if (open_once(&sc->spi, SPI_DEV, O_RDWR) < 0)
    {
      return -ENODEV;
    }

  /* AXP2101: DLDO1 (backlight) on and brightness.  AW9523B P1.1 is the
   * panel reset; make sure it is released.
   */

  ret = sc_i2c_setbits(sc, ADDR_AXP2101, 0x90, 0x80, true);
  if (ret >= 0)
    {
      ret = sc_i2c_write(sc, ADDR_AXP2101, 0x99, &bright, 1);
    }

  if (ret >= 0)
    {
      ret = sc_i2c_setbits(sc, ADDR_AW9523, 0x03, 0x02, true);
    }

  if (ret >= 0)
    {
      ret = sc_i2c_setbits(sc, ADDR_AW9523, 0x05, 0x02, false);
    }

  if (ret < 0)
    {
      fprintf(stderr, "stackchan: LCD power setup failed: %d\n", ret);
      return ret;
    }

  ret = lcd_cmd(sc, 0x11, NULL, 0);                  /* SLPOUT */
  usleep(120 * 1000);
  if (ret >= 0)
    {
      ret = lcd_cmd(sc, 0x3a, &colmod, 1);
    }

  if (ret >= 0)
    {
      ret = lcd_cmd(sc, 0x36, &madctl, 1);
    }

  if (ret >= 0)
    {
      ret = lcd_cmd(sc, 0x21, NULL, 0);              /* INVON */
    }

  if (ret >= 0)
    {
      ret = lcd_cmd(sc, 0x29, NULL, 0);              /* DISPON */
    }

  if (ret < 0)
    {
      fprintf(stderr, "stackchan: LCD init failed: %d\n", ret);
    }

  return ret;
}

/* Eye and mouth areas, cleared before each redraw.  Large enough for the
 * biggest variant (surprised eyes, happy/sad mouth).
 */

#define EYE_BOX       28
#define MOUTH_X0      100
#define MOUTH_Y0      130
#define MOUTH_X1      220
#define MOUTH_Y1      230

static void face_eye(FAR struct stackchan_s *sc, int cx, bool left,
                     int expr, bool open)
{
  int y;

  lcd_fill(sc, cx - EYE_BOX, EYE_Y - EYE_BOX, 2 * EYE_BOX + 1,
           2 * EYE_BOX + 1, COLOR_BLACK);

  if (!open || expr == EXPR_SLEEPY)
    {
      lcd_fill(sc, cx - EYE_R, EYE_Y - 3, 2 * EYE_R + 1, 6, COLOR_WHITE);
      return;
    }

  switch (expr)
    {
      case EXPR_HAPPY:

        /* Upper crescent: closed, smiling eyes */

        lcd_circle(sc, cx, EYE_Y, EYE_R, COLOR_WHITE);
        lcd_circle(sc, cx, EYE_Y + 8, EYE_R, COLOR_BLACK);
        break;

      case EXPR_SAD:
        lcd_circle(sc, cx, EYE_Y + 4, EYE_R - 4, COLOR_WHITE);
        break;

      case EXPR_ANGRY:

        /* Round eye with the top cut by a brow that slopes down towards
         * the middle of the face.
         */

        lcd_circle(sc, cx, EYE_Y, EYE_R, COLOR_WHITE);
        for (y = EYE_Y - EYE_R; y <= EYE_Y; y++)
          {
            int cut = 2 * (y - (EYE_Y - EYE_R - 2));

            if (left)
              {
                lcd_fill(sc, cx - EYE_R + cut, y, 2 * EYE_R + 1 - cut, 1,
                         COLOR_BLACK);
              }
            else
              {
                lcd_fill(sc, cx - EYE_R, y, 2 * EYE_R + 1 - cut, 1,
                         COLOR_BLACK);
              }
          }
        break;

      case EXPR_SURPRISED:
        lcd_circle(sc, cx, EYE_Y, EYE_R + 6, COLOR_WHITE);
        break;

      default:
        lcd_circle(sc, cx, EYE_Y, EYE_R, COLOR_WHITE);
        break;
    }
}

static void face_eyes(FAR struct stackchan_s *sc, int expr, bool open)
{
  face_eye(sc, EYE_LX, true, expr, open);
  face_eye(sc, EYE_RX, false, expr, open);
}

static void face_mouth(FAR struct stackchan_s *sc, int expr)
{
  int cx = LCD_W / 2;

  lcd_fill(sc, MOUTH_X0, MOUTH_Y0, MOUTH_X1 - MOUTH_X0,
           MOUTH_Y1 - MOUTH_Y0, COLOR_BLACK);

  switch (expr)
    {
      case EXPR_HAPPY:

        /* Lower crescent: a smile */

        lcd_circle(sc, cx, 160, 30, COLOR_WHITE);
        lcd_circle(sc, cx, 148, 30, COLOR_BLACK);
        break;

      case EXPR_SAD:

        /* Upper crescent: a frown */

        lcd_circle(sc, cx, 196, 30, COLOR_WHITE);
        lcd_circle(sc, cx, 208, 30, COLOR_BLACK);
        break;

      case EXPR_ANGRY:
        lcd_fill(sc, cx - 30, 172, 60, 6, COLOR_WHITE);
        break;

      case EXPR_SLEEPY:
        lcd_fill(sc, cx - 15, 170, 30, 6, COLOR_WHITE);
        break;

      case EXPR_SURPRISED:
        lcd_circle(sc, cx, 175, 14, COLOR_WHITE);
        lcd_circle(sc, cx, 175, 8, COLOR_BLACK);
        break;

      default:
        lcd_fill(sc, cx - MOUTH_W / 2, MOUTH_Y, MOUTH_W, MOUTH_H,
                 COLOR_WHITE);
        break;
    }
}

static void face_expr(FAR struct stackchan_s *sc, int expr)
{
  face_eyes(sc, expr, true);
  face_mouth(sc, expr);
}

static int face_draw(FAR struct stackchan_s *sc, int expr)
{
  int ret;

  ret = lcd_init(sc);
  if (ret < 0)
    {
      return ret;
    }

  ret = lcd_fill(sc, 0, 0, LCD_W, LCD_H, COLOR_BLACK);
  face_expr(sc, expr);
  return ret;
}

static void face_blink(FAR struct stackchan_s *sc, int expr)
{
  face_eyes(sc, expr, false);
  usleep(120 * 1000);
  face_eyes(sc, expr, true);
}

static int expr_parse(FAR const char *name)
{
  int i;

  for (i = 0; i < NEXPR; i++)
    {
      if (strcmp(name, g_expr_names[i]) == 0)
        {
          return i;
        }
    }

  return -1;
}

/* Servos (Feetech SCS protocol) */

static int servo_open(FAR struct stackchan_s *sc)
{
  struct termios tio;

  if (sc->servo >= 0)
    {
      return OK;
    }

  if (open_once(&sc->servo, SERVO_DEV, O_RDWR | O_NOCTTY | O_NONBLOCK) < 0)
    {
      return -ENODEV;
    }

  if (tcgetattr(sc->servo, &tio) == 0)
    {
      cfmakeraw(&tio);
      cfsetspeed(&tio, B1000000);
      tcsetattr(sc->servo, TCSANOW, &tio);
    }

  return OK;
}

/* Send one instruction and collect the status packet.  Returns the number
 * of parameter bytes copied to params, or a negated errno (-ETIMEDOUT if
 * the servo did not answer within SERVO_REPLY_MS).
 */

static int servo_xfer(FAR struct stackchan_s *sc, uint8_t id,
                      uint8_t inst, FAR const uint8_t *args, size_t nargs,
                      FAR uint8_t *params, size_t nparams)
{
  uint8_t pkt[16];
  uint8_t rx[16];
  size_t want = 6 + nparams;
  size_t got = 0;
  struct timespec start;
  struct timespec now;
  uint8_t sum;
  size_t i;
  int ret;

  if (nargs + 6 > sizeof(pkt) || want > sizeof(rx) || servo_open(sc) < 0)
    {
      return -EINVAL;
    }

  /* Drop anything left over from an earlier, timed-out exchange */

  while (read(sc->servo, rx, sizeof(rx)) > 0)
    {
    }

  pkt[0] = 0xff;
  pkt[1] = 0xff;
  pkt[2] = id;
  pkt[3] = nargs + 2;
  pkt[4] = inst;
  memcpy(&pkt[5], args, nargs);
  for (sum = 0, i = 2; i < 5 + nargs; i++)
    {
      sum += pkt[i];
    }

  pkt[5 + nargs] = ~sum;

  if (write(sc->servo, pkt, 6 + nargs) != (ssize_t)(6 + nargs))
    {
      return -EIO;
    }

  clock_gettime(CLOCK_MONOTONIC, &start);
  while (got < want)
    {
      struct pollfd pfd;
      long elapsed;

      clock_gettime(CLOCK_MONOTONIC, &now);
      elapsed = (now.tv_sec - start.tv_sec) * 1000 +
                (now.tv_nsec - start.tv_nsec) / 1000000;
      if (elapsed >= SERVO_REPLY_MS)
        {
          return -ETIMEDOUT;
        }

      pfd.fd     = sc->servo;
      pfd.events = POLLIN;
      if (poll(&pfd, 1, SERVO_REPLY_MS - elapsed) <= 0)
        {
          continue;
        }

      ret = read(sc->servo, &rx[got], want - got);
      if (ret > 0)
        {
          got += ret;
        }
    }

  if (rx[0] != 0xff || rx[1] != 0xff || rx[2] != id)
    {
      return -EPROTO;
    }

  if (rx[4] != 0)
    {
      fprintf(stderr, "stackchan: servo %d status 0x%02x\n", id, rx[4]);
    }

  memcpy(params, &rx[5], nparams);
  return nparams;
}

static int servo_ping(FAR struct stackchan_s *sc, uint8_t id)
{
  return servo_xfer(sc, id, 0x01, NULL, 0, NULL, 0);
}

static int servo_pos(FAR struct stackchan_s *sc, uint8_t id)
{
  uint8_t args[2];
  uint8_t p[2];
  int ret;

  args[0] = 0x38;  /* present position */
  args[1] = 2;
  ret = servo_xfer(sc, id, 0x02, args, 2, p, 2);
  return ret < 0 ? ret : (p[0] << 8) | p[1];
}

static int servo_goto(FAR struct stackchan_s *sc, uint8_t id, int pos,
                      int ms)
{
  uint8_t args[7];

  if (pos < g_limit[id][0])
    {
      pos = g_limit[id][0];
    }
  else if (pos > g_limit[id][1])
    {
      pos = g_limit[id][1];
    }

  /* Goal position block at 0x2a: position, time, speed (big-endian) */

  args[0] = 0x2a;
  args[1] = pos >> 8;
  args[2] = pos & 0xff;
  args[3] = ms >> 8;
  args[4] = ms & 0xff;
  args[5] = 0;
  args[6] = 0;
  return servo_xfer(sc, id, 0x03, args, 7, NULL, 0);
}

static int servo_torque(FAR struct stackchan_s *sc, uint8_t id, bool on)
{
  uint8_t args[2];

  args[0] = 0x28;  /* torque enable */
  args[1] = on;
  return servo_xfer(sc, id, 0x03, args, 2, NULL, 0);
}

/* Speaker (AW88298 amplifier on I2S1: BCK=G34, WS=G33, DOUT=G13).
 * Register values from M5Unified's CoreS3 speaker callback.
 */

static int amp_write(FAR struct stackchan_s *sc, uint8_t reg, uint16_t val)
{
  uint8_t buf[2];

  buf[0] = val >> 8;
  buf[1] = val & 0xff;
  return sc_i2c_write(sc, ADDR_AW88298, reg, buf, 2);
}

static int amp_on(FAR struct stackchan_s *sc, uint32_t rate)
{
  static const uint8_t rate_tbl[] =
  {
    4, 5, 6, 8, 10, 11, 15, 20, 22, 44
  };

  uint32_t r = (rate + 1102) / 2205;
  uint16_t reg06 = 0;
  int ret;

  /* AW9523B P0.2 releases the amplifier from reset.  Port 0 comes up
   * open-drain; make it push-pull and the pin an output first.
   */

  ret = sc_i2c_setbits(sc, ADDR_AW9523, 0x11, 0x10, true);
  if (ret >= 0)
    {
      ret = sc_i2c_setbits(sc, ADDR_AW9523, 0x04, 0x04, false);
    }

  if (ret >= 0)
    {
      ret = sc_i2c_setbits(sc, ADDR_AW9523, 0x02, 0x04, true);
    }

  if (ret < 0)
    {
      return ret;
    }

  usleep(10 * 1000);

  while (reg06 < sizeof(rate_tbl) - 1 && r > rate_tbl[reg06])
    {
      reg06++;
    }

  ret = amp_write(sc, 0x61, 0x0673);              /* boost off */
  if (ret >= 0)
    {
      ret = amp_write(sc, 0x04, 0x4040);          /* I2S on, amp on */
    }

  if (ret >= 0)
    {
      ret = amp_write(sc, 0x05, 0x0008);          /* unmute */
    }

  if (ret >= 0)
    {
      ret = amp_write(sc, 0x06, reg06 | 0x14c0);  /* rate, 16-bit x2 */
    }

  if (ret >= 0)
    {
      ret = amp_write(sc, 0x0c, 0x0064);          /* volume */
    }

  return ret;
}

static void amp_off(FAR struct stackchan_s *sc)
{
  amp_write(sc, 0x04, 0x4000);
  sc_i2c_setbits(sc, ADDR_AW9523, 0x02, 0x04, false);
}

#ifdef CONFIG_SYSTEM_NXPLAYER
/* Play a WAV file or http:// URL and move the mouth while it plays */

static int say(FAR struct stackchan_s *sc, FAR const char *src,
               uint32_t rate)
{
  FAR struct nxplayer_s *player;
  bool open = false;
  int ret;

  ret = amp_on(sc, rate);
  if (ret < 0)
    {
      fprintf(stderr, "stackchan: speaker amplifier: %d\n", ret);
      return ret;
    }

  player = nxplayer_create();
  if (player == NULL)
    {
      amp_off(sc);
      return -ENOMEM;
    }

  ret = nxplayer_playfile(player, src, AUDIO_FMT_UNDEF, AUDIO_FMT_UNDEF);
  if (ret >= 0)
    {
      int i;

      /* The play thread sets the state to "playing" only once the stream
       * has started, so wait for that before waiting for the end.
       */

      for (i = 0; i < 50 && player->state == 0; i++)
        {
          usleep(100 * 1000);
        }

      if (player->state == 0)
        {
          ret = -ETIMEDOUT;
        }
    }

  if (ret < 0)
    {
      fprintf(stderr, "stackchan: cannot play %s: %d\n", src, ret);
    }
  else if (g_demo_pid >= 0)
    {
      /* The demo task moves the mouth */

      g_talking = true;
      while (player->state != 0)
        {
          usleep(100 * 1000);
        }

      g_talking = false;
    }
  else
    {
      if (open_once(&sc->spi, SPI_DEV, O_RDWR) >= 0)
        {
          while (player->state != 0)
            {
              open = !open;
              face_mouth(sc, open ? EXPR_SURPRISED : g_expr);
              usleep(150 * 1000);
            }

          face_mouth(sc, g_expr);
        }
    }

  /* The player goes idle once the last buffer is handed over; let the
   * I2S finish sending it before the amplifier is switched off.
   */

  usleep(800 * 1000);
  nxplayer_release(player);
  amp_off(sc);
  return ret < 0 ? ret : OK;
}
#endif

/* Demo loop */

static FAR struct stackchan_s *sc_alloc(void)
{
  FAR struct stackchan_s *sc = calloc(1, sizeof(*sc));

  if (sc != NULL)
    {
      sc->i2c   = -1;
      sc->spi   = -1;
      sc->servo = -1;
    }

  return sc;
}

static void sc_free(FAR struct stackchan_s *sc)
{
  if (sc->i2c >= 0)
    {
      close(sc->i2c);
    }

  if (sc->spi >= 0)
    {
      close(sc->spi);
    }

  if (sc->servo >= 0)
    {
      close(sc->servo);
    }

  free(sc);
}

static int demo_task(int argc, FAR char *argv[])
{
  FAR struct stackchan_s *sc;
  struct timespec ts;
  int next_blink = 30;
  int next_look = 12;
  bool talk_open = false;
  bool servo_ok;
  int tick;

  sc = sc_alloc();
  if (sc == NULL)
    {
      g_demo_pid = -1;
      return 1;
    }

  clock_gettime(CLOCK_MONOTONIC, &ts);
  srand(ts.tv_nsec);

  face_draw(sc, g_expr);
  py32_wait(sc);
  led_set(sc, 0, 0, 24);
  servo_ok = servo_power(sc, true) == OK;

  /* 100 ms ticks: blink every 3-5 s, look around every 2-4 s */

  for (tick = 0; g_running; tick++)
    {
      if (g_talking)
        {
          /* Speaking: open and close the mouth every tick */

          talk_open = !talk_open;
          face_mouth(sc, talk_open ? EXPR_SURPRISED : g_expr);
        }
      else if (talk_open)
        {
          talk_open = false;
          face_mouth(sc, g_expr);
        }

      if (g_redraw)
        {
          g_redraw = false;
          face_expr(sc, g_expr);
        }

      if (tick >= next_blink)
        {
          face_blink(sc, g_expr);
          next_blink = tick + 30 + rand() % 20;
        }

      if (tick >= next_look)
        {
          if (!servo_ok)
            {
              servo_ok = servo_power(sc, true) == OK;
            }

          if (servo_ok)
            {
              servo_goto(sc, SERVO_PAN, 400 + rand() % 121, 600);
              servo_goto(sc, SERVO_TILT, 630 + rand() % 61, 600);
              next_look = tick + 20 + rand() % 20;
            }
          else
            {
              /* The PY32 is not answering: try again every 10 s, not
               * every few seconds, so the bus is left alone meanwhile.
               */

              next_look = tick + 100;
            }
        }

      usleep(100 * 1000);
    }

  servo_goto(sc, SERVO_PAN, 460, 600);
  servo_goto(sc, SERVO_TILT, 650, 600);
  usleep(700 * 1000);
  servo_torque(sc, SERVO_PAN, false);
  servo_torque(sc, SERVO_TILT, false);
  led_set(sc, 0, 0, 0);
  sc_free(sc);
  g_demo_pid = -1;
  return 0;
}

static void usage(void)
{
  printf("Usage: stackchan <command>\n"
         "  start | stop | status     background face + head motion\n"
         "  py32                      read the PY32 version (body I/O)\n"
         "  face [<expression>]       neutral happy sad angry sleepy\n"
         "                            surprised\n"
         "  blink                     blink once\n"
         "  say <file|http://url> [rate]  play a WAV and move the mouth\n"
         "  led <r> <g> <b>           all 12 LEDs (0-255)\n"
         "  servo on | off            servo power (VM_EN) / torque off\n"
         "  servo ping                ping pan (1) and tilt (2)\n"
         "  servo pos                 read positions\n"
         "  servo move <pan> <tilt>   move (pan %d-%d, tilt %d-%d)\n",
         g_limit[1][0], g_limit[1][1], g_limit[2][0], g_limit[2][1]);
}

static void report(FAR const char *what, int ret)
{
  if (ret < 0)
    {
      printf("%s: error %d\n", what, ret);
    }
  else
    {
      printf("%s: ok\n", what);
    }
}

static int cmd_servo(FAR struct stackchan_s *sc, int argc,
                     FAR char *argv[])
{
  int id;

  if (argc < 3)
    {
      usage();
      return 1;
    }

  if (strcmp(argv[2], "on") == 0)
    {
      report("VM_EN on", servo_power(sc, true));
    }
  else if (strcmp(argv[2], "off") == 0)
    {
      report("pan torque off", servo_torque(sc, SERVO_PAN, false));
      report("tilt torque off", servo_torque(sc, SERVO_TILT, false));
    }
  else if (strcmp(argv[2], "ping") == 0)
    {
      for (id = SERVO_PAN; id <= SERVO_TILT; id++)
        {
          int ret = servo_ping(sc, id);

          printf("servo %d: %s (%d)\n", id, ret >= 0 ? "ok" : "no reply",
                 ret);
        }
    }
  else if (strcmp(argv[2], "pos") == 0)
    {
      printf("pan %d tilt %d\n", servo_pos(sc, SERVO_PAN),
             servo_pos(sc, SERVO_TILT));
    }
  else if (strcmp(argv[2], "move") == 0 && argc == 5)
    {
      report("pan", servo_goto(sc, SERVO_PAN, atoi(argv[3]), 500));
      report("tilt", servo_goto(sc, SERVO_TILT, atoi(argv[4]), 500));
    }
  else
    {
      usage();
      return 1;
    }

  return 0;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int main(int argc, FAR char *argv[])
{
  FAR struct stackchan_s *sc;
  int ret = 0;

  if (argc < 2)
    {
      usage();
      return 1;
    }

  if (strcmp(argv[1], "start") == 0)
    {
      if (g_demo_pid >= 0)
        {
          printf("already running (pid %d)\n", g_demo_pid);
          return 0;
        }

      g_running = true;
      ret = task_create("stackchan_demo",
                        CONFIG_EXAMPLES_STACKCHAN_PRIORITY,
                        CONFIG_EXAMPLES_STACKCHAN_STACKSIZE, demo_task,
                        NULL);
      if (ret < 0)
        {
          g_running = false;
          printf("task_create failed: %d\n", errno);
          return 1;
        }

      g_demo_pid = ret;
      printf("started (pid %d)\n", ret);
      return 0;
    }
  else if (strcmp(argv[1], "stop") == 0)
    {
      g_running = false;
      printf("stopping\n");
      return 0;
    }
  else if (strcmp(argv[1], "status") == 0)
    {
      printf("demo %s\n", g_demo_pid >= 0 ? "running" : "stopped");
      return 0;
    }

  sc = sc_alloc();
  if (sc == NULL)
    {
      return 1;
    }

  if (strcmp(argv[1], "py32") == 0)
    {
      uint8_t ver = 0;

      /* The official BSP treats version 0x00 / 0xff as "not booted" */

      ret = sc_i2c_read(sc, ADDR_PY32, PY32_VERSION, &ver, 1);
      if (ret < 0)
        {
          printf("PY32: no answer (%d)\n", ret);
        }
      else
        {
          printf("PY32: version 0x%02x%s\n", ver,
                 ver == 0 || ver == 0xff ? " (not running)" : "");
        }

      ret = 0;
    }
  else if (strcmp(argv[1], "face") == 0)
    {
      int expr = argc > 2 ? expr_parse(argv[2]) : g_expr;

      if (expr < 0)
        {
          usage();
          ret = 1;
        }
      else if (g_demo_pid >= 0)
        {
          /* Let the demo task draw it */

          g_expr   = expr;
          g_redraw = true;
          printf("face: %s\n", g_expr_names[expr]);
        }
      else
        {
          g_expr = expr;
          report("face", face_draw(sc, expr));
        }
    }
#ifdef CONFIG_SYSTEM_NXPLAYER
  else if (strcmp(argv[1], "say") == 0 && argc >= 3)
    {
      report("say", say(sc, argv[2],
                        argc >= 4 ? strtoul(argv[3], NULL, 10) : 16000));
    }
#endif
  else if (strcmp(argv[1], "blink") == 0)
    {
      ret = open_once(&sc->spi, SPI_DEV, O_RDWR);
      if (ret >= 0)
        {
          face_blink(sc, g_expr);
        }

      report("blink", ret);
      ret = 0;
    }
  else if (strcmp(argv[1], "led") == 0 && argc == 5)
    {
      report("led", led_set(sc, atoi(argv[2]), atoi(argv[3]),
                            atoi(argv[4])));
    }
  else if (strcmp(argv[1], "servo") == 0)
    {
      ret = cmd_servo(sc, argc, argv);
    }
  else
    {
      usage();
      ret = 1;
    }

  sc_free(sc);
  return ret;
}
