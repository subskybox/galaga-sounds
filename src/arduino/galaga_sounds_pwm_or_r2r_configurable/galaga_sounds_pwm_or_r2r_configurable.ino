/*
  Galaga Sounds

  Output options (compile-time selectable):

  1) PWM DAC on D11 (OC2A / Timer2) followed by a passive RC low-pass filter

     Recommended RC low-pass filter:
       - R ≈ 10 kΩ (series from D11 to audio out)
       - C ≈ 10 nF (from audio out to GND)

     This forms a first-order low-pass filter with a cutoff around:
         fc ≈ 1 / (2πRC) ≈ 1.6 kHz

     Notes on the PWM filter:
     - The PWM carrier is ~62.5 kHz, so even a simple RC filter is sufficient
       to remove most of the PWM energy.
     - Using a larger capacitor (e.g. 22 nF) further smooths the output but
       audibly dulls the sound (“warmer” but less authentic).
     - 10 nF provides a good balance: reduced PWM noise while preserving
       the original Galaga brightness and bite.

  2) 6-bit resistor DAC (binary-weighted / Namco-style) on D2–D7

     Recommended resistor values (MSB → LSB):
       - D2 → 4.7 kΩ
       - D3 → 2.2 kΩ
       - D4 → 1.0 kΩ
       - D5 →   470 Ω
       - D6 →   220 Ω
       - D7 →   100 Ω (or 120 Ω)

     All resistors are summed at a single node to form the audio output.
     This follows the style used in many early Namco sound boards and
     intentionally introduces slight non-linearity for a more authentic,
     crunchy sound when multiple voices mix.

     Optional smoothing:
       - Add a small capacitor (≈ 1 nF – 4.7 nF) from the DAC output node
         to GND to reduce high-frequency hash.

  General notes:
  - The output is intended to feed a high-impedance input (amplifier or
    powered speaker). Do not drive speakers directly.

  Serial:
    Send "14"   + Enter to play sound 14 (decimal)
    Send "0x14" + Enter to play sound 0x14 (hex)
    Send "stop" to stop current sound

    Send "m7" + Enter to enable all voices (mask bits v0..v2)
    Send "m1"/"m2"/"m4" to solo a voice, "m0" to mute all

  Implementation notes:
  - This uses the original Galaga tables derived from the Z80 sound CPU.
  - This is mostly a port from Fred Vecoven’s PIC18 implementation:
      https://www.vecoven.com/elec/galaga/code/gal_pic.html
  - Volume is applied in software, then quantized back to 4-bit to match
    the original hardware’s crunchy character.
  - Audio sample (waveform update) rate: 62,500 Hz using Timer1 CTC
    (OCR1A = 255).
  - Explosion sound (23) is not a perfect emulation of the NAMCO 54XX Noise Generator

  Author:
    subskybox@gmail.com
*/


#include <Arduino.h>
#include <avr/pgmspace.h>


// --- Debug: voice output mask ---
// Bit0=voice0, Bit1=voice1, Bit2=voice2. Default 0x07 (all enabled).
volatile uint8_t gVoiceOutMask = 0x07;
// Pitch scaling used by the original Galaga sound logic (inc *= 3/2).
#define PITCH_NUM 393
#define PITCH_DEN 256 // 393/256 = 1.53515625

// Optional per-sound volume trims (0 = none)
#define SOUND10_VOL_TRIM 5  // reduce raw volume by 1 step for sound 10

// -------------------- Sound 23: Explosion (standalone-tuned) --------------------
// This is a special sound (not in the original 0-22 Galaga scheduler tables).
// It generates "brown-ish" noise with a smooth 2-stage decay envelope.
// The envelope timing is derived inside the 62.5kHz audio ISR, so it does not
// depend on Timer0 (micros()/millis()) and stays stable even during Serial I/O.

#define SOUND23_ID             23

#define S23_EXPLOSION_TICKS    250    // ~2.08s at 120 Hz (derived in ISR)
#define S23_NOISE_DIV          16     // update LFSR every N samples (lower CPU, darker)
#define S23_BROWN_LEAK_SHIFT   4      // lower => bassier (4..6)
#define S23_LP_SHIFT           5      // low-pass: y += (x-y)>>SHIFT (higher => darker, cheaper)
#define S23_GAIN_NUM           175    // post-gain multiplier numerator
#define S23_GAIN_SHIFT         6      // post-gain shift (gain ≈ 175/64 ≈ 2.734)

// 2-stage decay envelope knee
#define S23_STAGE1_TICKS       125    // first stage duration (ticks)
#define S23_STAGE1_END_LEVEL   3      // 0..15 (converted to Q8 end gain)

// ISR-owned state
static volatile bool     s23_active         = false;
static volatile uint16_t s23_tick           = 0;     // 0..S23_EXPLOSION_TICKS
static volatile uint16_t s23_env_gain_q8    = 0;     // 0..255

static volatile uint16_t s23_lfsr           = 0xACE1u;
static volatile int16_t  s23_white_hold     = 0;
static volatile uint16_t s23_noise_div_ctr  = 0;

static volatile int16_t  s23_brown_x        = 0;
static volatile int16_t  s23_lp_y           = 0;

// 120 Hz tick generator inside ISR: accumulator in "Hz units"
static volatile uint16_t s23_tick_acc       = 0;     // accumulates +120, wraps at 62500

static const uint8_t s23_gain_table_q8[S23_EXPLOSION_TICKS] PROGMEM = {
  255, 254, 252, 251, 249, 247, 246, 244, 242, 241, 239, 238, 236, 234, 233, 231, 229, 228, 226, 224, 223, 221, 220, 218, 216, 215, 213, 211, 210, 208, 207, 205, 203, 202, 200, 198, 197, 195, 193, 192, 190, 189, 187, 185, 184, 182, 180, 179, 177, 176, 174, 172, 171, 169, 167, 166, 164, 162, 161, 159, 158, 156, 154, 153, 151, 149, 148, 146, 145, 143, 141, 140, 138, 136, 135, 133, 131, 130, 128, 127, 125, 123, 122, 120, 118, 117, 115, 114, 112, 110, 109, 107, 105, 104, 102, 100, 99, 97, 96, 94, 92, 91, 89, 87, 86, 84, 83, 81, 79, 78, 76, 74, 73, 71, 69, 68, 66, 65, 63, 61, 60, 58, 56, 55, 53, 51, 50, 48, 46, 44, 43, 41, 39, 37, 36, 34, 32, 30, 29, 27, 25, 23, 22, 20, 18, 16, 15, 13, 11, 9, 8, 6, 4, 2
};

static inline void sound23_start_explosion() {
  noInterrupts();
  s23_active      = true;
  s23_tick        = 0;
  s23_env_gain_q8 = 255;

  s23_lfsr        = 0xACE1u;
  s23_white_hold  = 0;
  s23_noise_div_ctr = 0;

  s23_brown_x = 0;
  s23_lp_y    = 0;

  s23_tick_acc = 0;
  interrupts();
}

static inline void sound23_stop_explosion() {
  noInterrupts();

  s23_active        = false;
  s23_tick          = 0;
  s23_env_gain_q8   = 0;

  // Reset all ISR-visible explosion state so the next audio sample is silent.
  s23_tick_acc      = 0;
  s23_noise_div_ctr = 0;
  s23_white_hold    = 0;
  s23_lfsr          = 0xACE1u;

  s23_brown_x       = 0;
  s23_lp_y          = 0;

  interrupts();
}


// Galaga sound descriptions (index = sound number)
// Galaga sound descriptions (index = sound number) stored in PROGMEM
static const char s0[]  PROGMEM = "Ambience";
static const char s1[]  PROGMEM = "Hit Capture Ship";
static const char s2[]  PROGMEM = "Hit Red Ship";
static const char s3[]  PROGMEM = "Hit Blue Ship";
static const char s4[]  PROGMEM = "Hit Capture/Green Ship";
static const char s5[]  PROGMEM = "Tractor Beam";
static const char s6[]  PROGMEM = "Tractor Beam Capture";
static const char s7[]  PROGMEM = "Fighter Destroyed";
static const char s8[]  PROGMEM = "Credit";
static const char s9[]  PROGMEM = "Fighter Captured";
static const char s10[] PROGMEM = "Extra Life";
static const char s11[] PROGMEM = "Game Start";
static const char s12[] PROGMEM = "Name Entry (1rst place)";
static const char s13[] PROGMEM = "Challenging Stage Start";
static const char s14[] PROGMEM = "Challenging Stage Results";
static const char s15[] PROGMEM = "Shooting";
static const char s16[] PROGMEM = "Name Entry (2nd - 5th place)";
static const char s17[] PROGMEM = "Fighter Rescued";
static const char s18[] PROGMEM = "Free Life or Triple Formation";
static const char s19[] PROGMEM = "Enemy Flying";
static const char s20[] PROGMEM = "Challenging Stage Perfect";
static const char s21[] PROGMEM = "Stage Flag";
static const char s22[] PROGMEM = "Name Entry (1rst place) End";
static const char s23[] PROGMEM = "Explosion";

static const char* const soundNames[] PROGMEM = {
  s0,s1,s2,s3,s4,s5,s6,s7,s8,s9,s10,
  s11,s12,s13,s14,s15,s16,s17,s18,s19,s20,s21,s22,s23
};

// Fast mix->4bit scaling lookup (avoids division in ISR)
const uint8_t PROGMEM mix_to_out4[676] = {
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,
  1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
  1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,2,2,2,2,
  2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
  2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,3,3,3,3,3,3,3,
  3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,
  3,3,3,3,3,3,3,3,3,3,3,3,3,3,4,4,4,4,4,4,4,4,4,4,
  4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,
  4,4,4,4,4,4,4,4,4,4,4,5,5,5,5,5,5,5,5,5,5,5,5,5,
  5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,
  5,5,5,5,5,5,5,5,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,
  6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,
  6,6,6,6,6,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
  7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
  7,7,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,
  8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,9,
  9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,
  9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,10,10,10,10,
  10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,
  10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,11,11,11,11,11,11,11,
  11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,
  11,11,11,11,11,11,11,11,11,11,11,11,11,11,12,12,12,12,12,12,12,12,12,12,
  12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,
  12,12,12,12,12,12,12,12,12,12,12,13,13,13,13,13,13,13,13,13,13,13,13,13,
  13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,
  13,13,13,13,13,13,13,13,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,
  14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,
  14,14,14,14,14,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,
  15,15,15,15
};

// RAM lookup tables to make ISR fast
static uint8_t prom4_ram[256];      // high-nibble amplitude (0..15) for each PROM byte
static uint8_t mul4x4_ram[256];     // (samp4<<4)|vol4 -> samp4*vol4 (0..225)

// Signed contribution LUT: (samp4-8)*vol4 -> [-120..+105]
static int8_t sampVolLUT[256];

// Mix-to-DAC LUT for 6-bit output, indexed by (mix + 192) after clamping to [-192..+192]
static uint8_t mixToDac6[385];

// -------------------- Audio output selection --------------------
// Choose ONE output mode at compile time:
//
//   - PWM DAC on D11 (OC2A / Timer2) + RC low-pass filter
//   - 6-bit R-2R ladder on D2..D7 (PORTD bits 2..7)
//
// Set AUDIO_OUT_MODE below (or define it via build flags).
#define AUDIO_OUT_PWM_D11           1
#define AUDIO_OUT_R2R_6BIT_D2_D7    2

#ifndef AUDIO_OUT_MODE
  #define AUDIO_OUT_MODE AUDIO_OUT_PWM_D11
#endif

#if (AUDIO_OUT_MODE == AUDIO_OUT_PWM_D11)

// PWM audio on D11 (OC2A) using Timer2, Fast PWM @ ~62.5 kHz (16MHz/256, prescaler=1)
static inline void audioWrite6(uint8_t code6 /*0..63*/) {
  OCR2A = (uint8_t)(code6 << 2);   // 6-bit -> 8-bit duty
}

static void audioInit() {
  pinMode(11, OUTPUT);

  // Timer2: Fast PWM (WGM21:0=3), non-inverting on OC2A (COM2A1=1), prescaler=1 (CS20=1)
  TCCR2A = _BV(COM2A1) | _BV(WGM21) | _BV(WGM20);
  TCCR2B = _BV(CS20);
  OCR2A = 128; // midscale
}

#elif (AUDIO_OUT_MODE == AUDIO_OUT_R2R_6BIT_D2_D7)

// 6-bit resistor DAC on D2..D7:
//   D2 = bit0 (LSB) ... D7 = bit5 (MSB). Uses PORTD bits 2..7; D0/D1 kept for Serial.
static inline void audioWrite6(uint8_t code6 /*0..63*/) {
  code6 &= 0x3F;
  uint8_t p = PORTD & 0b00000011;   // keep PD0-1 (Serial)
  PORTD = p | (code6 << 2);         // set PD2-7
}

static void audioInit() {
  // D2..D7 output, keep D0/D1 for Serial
  DDRD |= 0b11111100;
  audioWrite6(32); // midscale
}

#else
  #error "Invalid AUDIO_OUT_MODE"
#endif


// -------------------- ROM data (extracted from /mnt/data/galaga.asm) --------------------

const uint8_t PROGMEM galaga_wavetable_prom[256] = {
  0x70,0x70,0xE0,0xB0,0x70,0x70,0x70,0xA0,0x90,0x90,0xE0,0xD0,0xA0,0xE0,0x80,0xC0,
  0xA0,0xA0,0xE0,0xE0,0xC0,0xC0,0xA0,0xC0,0xB0,0xB0,0xE0,0xD0,0xD0,0x90,0xC0,0xA0,
  0xC0,0x70,0xE0,0xC0,0xE0,0xC0,0xE0,0x70,0xD0,0xD0,0xE0,0xA0,0xD0,0xE0,0xD0,0x70,
  0xD0,0xD0,0xE0,0x80,0xC0,0xA0,0xC0,0x80,0xE0,0x70,0xE0,0x80,0xA0,0x70,0xC0,0xB0,
  0xE0,0xE0,0xE0,0x80,0x70,0xC0,0xB0,0xD0,0xE0,0x70,0xE0,0xA0,0x40,0xF0,0xA0,0xE0,
  0xD0,0xD0,0xE0,0xC0,0x20,0xD0,0x80,0xD0,0xD0,0xD0,0xE0,0xD0,0x10,0x80,0x70,0xA0,
  0xC0,0x70,0xE0,0xE0,0x00,0xA0,0x50,0x60,0xB0,0xB0,0xE0,0xD0,0x10,0xB0,0x60,0x50,
  0xA0,0xA0,0xE0,0xB0,0x20,0x70,0x70,0x50,0x90,0x90,0xE0,0x80,0x40,0x20,0x80,0x70,
  0x70,0x70,0x00,0x40,0x70,0x80,0x80,0x90,0x50,0x50,0x00,0x20,0xB0,0xD0,0x90,0x90,
  0x40,0x70,0x00,0x10,0xD0,0x90,0xA0,0x80,0x30,0x30,0x00,0x20,0xE0,0x40,0xB0,0x40,
  0x20,0x70,0x00,0x30,0xD0,0x50,0x90,0x10,0x10,0x10,0x00,0x50,0xB0,0x70,0x80,0x00,
  0x10,0x70,0x00,0x70,0x70,0x20,0x60,0x10,0x00,0x00,0x00,0x70,0x30,0x00,0x50,0x30,
  0x00,0x70,0x00,0x70,0x10,0x30,0x40,0x60,0x00,0x00,0x00,0x50,0x00,0x80,0x40,0x70,
  0x10,0x70,0x00,0x30,0x10,0x50,0x30,0x70,0x10,0x10,0x00,0x20,0x30,0x10,0x20,0x40,
  0x20,0x70,0x00,0x10,0x70,0x30,0x40,0x20,0x30,0x30,0x00,0x20,0xE0,0x60,0x60,0x20,
  0x40,0x70,0x00,0x40,0x70,0x30,0x80,0x40,0x50,0x50,0x00,0x70,0x00,0x10,0x90,0x70
};

const uint16_t PROGMEM table0_freq[13] = {
  0x8150,0x8900,0x9126,0x99C8,0xA2EC,0xAC9D,0xB6E0,0xC1C0,
  0xCD45,0xD97A,0xE669,0xF41C,0x0000
};

const uint8_t PROGMEM table3_soundMeta[69] = {
  0x00,0x01,0x00,0x01,0x01,0x01,0x02,0x01,0x01,0x03,0x01,0x01,
  0x04,0x01,0x01,0x05,0x01,0x00,0x06,0x01,0x02,0x20,0x03,0x00,
  0x0A,0x03,0x00,0x0D,0x03,0x00,0x07,0x03,0x00,0x13,0x03,0x00,
  0x16,0x03,0x00,0x19,0x03,0x00,0x1C,0x03,0x00,0x1F,0x01,0x02,
  0x2C,0x03,0x00,0x10,0x03,0x00,0x23,0x01,0x00,0x24,0x01,0x00,
  0x25,0x03,0x00,0x28,0x01,0x00,0x29,0x03,0x00
};

const uint16_t PROGMEM table4_waveOffset[47] = {
  0x0000,0x0057,0x0035,0x0001,0x00A1,0x00BB,0x00CF,0x0215,0x0225,0x0235,0x01DF,0x01F1,
  0x0203,0x0245,0x0289,0x02CD,0x02F9,0x033D,0x0381,0x00E3,0x012F,0x0179,0x049F,0x052D,
  0x057B,0x01A9,0x01BB,0x01CD,0x00E3,0x00E3,0x00E3,0x044B,0x03AD,0x03E3,0x0419,0x0437,
  0x044B,0x05AF,0x0623,0x069F,0x05A1,0x0523,0x0571,0x0597,0x06E1,0x071D,0x0759
};

const uint8_t PROGMEM table5_mask[23] = {
  0x04,0x02,0x02,0x02,0x02,0x04,0x04,0x0A,0x07,0x0C,0x0B,0x04,0x0A,0x0D,0x04,0x01,0x04,0x0C,0x02,0x06,0x05,0x02,0x03
};

const uint8_t PROGMEM wave_rom[1907] = {
  0xFF,0x00,0x00,0x06,0x71,0x01,0x72,0x01,0x73,0x01,0x75,0x01,0x74,0x01,0x73,0x01,
  0x72,0x01,0x71,0x01,0x70,0x01,0x8B,0x01,0x8A,0x01,0x0C,0x04,0x86,0x01,0x87,0x01,
  0x88,0x01,0x89,0x01,0x8A,0x01,0x89,0x01,0x88,0x01,0x87,0x01,0x86,0x01,0x85,0x01,
  0x84,0x01,0x83,0x01,0xFF,0x00,0x00,0x04,0x88,0x01,0x8A,0x01,0x70,0x01,0x71,0x01,
  0x73,0x01,0x75,0x01,0x77,0x01,0x78,0x01,0x0C,0x06,0x74,0x01,0x73,0x01,0x72,0x01,
  0x71,0x01,0x70,0x01,0x8B,0x01,0xFF,0x00,0x00,0x07,0x89,0x01,0x8A,0x01,0x8B,0x01,
  0x0C,0x01,0x70,0x01,0x71,0x01,0x72,0x01,0x0C,0x01,0x73,0x01,0x74,0x01,0x75,0x01,
  0x0C,0x03,0x8B,0x01,0x70,0x01,0x71,0x01,0x0C,0x01,0x72,0x01,0x73,0x01,0x74,0x01,
  0x0C,0x01,0x75,0x01,0x76,0x01,0x77,0x01,0x0C,0x03,0x71,0x01,0x72,0x01,0x73,0x01,
  0x0C,0x01,0x74,0x01,0x75,0x01,0x76,0x01,0x0C,0x01,0x77,0x01,0x78,0x01,0x79,0x01,
  0xFF,0x00,0x00,0x05,0x71,0x01,0x72,0x01,0x73,0x01,0x0C,0x01,0x74,0x01,0x75,0x01,
  0x76,0x01,0x0C,0x01,0x77,0x01,0x78,0x01,0x79,0x01,0xFF,0x00,0x00,0x04,0x61,0x01,
  0x7A,0x01,0x60,0x01,0x78,0x01,0x7A,0x01,0x76,0x01,0x78,0x01,0x75,0x01,0xFF,0x00,
  0x00,0x00,0x76,0x01,0x79,0x01,0x60,0x01,0x63,0x01,0x66,0x01,0x63,0x01,0x60,0x01,
  0x79,0x01,0xFF,0x00,0x00,0x07,0x81,0x08,0x81,0x01,0x86,0x03,0x88,0x09,0x8B,0x03,
  0x8A,0x09,0x86,0x03,0x88,0x09,0x73,0x03,0x71,0x09,0x86,0x03,0x88,0x09,0x8B,0x03,
  0x8A,0x09,0x86,0x03,0x71,0x09,0x75,0x03,0x76,0x09,0x74,0x03,0x72,0x09,0x71,0x03,
  0x8B,0x09,0x89,0x03,0x88,0x09,0x84,0x03,0x74,0x09,0x76,0x03,0x74,0x09,0x71,0x03,
  0x73,0x04,0x8B,0x04,0x88,0x04,0x71,0x04,0x8A,0x04,0x88,0x04,0x0C,0x10,0xFF,0x00,
  0x00,0x06,0x8A,0x09,0x81,0x03,0x88,0x09,0x83,0x03,0x86,0x09,0x81,0x03,0x83,0x09,
  0x85,0x03,0x8A,0x09,0x81,0x03,0x88,0x09,0x83,0x03,0x86,0x09,0x81,0x03,0x88,0x09,
  0x71,0x03,0x72,0x09,0x71,0x03,0x8B,0x09,0x89,0x03,0x88,0x09,0x86,0x03,0x84,0x09,
  0x88,0x03,0x89,0x09,0x8B,0x03,0x89,0x09,0x86,0x03,0x8B,0x04,0x88,0x04,0x83,0x04,
  0x88,0x04,0x85,0x04,0x83,0x04,0x0C,0x10,0xFF,0x00,0x00,0x07,0x81,0x0C,0x83,0x09,
  0x86,0x03,0x85,0x0C,0x81,0x0C,0x86,0x0C,0x88,0x09,0x8B,0x03,0x8A,0x0C,0x88,0x0C,
  0x89,0x0C,0x88,0x09,0x86,0x03,0x84,0x0C,0x89,0x0C,0x74,0x0C,0x71,0x09,0x89,0x03,
  0x88,0x0C,0x71,0x09,0x8A,0x03,0x0C,0x10,0xFF,0x02,0x00,0x03,0x78,0x02,0x0C,0x01,
  0x78,0x01,0x79,0x01,0x7B,0x01,0x61,0x03,0x0C,0x03,0xFF,0x02,0x00,0x03,0x73,0x02,
  0x0C,0x01,0x73,0x01,0x74,0x01,0x76,0x01,0x78,0x03,0x0C,0x02,0xFF,0x02,0x00,0x03,
  0x70,0x02,0x0C,0x01,0x70,0x01,0x71,0x01,0x73,0x01,0x75,0x03,0x0C,0x02,0xFF,0x01,
  0x00,0x04,0x78,0x01,0x7A,0x01,0x63,0x01,0x78,0x01,0x7A,0x01,0x63,0x01,0x65,0x03,
  0xFF,0x01,0x00,0x05,0x73,0x01,0x78,0x01,0x7A,0x01,0x73,0x01,0x78,0x01,0x7A,0x01,
  0x60,0x03,0xFF,0x01,0x00,0x07,0x8A,0x01,0x73,0x01,0x78,0x01,0x8A,0x01,0x73,0x01,
  0x78,0x01,0x7A,0x03,0xFF,0x01,0x06,0x04,0x7A,0x01,0x78,0x01,0x7A,0x01,0x61,0x01,
  0x65,0x01,0x68,0x03,0xFF,0x01,0x06,0x04,0x78,0x01,0x75,0x01,0x78,0x01,0x7A,0x01,
  0x61,0x01,0x65,0x03,0xFF,0x01,0x06,0x04,0x75,0x01,0x71,0x01,0x75,0x01,0x78,0x01,
  0x7A,0x01,0x60,0x03,0xFF,0x02,0x04,0x03,0x7A,0x01,0x76,0x01,0x78,0x01,0x75,0x01,
  0x76,0x01,0x73,0x01,0x75,0x01,0x72,0x01,0x73,0x01,0x8A,0x01,0x8B,0x01,0x88,0x01,
  0x86,0x01,0x85,0x01,0x83,0x01,0x82,0x01,0x83,0x01,0x86,0x01,0x85,0x01,0x88,0x01,
  0x86,0x01,0x8A,0x01,0x88,0x01,0x8B,0x01,0x8A,0x01,0x73,0x01,0x72,0x01,0x73,0x01,
  0x75,0x01,0x8A,0x01,0x70,0x01,0x72,0x01,0xFF,0x02,0x04,0x03,0x76,0x01,0x73,0x01,
  0x75,0x01,0x72,0x01,0x73,0x01,0x70,0x01,0x72,0x01,0x8A,0x01,0x8B,0x01,0x86,0x01,
  0x88,0x01,0x85,0x01,0x83,0x01,0x82,0x01,0x80,0x01,0x9A,0x01,0x9A,0x01,0x83,0x01,
  0x82,0x01,0x85,0x01,0x83,0x01,0x86,0x01,0x85,0x01,0x88,0x01,0x86,0x01,0x8A,0x01,
  0x88,0x01,0x8B,0x01,0x8A,0x01,0x88,0x01,0x86,0x01,0x85,0x01,0xFF,0x02,0x10,0x03,
  0x93,0x02,0x9A,0x02,0x83,0x03,0x9A,0x01,0x98,0x01,0x96,0x01,0x95,0x01,0x93,0x02,
  0x95,0x03,0x96,0x02,0x98,0x02,0x9A,0x02,0x9B,0x02,0x9A,0x02,0x98,0x01,0x96,0x01,
  0x95,0x01,0x92,0x01,0x93,0x01,0x95,0x01,0xFF,0x02,0x04,0x03,0x7A,0x01,0x77,0x01,
  0x78,0x01,0x75,0x01,0x77,0x01,0x73,0x01,0x75,0x01,0x72,0x01,0x73,0x01,0x8A,0x01,
  0x80,0x01,0x88,0x01,0x87,0x01,0x85,0x01,0x83,0x01,0x82,0x01,0x83,0x01,0x87,0x01,
  0x85,0x01,0x88,0x01,0x87,0x01,0x8A,0x01,0x88,0x01,0x80,0x01,0x8A,0x01,0x73,0x01,
  0x72,0x01,0x73,0x01,0x75,0x01,0x8A,0x01,0x70,0x01,0x72,0x01,0xFF,0x02,0x04,0x03,
  0x77,0x01,0x73,0x01,0x75,0x01,0x72,0x01,0x73,0x01,0x70,0x01,0x72,0x01,0x8A,0x01,
  0x80,0x01,0x87,0x01,0x88,0x01,0x85,0x01,0x83,0x01,0x82,0x01,0x80,0x01,0x9A,0x01,
  0x9A,0x01,0x83,0x01,0x82,0x01,0x85,0x01,0x83,0x01,0x87,0x01,0x85,0x01,0x88,0x01,
  0x87,0x01,0x8A,0x01,0x88,0x01,0x80,0x01,0x8A,0x01,0x88,0x01,0x87,0x01,0x85,0x01,
  0xFF,0x02,0x10,0x03,0x93,0x02,0x9A,0x02,0x83,0x03,0x9A,0x01,0x98,0x01,0x97,0x01,
  0x95,0x01,0x93,0x02,0x95,0x03,0x97,0x02,0x98,0x02,0x9A,0x02,0x90,0x02,0x9A,0x02,
  0x98,0x01,0x97,0x01,0x95,0x01,0x92,0x01,0x93,0x01,0x95,0x01,0xFF,0x02,0x04,0x03,
  0x7A,0x01,0x76,0x01,0x78,0x01,0x75,0x01,0x76,0x01,0x73,0x01,0x75,0x01,0x72,0x01,
  0x73,0x01,0x8A,0x01,0x8A,0x01,0x88,0x01,0x86,0x01,0x85,0x01,0x83,0x01,0x82,0x01,
  0x83,0x01,0x85,0x01,0x86,0x01,0x88,0x01,0x86,0x01,0x8A,0x01,0x70,0x01,0x72,0x01,
  0x73,0x04,0xFF,0x02,0x04,0x03,0x76,0x01,0x73,0x01,0x75,0x01,0x72,0x01,0x73,0x01,
  0x70,0x01,0x72,0x01,0x8A,0x01,0x8A,0x01,0x86,0x01,0x86,0x01,0x85,0x01,0x83,0x01,
  0x82,0x01,0x80,0x01,0x9A,0x01,0x9A,0x01,0x8B,0x01,0x80,0x01,0x82,0x01,0x83,0x01,
  0x85,0x01,0x86,0x01,0x88,0x01,0x8A,0x04,0xFF,0x02,0x10,0x03,0x73,0x02,0x75,0x02,
  0x76,0x02,0x75,0x02,0x73,0x02,0x72,0x02,0x70,0x02,0x72,0x02,0x73,0x02,0x8B,0x02,
  0x8A,0x02,0x86,0x02,0x83,0x04,0xFF,0x00,0x00,0x04,0x71,0x04,0x73,0x04,0x71,0x04,
  0x73,0x04,0x76,0x04,0x78,0x04,0x76,0x04,0x78,0x04,0xFF,0x00,0x00,0x06,0x56,0x01,
  0x55,0x01,0x54,0x01,0x53,0x01,0x52,0x01,0x51,0x01,0x50,0x01,0x6B,0x01,0x6A,0x01,
  0x69,0x01,0x68,0x01,0x67,0x01,0x66,0x01,0x65,0x01,0x64,0x01,0x63,0x01,0x62,0x01,
  0x61,0x01,0x60,0x01,0x7B,0x01,0x7A,0x01,0x79,0x01,0x78,0x01,0x77,0x01,0x76,0x01,
  0x75,0x01,0x74,0x01,0x73,0x01,0x72,0x01,0x71,0x01,0x70,0x01,0x8B,0x01,0x8A,0x01,
  0x89,0x01,0x88,0x01,0x87,0x01,0x86,0x01,0x85,0x01,0x84,0x01,0x83,0x01,0xFF,0x02,
  0x04,0x05,0x60,0x01,0x78,0x01,0x75,0x01,0x71,0x01,0x60,0x01,0x78,0x01,0x75,0x01,
  0x71,0x01,0x60,0x01,0x78,0x01,0x75,0x01,0x71,0x01,0x60,0x01,0x78,0x01,0x75,0x01,
  0x71,0x01,0x60,0x01,0x78,0x01,0x75,0x01,0x71,0x01,0x60,0x01,0x78,0x01,0x75,0x01,
  0x71,0x01,0x60,0x01,0x0C,0x01,0x78,0x01,0x7A,0x01,0x75,0x01,0x78,0x01,0x73,0x01,
  0x75,0x01,0x61,0x01,0x7A,0x01,0x76,0x01,0x73,0x01,0x61,0x01,0x7A,0x01,0x76,0x01,
  0x73,0x01,0x61,0x01,0x7A,0x01,0x76,0x01,0x73,0x01,0x61,0x01,0x7A,0x01,0x76,0x01,
  0x73,0x01,0x61,0x01,0x79,0x01,0x76,0x01,0x73,0x01,0x61,0x01,0x79,0x01,0x76,0x01,
  0x73,0x01,0x61,0x01,0x0C,0x01,0x79,0x01,0x61,0x01,0x78,0x01,0x79,0x01,0x75,0x01,
  0x78,0x01,0xFF,0x02,0x02,0x05,0x60,0x01,0x60,0x01,0x60,0x01,0xFF,0x02,0x04,0x05,
  0x61,0x02,0x78,0x02,0x78,0x02,0x61,0x02,0x78,0x02,0x78,0x02,0x61,0x02,0x78,0x02,
  0x78,0x02,0x61,0x02,0x78,0x02,0x78,0x02,0x61,0x02,0x78,0x02,0x7A,0x02,0x75,0x02,
  0x63,0x02,0x7A,0x02,0x7A,0x02,0x63,0x02,0x7A,0x02,0x7A,0x02,0x63,0x02,0x7A,0x02,
  0x79,0x02,0x63,0x02,0x79,0x02,0x79,0x02,0x63,0x02,0x79,0x02,0x76,0x02,0x73,0x02,
  0xFF,0x02,0x02,0x05,0x78,0x01,0x78,0x01,0x78,0x01,0xFF,0x02,0x10,0x05,0x85,0x06,
  0x85,0x06,0x85,0x06,0x85,0x06,0x85,0x04,0x85,0x04,0x86,0x06,0x86,0x06,0x86,0x06,
  0x86,0x06,0x86,0x04,0x86,0x04,0xFF,0x02,0x04,0x05,0x81,0x01,0x81,0x01,0x81,0x01,
  0xFF,0x02,0x00,0x07,0x65,0x01,0x0C,0x01,0x61,0x01,0x0C,0x01,0x63,0x01,0xFF,0x02,
  0x00,0x05,0x7A,0x05,0x0C,0x01,0x7A,0x01,0x0C,0x01,0x7A,0x03,0x0C,0x01,0x78,0x07,
  0x0C,0x01,0x78,0x07,0x0C,0x01,0x78,0x03,0x0C,0x01,0x7B,0x05,0x0C,0x01,0x7B,0x01,
  0x0C,0x01,0x7B,0x03,0x0C,0x01,0x7A,0x07,0x0C,0x01,0x7A,0x07,0x0C,0x01,0x7A,0x03,
  0x0C,0x01,0x7B,0x01,0x0C,0x01,0x7B,0x01,0x0C,0x03,0x7B,0x01,0x0C,0x01,0x7B,0x03,
  0x0C,0x01,0x61,0x01,0x0C,0x01,0x61,0x01,0x0C,0x03,0x61,0x01,0x0C,0x01,0x61,0x03,
  0x0C,0x01,0x61,0x03,0x0C,0x01,0x61,0x03,0x0C,0x01,0x63,0x01,0x0C,0x01,0x63,0x01,
  0x0C,0x03,0x63,0x01,0x0C,0x01,0x63,0x03,0x0C,0x01,0x63,0x03,0x0C,0x01,0x63,0x03,
  0x0C,0x01,0xFF,0x02,0x00,0x03,0x86,0x02,0x8A,0x02,0x71,0x02,0x76,0x02,0x86,0x02,
  0x8A,0x02,0x71,0x02,0x76,0x02,0x86,0x02,0x8A,0x02,0x71,0x02,0x76,0x02,0x86,0x02,
  0x8A,0x02,0x71,0x02,0x76,0x02,0x86,0x02,0x8A,0x02,0x71,0x02,0x76,0x02,0x86,0x02,
  0x8A,0x02,0x71,0x02,0x76,0x02,0x86,0x02,0x8A,0x02,0x71,0x02,0x76,0x02,0x86,0x02,
  0x8A,0x02,0x71,0x02,0x76,0x02,0x77,0x01,0x0C,0x01,0x77,0x01,0x0C,0x03,0x77,0x01,
  0x0C,0x01,0x77,0x03,0x0C,0x01,0x69,0x01,0x0C,0x01,0x69,0x01,0x0C,0x03,0x69,0x01,
  0x0C,0x01,0x69,0x03,0x0C,0x01,0x69,0x03,0x0C,0x01,0x69,0x03,0x0C,0x01,0x8B,0x02,
  0x73,0x02,0x76,0x02,0x7B,0x02,0x7B,0x02,0x76,0x02,0x73,0x02,0x8B,0x02,0xFF,0x00,
  0x00,0x02,0x86,0x08,0x81,0x08,0x86,0x08,0x81,0x08,0x86,0x08,0x81,0x08,0x86,0x08,
  0x81,0x08,0x82,0x01,0x0C,0x01,0x82,0x01,0x0C,0x03,0x82,0x01,0x0C,0x01,0x82,0x03,
  0x0C,0x01,0x84,0x01,0x0C,0x01,0x84,0x01,0x0C,0x03,0x84,0x01,0x0C,0x01,0x84,0x03,
  0x0C,0x01,0x84,0x03,0x0C,0x01,0x84,0x03,0x0C,0x01,0x7B,0x08,0x76,0x04,0x8B,0x04,
  0xFF,0x00,0x0C,0x05,0x75,0x0C,0x71,0x0C,0x8A,0x0C,0x86,0x0C,0x0C,0x09,0x75,0x03,
  0x71,0x09,0x8A,0x03,0x86,0x04,0x8A,0x04,0x71,0x04,0x89,0x04,0x70,0x04,0x73,0x04,
  0x8B,0x0C,0x73,0x0C,0x76,0x0C,0x78,0x0C,0x0C,0x09,0x79,0x03,0x76,0x09,0x72,0x03,
  0x8B,0x04,0x89,0x04,0x86,0x04,0x72,0x04,0x89,0x04,0x76,0x04,0xFF,0x00,0x0C,0x05,
  0x71,0x0C,0x8A,0x0C,0x86,0x0C,0x85,0x0C,0x0C,0x09,0x81,0x03,0x8A,0x09,0x86,0x03,
  0x85,0x04,0x86,0x04,0x8A,0x04,0x86,0x04,0x89,0x04,0x8B,0x04,0x88,0x0C,0x8B,0x0C,
  0x73,0x0C,0x76,0x0C,0x0C,0x09,0x76,0x03,0x72,0x09,0x8B,0x03,0x8A,0x04,0x86,0x04,
  0x82,0x04,0x8B,0x04,0x89,0x04,0x82,0x04,0xFF,0x00,0x00,0x03,0x75,0x18,0x75,0x18,
  0x75,0x18,0x71,0x0C,0x75,0x0C,0x73,0x18,0x73,0x18,0x72,0x18,0x76,0x0C,0x78,0x0C,
  0xFF,0xFA,0xFF
};

// -------------------- Engine state --------------------
// 120 Hz scheduler timing accumulator
static uint32_t last60 = 0;

static uint8_t states_[47];
static uint8_t positions_[47];
static const uint8_t MAX_OFFSETS = 47;
static uint8_t do_sound[24] = {0};

// This stops all sounds if the selected sound is complex (i.e. special or multivoice)
static const uint8_t sound_voice_mask[24] PROGMEM = {
  /* 00 */ 0,
  /* 01 */ 0,
  /* 02 */ 0,
  /* 03 */ 0,
  /* 04 */ 0,
  /* 05 */ 1,
  /* 06 */ 1,
  /* 07 */ 1,
  /* 08 */ 1,
  /* 09 */ 1,
  /* 10 */ 1,
  /* 11 */ 1,
  /* 12 */ 1,
  /* 13 */ 1,
  /* 14 */ 1,
  /* 15 */ 0,
  /* 16 */ 1,
  /* 17 */ 1,
  /* 18 */ 0,
  /* 19 */ 0,
  /* 20 */ 1,
  /* 21 */ 1,
  /* 22 */ 1,
  /* 23 */ 1
};

static inline uint8_t get_sound_voice_mask(uint8_t sid) {
#if defined(ARDUINO_ARCH_AVR)
  return pgm_read_byte_near(&sound_voice_mask[sid]);
#else
  return sound_voice_mask[sid];
#endif
}


// Tracks the last triggered sound for voice-overlap decisions (-1 = none)
static int8_t currentSound = -1;
static int8_t lastSoundCmd = -1;
static bool suppressRepeatOnce = false;
static uint8_t in_sound[24] = {0};

static uint8_t prev_in_sound[24] = {0}; // previous in_sound[] snapshot for end-transition cleanup
// Special looping counters (sound05/sound06 behavior)
static uint8_t sound05_count   = 0;
static uint8_t sound05_vol     = 0x0C;
static uint8_t sound06_count   = 0;
static uint8_t sound06_wavesel = 0;

// sound10 (0x10) tail fade helper: voice1 can otherwise stay pegged at 0x0A
static bool    s10_fading   = false;
static uint8_t s10_fadeVol1 = 0;

// Per-voice params used by audio ISR (3 voices)
static volatile uint16_t acc16[3] = {0,0,0};   // 16-bit phase accumulator (hi byte = wavetable phase)
static volatile uint16_t inc16[3] = {0,0,0};   // 16-bit increment (same scaling as PIC low+mid bytes)

static volatile uint8_t  volRaw[3] = {0,0,0};   // raw 8-bit vol; audio uses low nibble
static volatile uint8_t  waveSel[3]= {0,0,0};   // 0..7

// -------------------- Debug logging (sound00) --------------------
// Prints a MAME-like trace line so you can compare Arduino behavior vs MAME.
// Serial commands:
//   d0   -> toggle sound00 per-tick logging (120 lines/sec)
//   d0s  -> toggle sound00 segment-only logging (one line per 34 ticks)
static bool gSound00LogTicks    = false;
static bool gSound00LogSegments = false;

// Sound00 frequency mapping into the Arduino phase increment domain.
// The Z80 writes a 16-bit WSG word 0xSWAP(H)H (e.g. 0xC55C). Our oscillator uses inc16 as a 16-bit phase increment
// at the audio ISR rate, so we right-shift this word to land in the same range as other sounds.
// Default shift=8 produces ~100–300 Hz for sound00 on the current ISR/sample-index mapping.
static const uint8_t gSound00FreqShift = 7;

static void sound00_log(bool isSegEvent,
                        uint8_t byte9211,
                        uint8_t tbl,
                        uint8_t seg,
                        uint8_t t34,
                        uint16_t base,
                        int16_t stepRaw,
                        int16_t stepScaled,
                        uint16_t freqScaled,
                        uint16_t phiInc,
                        uint8_t incByte,
                        uint8_t ws,
                        uint8_t vol) {
  // Keep it lightweight: build one line and print once.
  // Format is intentionally close to the MAME line you shared.
  char buf[180];
  // Example (MAME-ish):
  // CH1: 9211=0x01 tbl=1 st=00 t34=00 base=0x5C00 dRaw=0x0130 dSc=+0x0198 freq=0x5C00 inc=0x5C ws=0 vol=A
  snprintf(buf, sizeof(buf),
           "CH1:%c 9211=0x%02X tbl=%u st=%02X t34=%02u base=0x%04X dRaw=0x%04X dSc=%+d freq=0x%04X phi=0x%04X inc=0x%02X ws=%u vol=%X",
           isSegEvent ? 'S' : ' ',
           byte9211,
           (unsigned)tbl,
           (unsigned)seg,
           (unsigned)t34,
           (unsigned)base,
           (unsigned)((uint16_t)stepRaw),
           (int)stepScaled,
           (unsigned)freqScaled,
           (unsigned)phiInc,
           (unsigned)incByte,
           (unsigned)ws,
           (unsigned)(vol & 0x0F));
  Serial.println(buf);
}


// -------------------- Sound 00 (special sweep) tables --------------------
// These come from the Galaga Z80 sound CPU disassembly:
//
// TABLE1 @ 0x06C3: positive per-tick step sizes (ascending mode, byte_9211 = 0x01)
// TABLE2 @ 0x06D3: first 8 words are negative per-tick step sizes (descending mode, byte_9211 = 0xFF)
// Then TABLE2 continues with 8 base seeds for ascending, then 8 base seeds for descending.
//
// Sound00 uses wavetable 0 and volume 0x0A (matches MAME trace ws=0, vol=A).
static const uint16_t sound00_step_up[8] PROGMEM = {
  0x0130, 0x0168, 0x0136, 0x01A8, 0x0168, 0x0200, 0x01AC, 0x0208
};
static const uint16_t sound00_step_dn[8] PROGMEM = {
  0xFE00, 0xFE58, 0xFE08, 0xFE98, 0xFE58, 0xFED0, 0xFE98, 0xFED6
};
static const uint16_t sound00_base_up[8] PROGMEM = {
  0x5B00, 0x6C00, 0x5B00, 0x7E00, 0x6C00, 0x9700, 0x8100, 0x9900
};
static const uint16_t sound00_base_dn[8] PROGMEM = {
  0xD900, 0xB600, 0xD900, 0x9700, 0xB600, 0x7E00, 0x9900, 0x8100
};

// Sound00 state (updated at the same 120 Hz tick rate as the rest of the engine)
static uint8_t  sound00_dir     = 0x01;   // 0x01=ascending, 0xFF=descending (mirrors byte_9211)
static uint8_t  sound00_seg     = 0;      // 0..7 (mirrors 9A81)
static uint8_t  sound00_tick    = 0;      // 0..33 (mirrors 9A00)
static uint16_t sound00_acc     = 0;      // 16-bit accumulator (mirrors 9A86)
static uint16_t sound00_step    = 0;      // 16-bit step (mirrors 9A84)
static bool     sound00_active  = false;

static inline uint16_t sound00_pgm_u16(const uint16_t* p, uint8_t idx) {
  return (uint16_t)pgm_read_word_near(p + idx);
}

// Load a new segment's BASE+STEP (matches ACC0 = BASE + STEP behavior seen in trace)
static void sound00_load_segment() {
  const uint8_t i = (sound00_seg & 0x07);
  if (sound00_dir == 0x01) {
    sound00_step = sound00_pgm_u16(sound00_step_up, i);
    const uint16_t base = sound00_pgm_u16(sound00_base_up, i);
    sound00_acc  = (uint16_t)(base + sound00_step);
  } else {
    sound00_step = sound00_pgm_u16(sound00_step_dn, i);
    const uint16_t base = sound00_pgm_u16(sound00_base_dn, i);
    sound00_acc  = (uint16_t)(base + sound00_step);
  }
}

// One 120 Hz tick of sound00
static void sound00_tick_update() {
  if (!sound00_active) return;

  // Advance within the current segment:
  // tick==0 is already the freshly loaded ACC0; subsequent ticks add STEP.
  if (sound00_tick != 0) {
    sound00_acc = (uint16_t)(sound00_acc + sound00_step);
  }

  // Update voice 0 frequency from high byte of accumulator (H = acc >> 8)
  const uint8_t H = (uint8_t)(sound00_acc >> 8);
  // Galaga sound00 sets two bytes derived from H:
//   b61 = H
//   b62 = swap_nibbles(H)
// which forms a 16-bit word 0xSWAP(H)H (little-endian 0xC55C when H=0x5C).
// Using that as the phase increment matches the original mapping much better than 0xHH00.
const uint8_t Hs = (uint8_t)((H << 4) | (H >> 4));   // swap nibbles
const uint16_t wsgFreq = (uint16_t)(((uint16_t)Hs << 8) | (uint16_t)H);

// For MAME trace comparison we still compute the packed word above (b62:b61 = swap(H):H).
// But for *audio generation* we must feed the oscillator with a smoothly increasing phase increment.
// The Z80/MAME trace treats the effective increment as the high-byte-derived base (H<<8), with inc=H.
// If we shift the packed word directly (0xC55C >> 8 = 0xC5, but 0x0660 >> 8 = 0x06), it "wraps"
// at nibble boundaries and creates the high-pitched chirps you heard.
// So: drive the phase increment from base = (H<<8), scaled down by a shift (default 8 => phiInc ~= H).
const uint16_t base16 = (uint16_t)H << 8;

// Sound00 uses wavetable 0, volume A, single voice (voice0)
waveSel[0] = 0;         // ws=0
volRaw[0]  = 0x0A;      // vol=A
const uint16_t phiInc = (uint16_t)(base16 >> gSound00FreqShift);
inc16[0]   = phiInc;

  // Optional MAME-like debug logging
  if (gSound00LogTicks || gSound00LogSegments) {
    const uint8_t tbl = (sound00_dir == 0x01) ? 1 : 2;
    const uint16_t base = (uint16_t)H << 8;
    const int16_t stepRaw = (int16_t)sound00_step;
    const int16_t stepScaled = stepRaw; // unscaled for sound00
    const bool isSegEvent = (sound00_tick == 0);
    if ((gSound00LogTicks && !isSegEvent) || (gSound00LogTicks && isSegEvent) || (gSound00LogSegments && isSegEvent)) {
      sound00_log(isSegEvent,
                  sound00_dir,
                  tbl,
                  sound00_seg,
                  sound00_tick,
                  base,
                  stepRaw,
                  stepScaled,
                  wsgFreq,
                  phiInc,
                  H,
                  0,
                  0x0A);
    }
  }

  // Ensure other voices are silent
  volRaw[1] = 0; inc16[1] = 0;
  volRaw[2] = 0; inc16[2] = 0;

  // Tick/segment counters: 34 ticks per segment (0..33)
  sound00_tick++;
  if (sound00_tick >= 34) {
    sound00_tick = 0;

    // Advance segment 0..7. When we wrap back to 0, the *main CPU* would flip byte_9211
    // (01 <-> FF), which switches between TABLE1+TABLE2 bases (up) and TABLE2 negative steps+bases (down).
    // Your MAME observation: the flip is slow (~every couple of seconds), which corresponds well to
    // running the full 8-segment pattern before switching direction.
    sound00_seg++;
    if (sound00_seg >= 8) {
      sound00_seg = 0;
      sound00_dir = (sound00_dir == 0x01) ? 0xFF : 0x01;
    }

    sound00_load_segment();
  }
}

// Start/stop sound00 (called from scheduler)
static void sound00_start() {
  sound00_active  = true;
  sound00_dir     = 0x01;
  sound00_seg     = 0;
  sound00_tick    = 0;  sound00_load_segment();

  // Do NOT reset acc16[0] here; keep phase continuity similar to hardware.
  waveSel[0] = 0;
}
static void sound00_stop() {
  sound00_active = false;
  // Hard mute voice0 so "stop" is immediate even if scheduler paths differ.
  volRaw[0] = 0;
  inc16[0]  = 0;
  // Leave acc16 as-is; it will be reset on next start.
}



// "playing" gates the audio ISR (we set it true whenever any sound is active)
static volatile bool playing = false;

// Per-sound decoded parameters (set by get_data_for_sound)
static uint8_t baseOffset   = 0;  // index into table4/states/positions
static uint8_t numVoices    = 0;  // 1..3
static uint8_t voiceStart   = 0;  // 0..2

static inline uint8_t pgm_u8(const uint8_t* p, uint16_t i) {
  return pgm_read_byte_near(p + i);
}
static inline uint16_t pgm_u16(const uint16_t* p, uint16_t i) {
  return pgm_read_word_near(p + i);
}

// -------------------- Galaga data fetch (table3) --------------------
static void get_data_for_sound(uint8_t sound) {
  uint16_t idx = (uint16_t)sound * 3;
  baseOffset = pgm_read_byte_near(table3_soundMeta + idx + 0);
  numVoices  = pgm_read_byte_near(table3_soundMeta + idx + 1);
  voiceStart = pgm_read_byte_near(table3_soundMeta + idx + 2);
}

// -------------------- process_chunk (matches galaga.asm process_chunk) --------------------
static bool process_chunk(uint8_t offset, uint8_t voice, uint8_t sound) {
  
  states_[offset]++;

  // table4 is a table of 16-bit pointers to WAVExx in ROM; here we pre-converted them
  // into offsets into wave_rom[].
  uint16_t waveBase = pgm_u16(table4_waveOffset, offset);

  // Read data0, data1, wavesel
  uint8_t data0 = pgm_u8(wave_rom, waveBase + 0);
  uint8_t data1 = pgm_u8(wave_rom, waveBase + 1);
  uint8_t ws    = pgm_u8(wave_rom, waveBase + 2);

  // Script bytes start after those 3 bytes
  uint16_t scriptBase = waveBase + 3;
  uint8_t pos = positions_[offset];

  uint8_t data_byte = pgm_u8(wave_rom, scriptBase + pos);
  uint8_t data_next = pgm_u8(wave_rom, scriptBase + pos + 1);

  // If 0xFF => vol[voice]=0, stop oscillator, chunk_done=1
  if (data_byte == 0xFF) {
    volRaw[voice] = 0;
    inc16[voice]  = 0;
    acc16[voice]  = 0;
    return true;
  }

  // -------- frequency --------
  uint8_t idx0 = data_byte & 0x0F;             // (data & 0xF)
  uint16_t bfreq = pgm_u16(table0_freq, idx0); // base frequency

  uint8_t shift = (data_byte >> 4) & 0x0F;     // (data >> 4)
  while (shift--) bfreq >>= 1;

  // Store increment (PIC adds 16-bit freq into accumulator low+mid bytes), then apply global pitch scale.
  // Keep as 16-bit for ISR speed.
  uint32_t inc = (uint32_t)bfreq;
  inc = (inc * (uint32_t)PITCH_NUM) / (uint32_t)PITCH_DEN;
  if (inc > 0xFFFFu) inc = 0xFFFFu;
  inc16[voice] = (uint16_t)inc;

  // -------- volume (faithful branching, store raw byte) --------
  uint8_t v;
  if (data_byte == 0x0C) {
    v = 0;
  } else {
    // PIC uses: w = data_byte - 0x0C, then tests data0&w and data1&w
    uint8_t w = (uint8_t)(data_byte - 0x0C);

    uint8_t m0 = (uint8_t)(data0 & w);
    if (m0 != 0) {
      if (m0 == 1) {
        // if states[offset] < 6 then v = 2*states else fall through
        uint8_t s = states_[offset];
        if (s < 6) v = (uint8_t)(s + s);
        else goto data0_zero;
      } else {
        // if states[offset] < 6 then v = ~states else fall through
        uint8_t s = states_[offset];
        if (s < 6) v = (uint8_t)(~s);
        else goto data0_zero;
      }
    } else {
data0_zero:
      uint8_t m1 = (uint8_t)(data1 & w);
      if (m1 != 0) {
        uint8_t s = states_[offset];
        if (s <= 0x0A) v = 0x0A;
        else if (s >= 0x15) v = 0;
        else v = (uint8_t)(0x15 - s);
      } else {
        v = 0x0A;
      }
    }
  }

  // If sound10 has entered its tail phase (voices 0 & 2 already silent),
  // let voice1 fade down instead of being reasserted by the script.
  if (sound == 10 && voice == 1 && s10_fading) {
    if (s10_fadeVol1 > 0) s10_fadeVol1--;
    v = (s10_fadeVol1 & 0x0F);
  }

  // Optional: trim sound 10 loudness (applies after special fade logic above)
#if SOUND10_VOL_TRIM > 0
  if (sound == 10) {
    if (v > SOUND10_VOL_TRIM) v = (uint8_t)(v - SOUND10_VOL_TRIM);
    else v = 0;
  }
#endif

  volRaw[voice]  = v;
  waveSel[voice] = (ws & 0x07);

  // PIC-style fix_voices: if volume is 0, make the voice truly silent by stopping oscillator
  if ((v & 0x0F) == 0) {
    inc16[voice] = 0;
    acc16[voice] = 0;
  }

  // -------- adjust tables (PIC-style 8-bit acc loop) --------
  uint8_t mask = pgm_u8(table5_mask, sound);
  uint8_t data = data_next;   // 8-bit, shifted left each step (mod 256)
  uint8_t acc  = 0;

  for (uint8_t i = 0; i < 8; i++) {
    if (mask & 0x01) acc = (uint8_t)(acc + data);  // 8-bit wrap like PIC addwf
    data <<= 1;
    mask >>= 1;
  }

  // Compare low byte only (PIC uses cpfseq on acc16 low byte)
  if (states_[offset] == acc) {
    states_[offset] = 0;
    positions_[offset] = (uint8_t)(positions_[offset] + 2);
  }

  return false;
}

// -------------------- Galaga sound engine helpers (ported from galaga.asm) --------------------

// Like PIC "init_sound": clears states/positions for [offset .. offset+numVoices-1]
static void init_sound(uint8_t offset, uint8_t nVoices) {
  for (uint8_t i = 0; i < nVoices; i++) {
    uint8_t o = offset + i;
    if (o < 47) { states_[o] = 0; positions_[o] = 0; }
  }
}

// get_data + special case for sound 0x0E (echo effect)
static void get_data_for_sound_runtime(uint8_t sound) {
  get_data_for_sound(sound);

  // Sound 0x0E (decimal 14) uses a progressive echo:
  // it starts with 1 voice, then enables additional voices as the script advances.
  // This must be evaluated every continue_sound() tick (based on positions_[]),
  // but positions_[0x1C]/[0x1D] must be reset when the sound is started to keep it deterministic.
  if (sound == 0x0E) {
    if (positions_[0x1C] == 0) {
      numVoices = 1;
    } else if (positions_[0x1C] == 1) {
      numVoices = 2;
    } else {
      if (positions_[0x1D] != 0) {
        numVoices = 2;
      } else {
        numVoices = 3;
      }
    }
  }
}

// Like PIC "continue_sound": continue sound chunks for all voices, clear in_sound when done.
static void continue_sound(uint8_t sound) {
  get_data_for_sound_runtime(sound);

  bool chunk_done = false;
  uint8_t off = baseOffset;
  uint8_t v   = voiceStart;

  for (uint8_t i = 0; i < numVoices; i++) {
    if (off < MAX_OFFSETS) {
      if (v < 3) chunk_done |= process_chunk(off, v, sound);
    } else {
      // Fail-safe: bad offset would corrupt memory; end this sound.
      chunk_done = true;
    }
    off++;
    v++;
  }

  // Sound 0x0E (14) enables voices progressively to create an echo.
  // When it's using fewer than 3 voices, any *inactive* voice must be silenced,
  // otherwise it can free-run at a stale increment and sound like a buzz.
  if (sound == 0x0E) {
    bool used[3] = {false, false, false};
    uint8_t off2 = baseOffset;
    uint8_t v2   = voiceStart;
    for (uint8_t i2 = 0; i2 < numVoices; i2++) {
      if (v2 < 3) used[v2] = true;
      off2++;
      v2++;
    }
    for (uint8_t vv = 0; vv < 3; vv++) {
      if (!used[vv]) {
        volRaw[vv] = 0;
        inc16[vv]  = 0;
      }
    }
  }
if (chunk_done) {
    in_sound[sound] = 0;
    // Stop all voices immediately when this sound ends.
    volRaw[0] = volRaw[1] = volRaw[2] = 0;
    inc16[0]  = inc16[1]  = inc16[2]  = 0;
  }
}

// Like PIC "start_sound": mark in_sound=1, init arrays, then immediately continue
static void start_sound(uint8_t sound) {
  volRaw[0]=volRaw[1]=volRaw[2]=0;
  acc16[0]=acc16[1]=acc16[2]=0;
  inc16[0]=inc16[1]=inc16[2]=0;
  if (sound == 0x0E) {
    positions_[0x1C] = positions_[0x1D] = 0;
    states_[0x1C]    = states_[0x1D]    = 0;
  }

  in_sound[sound] = 1;
  get_data_for_sound_runtime(sound);
  init_sound(baseOffset, numVoices);

  for (uint8_t vv = 0; vv < 3; vv++) { if (vv < voiceStart || vv >= (uint8_t)(voiceStart + numVoices)) volRaw[vv] = 0; }

  continue_sound(sound);
}

// Like PIC "play_sound_bis": if not already in_sound, init; then continue
static void play_sound_bis(uint8_t sound) {
  if (in_sound[sound] == 0) {
    // Fresh start: clear voice state so replays are deterministic.
    volRaw[0]=volRaw[1]=volRaw[2]=0;
    acc16[0]=acc16[1]=acc16[2]=0;
    inc16[0]=inc16[1]=inc16[2]=0;

    // Sound 0x0E (14) uses positions_[0x1C]/[0x1D] to build an echo across voices.
    // Reset those when starting so the echo always begins the same way.
    if (sound == 0x0E) {
      positions_[0x1C] = positions_[0x1D] = 0;
      states_[0x1C]    = states_[0x1D]    = 0;
    }

    in_sound[sound] = 1;
    get_data_for_sound_runtime(sound);
    init_sound(baseOffset, numVoices);

    // Silence any unused voices (and their increment) on init.
    for (uint8_t vv = 0; vv < 3; vv++) {
      if (vv < voiceStart || vv >= (uint8_t)(voiceStart + numVoices)) {
        volRaw[vv] = 0;
        inc16[vv]  = 0;
      }
    }
  }

  continue_sound(sound);
}

// Like PIC "play_sound": one-shot sounds that clear/do follow-ups at chunk end
static void play_sound(uint8_t sound) {
  // check_in_sound + optional init
  if (in_sound[sound] == 0) {
    in_sound[sound] = 1;
    get_data_for_sound_runtime(sound);
    init_sound(baseOffset, numVoices);
  }

  // process voices for this sound
  get_data_for_sound_runtime(sound);

  bool chunk_done = false;
  uint8_t off = baseOffset;
  uint8_t v   = voiceStart;
  for (uint8_t i = 0; i < numVoices; i++) {
    if (off < MAX_OFFSETS) {
      if (v < 3) chunk_done |= process_chunk(off, v, sound);
    } else {
      // Fail-safe: bad offset would corrupt memory; end this sound.
      chunk_done = true;
    }
    off++;
    v++;
  }

// Sound 0x0E (14) enables voices progressively to create an echo.
// When it's using fewer than 3 voices, any inactive voice must be silenced,
// otherwise it can free-run at a stale increment and sound like a buzz.
  if (sound == 0x0E) {
    bool used[3] = {false, false, false};
    uint8_t vv = voiceStart;
    for (uint8_t ii = 0; ii < numVoices; ii++) {
      if (vv < 3) used[vv] = true;
      vv++;
    }
    for (uint8_t i3 = 0; i3 < 3; i3++) {
      if (!used[i3]) { volRaw[i3] = 0; inc16[i3] = 0; }
    }
  }

  if (!chunk_done) return;

  // chunk done: clear in_sound
  in_sound[sound] = 0;

  // Now apply play_sound tail behavior from ASM (do_sound bookkeeping / chaining)
  if (sound == 0x08) {
    // sound 8: decrement do_sound[8] (can play multiple times)
    if (do_sound[sound] > 0) do_sound[sound]--;
    return;
  }

  if (sound == 0x16) {
    // sound16 (decimal 22): play multiple short hits when do_sound[0x16] is a count.
    if (do_sound[sound] > 0) do_sound[sound]--;
    return;
  }

  if (sound == 0x0C) {
    // sound C: played twice and followed by 0x16 (see galaga.asm play_sound)
    if (do_sound[sound] > 0) do_sound[sound]--;
    if (do_sound[sound] == 0) { do_sound[0x16] = 3; return; }
    if ((do_sound[sound] & 0x01) != 0) return;
    do_sound[0x16] = 3;
    return;
  }

  if (sound == 0x14) {
    // sound 0x14: played once and followed by 0x13
    do_sound[sound] = 0;
    do_sound[0x13] = 1;
    return;
  }

  // default: clear do_sound
  do_sound[sound] = 0;
}

// -------------------- Galaga refresh (priority scheduler) --------------------

static inline void mute_sound_voices(uint8_t soundId) {
  // table3_soundMeta: triplets {offset, numVoices, voiceStart}
  uint8_t idx = soundId * 3;
  if (idx + 2 >= sizeof(table3_soundMeta)) return;
  uint8_t numVoices = pgm_read_byte(&table3_soundMeta[idx + 1]);
  uint8_t voiceStart = pgm_read_byte(&table3_soundMeta[idx + 2]);
  for (uint8_t v = voiceStart; v < (uint8_t)(voiceStart + numVoices) && v < 3; v++) {
    volRaw[v] = 0;
    // leave inc/acc alone; we just want silence
  }
}

static void galaga_refresh_tick() {
  // capture previous in_sound state so we can silence voices on end transitions
  for (uint8_t s = 0; s < (uint8_t)sizeof(in_sound); s++) prev_in_sound[s] = in_sound[s];

// -------------------- sound00 (special sweep) --------------------
// Sound00 is a continuous 1-voice sweep (voice0) driven by TABLE1/TABLE2.
// It runs as long as do_sound[0x00] is asserted.
if (do_sound[0x00]) {
  if (!sound00_active) sound00_start();
  in_sound[0x00] = 1;
  sound00_tick_update();
} else {
  if (sound00_active) sound00_stop();
  in_sound[0x00] = 0;
}


  if (do_sound[0x13]) {
    do_sound[0x13] = 0;
    start_sound(0x13);
  } else if (in_sound[0x13]) {
    continue_sound(0x13);
  }
  if (do_sound[0x0F]) {
    do_sound[0x0F] = 0;
    start_sound(0x0F);
  } else if (in_sound[0x0F]) {
    continue_sound(0x0F);
  }

  // sound0E (special: 3-voice echo; PIC overrides vol[1]=6, vol[2]=9 while active)
  if (do_sound[0x0E]) {
    do_sound[0x0E] = 0;
    start_sound(0x0E);
  } else if (in_sound[0x0E]) {
    continue_sound(0x0E);
  }

  if (do_sound[0x03]) {
    do_sound[0x03] = 0;
    start_sound(0x03);
  } else if (in_sound[0x03]) {
    continue_sound(0x03);
  }
  if (do_sound[0x02]) {
    do_sound[0x02] = 0;
    start_sound(0x02);
  } else if (in_sound[0x02]) {
    continue_sound(0x02);
  }
  if (do_sound[0x04]) {
    do_sound[0x04] = 0;
    start_sound(0x04);
  } else if (in_sound[0x04]) {
    continue_sound(0x04);
  }
  if (do_sound[0x01]) {
    do_sound[0x01] = 0;
    start_sound(0x01);
  } else if (in_sound[0x01]) {
    continue_sound(0x01);
  }
  if (do_sound[0x12]) {
    do_sound[0x12] = 0;
    start_sound(0x12);
  } else if (in_sound[0x12]) {
    continue_sound(0x12);
  }

  // sound05 (special looping)
  if (do_sound[0x05]) {
    play_sound_bis(0x05);

    sound05_count++;
    if (sound05_count > 6) {
      sound05_count = 0;
      if (sound05_vol < 4) {
        sound05_vol = 0x0C;
      } else {
        sound05_vol--;
      }
    }
    // force voice2 volume
    volRaw[2] = sound05_vol;
  } else {
    in_sound[0x05] = 0;
  }

  // sound06 (special looping)
  if (do_sound[0x06]) {
    play_sound_bis(0x06);

    sound06_count++;
    if (sound06_count == 0x1C) {
      sound06_count = 0;
      sound06_wavesel = (sound06_wavesel + 1) & 0x07;
}
    // force voice2 wavesel
    waveSel[2] = (sound06_wavesel & 0x07);
  } else {
    in_sound[0x06] = 0;
  }

  // sound09 (bis, stop when do_sound deasserts)
  if (do_sound[0x09]) {
    play_sound_bis(0x09);
  } else {
    in_sound[0x09] = 0;
  }

  // sound07 (one-shot)
  if (do_sound[0x07]) {
    play_sound(0x07);
  }

  // sound11 (bis, stop when do_sound deasserts)
  if (do_sound[0x11]) {
    play_sound_bis(0x11);
  } else {
    in_sound[0x11] = 0;
  }

  // sound0D (one-shot)
  if (do_sound[0x0D]) {
    play_sound(0x0D);
  }

  // sound0E (one-shot + forced volumes while active)
  if (do_sound[0x0E]) {
    play_sound(0x0E);
  }
  if (do_sound[0x0E]) {
    // while 0x0E still active, force volumes (matches check_0E_part2)
    {
      // Apply only to currently-used voices; otherwise we re-enable an inactive voice and get a "random pitch" buzz.
      bool used[3] = {false, false, false};
      uint8_t vv = voiceStart;
      for (uint8_t ii = 0; ii < numVoices; ii++) {
        if (vv < 3) used[vv] = true;
        vv++;
      }
      volRaw[2] = used[2] ? 9 : 0;
      volRaw[1] = used[1] ? 6 : 0;
    }
  }

  // Remaining one-shot sounds, in order
  if (do_sound[0x14]) play_sound(0x14);
  if (do_sound[0x15]) play_sound(0x15);
  if (do_sound[0x0A]) play_sound(0x0A);
  if (do_sound[0x0B]) play_sound(0x0B);

  // sound10 (bis, but does NOT clear in_sound when do_sound deasserts in ASM)
  if (do_sound[0x10]) {
    play_sound_bis(0x10);
  } else {
    do_sound[0x10] = 0;
  }

  if (do_sound[0x0C]) play_sound(0x0C);
  if (do_sound[0x16]) play_sound(0x16);
  if (do_sound[0x08]) play_sound(0x08);

  // Sound 0x0A (decimal 10) envelope alignment: voice1 is scripted at 0x0A,
  // but the other two voices fade. Clamp voice1 to the current fade level so
  // it fades in-sync (prevents late/long tail).
  if (in_sound[10]) {
    uint8_t v0n = (volRaw[0] & 0x0F);
    uint8_t v2n = (volRaw[2] & 0x0F);
    uint8_t env = (v0n < v2n) ? v0n : v2n;
    uint8_t v1n = (volRaw[1] & 0x0F);
    if (v1n > env) {
      volRaw[1] = (volRaw[1] & 0xF0) | env;
    }
  }

  // Sound 0x0A (decimal 10) tail fade: once voices 0 & 2 are silent, fade voice1 instead of reasserting 0x0A.
  if (in_sound[10]) {
    uint8_t v0 = (volRaw[0] & 0x0F);
    uint8_t v2 = (volRaw[2] & 0x0F);
    if (!s10_fading && v0 == 0 && v2 == 0) {
      s10_fading = true;
      s10_fadeVol1 = (volRaw[1] & 0x0F);
    }
  } else {
    s10_fading = false;
    s10_fadeVol1 = 0;
  }

  for (uint8_t v = 0; v < 3; v++) {
    if ((volRaw[v] & 0x0F) == 0) {
      inc16[v] = 0;
      acc16[v] = 0;
    }
  }

  // Update "playing" gate: active if any in_sound[] is set
  bool any = false;
  for (uint8_t i = 0; i < (sizeof(in_sound) / sizeof(in_sound[0])); i++) {
    if (in_sound[i]) { any = true; break; }
  }
  playing = any;
  // silence voices for sounds that ended this tick (covers alternate end paths)
  for (uint8_t s = 0; s < (uint8_t)sizeof(in_sound); s++) {
    if (prev_in_sound[s] && !in_sound[s]) {
      mute_sound_voices(s);
    }
  }
}

static void galaga_stop_all() {
  memset(do_sound, 0, sizeof(do_sound));
  memset(in_sound, 0, sizeof(in_sound));
  sound00_stop();
  sound23_stop_explosion();

  sound05_count = 0;
  sound05_vol   = 0x0C;
  sound06_count = 0;
  sound06_wavesel = 0;

  volRaw[0] = volRaw[1] = volRaw[2] = 0;
  inc16[0]  = inc16[1]  = inc16[2]  = 0;
  waveSel[0]= waveSel[1]= waveSel[2]= 0;
  acc16[0]  = acc16[1]  = acc16[2]  = 0;

  playing = false;
  currentSound = -1;
}

// Trigger a sound (clears other sounds).
static void trigger_sound(uint8_t sound) {
  sound &= 0x1F; // safety (commands may arrive as ASCII-parsed values)
  if (sound >= 24) return;

  // Stop only if the new sound overlaps voices with the currently playing sound.
  if (get_sound_voice_mask(sound)) {
    galaga_stop_all();
  }

  // New sound becomes current owner (even if it only uses a subset of voices).
  currentSound = (int8_t)sound;

  if (sound == SOUND23_ID) {
    sound23_start_explosion();
    return;
  }

  // For looping sounds, keep do_sound asserted until stop/next trigger.
  do_sound[sound] = 1;

  // Reset loop state on start
  if (sound == 0x05) {
    sound05_count = 0;
    sound05_vol   = 0x0C;
  }
  if (sound == 0x06) {
    sound06_count = 0;
    sound06_wavesel = 0;
  }
}

// -------------------- Audio ISR (Timer1 @ 62500 Hz) --------------------
ISR(TIMER1_COMPA_vect) {
  int16_t mix = 0;

  if (playing) {
    // Fully unrolled, branchless 3-voice mix:
    // - No multiplies/divides in ISR (LUT-driven)
    // - No per-voice volume branches (vol=0 contributes 0 via LUT)
    //
    // Voice 0
    uint8_t vol0 = ((gVoiceOutMask & 0x01) ? (volRaw[0] & 0x0F) : 0);
    acc16[0] += inc16[0];
    uint8_t acc0 = (uint8_t)(acc16[0] >> 8);
    uint8_t idx0 = (acc0 & 0xF8) | waveSel[0];
    uint8_t s0   = prom4_ram[idx0];
    mix += sampVolLUT[(s0 << 4) | vol0];

    // Voice 1
    uint8_t vol1 = ((gVoiceOutMask & 0x02) ? (volRaw[1] & 0x0F) : 0);
    acc16[1] += inc16[1];
    uint8_t acc1 = (uint8_t)(acc16[1] >> 8);
    uint8_t idx1 = (acc1 & 0xF8) | waveSel[1];
    uint8_t s1   = prom4_ram[idx1];
    mix += sampVolLUT[(s1 << 4) | vol1];

    // Voice 2
    uint8_t vol2 = ((gVoiceOutMask & 0x04) ? (volRaw[2] & 0x0F) : 0);
    acc16[2] += inc16[2];
    uint8_t acc2 = (uint8_t)(acc16[2] >> 8);
    uint8_t idx2 = (acc2 & 0xF8) | (waveSel[2] & 0x07);
    uint8_t s2   = prom4_ram[idx2];
    mix += sampVolLUT[(s2 << 4) | vol2];
  }
  // Sound 23: Explosion (brown-ish noise) mixed in regardless of 'playing'
  if (s23_active && s23_env_gain_q8 != 0) {
    // --- 120 Hz envelope tick derived here (no Timer0/micros dependency) ---
    // Keep ISR prologue small: no 32-bit math, no division.
    uint16_t acc = (uint16_t)(s23_tick_acc + 120);
    if (acc >= 62500) {
      acc = (uint16_t)(acc - 62500);
      s23_tick_acc = acc;

      uint16_t t = s23_tick;
      if (t < S23_EXPLOSION_TICKS) {
        s23_env_gain_q8 = pgm_read_byte_near(s23_gain_table_q8 + t);
        s23_tick = (uint16_t)(t + 1);
      } else {
        s23_env_gain_q8 = 0;
      }

      if ((s23_env_gain_q8 == 0) || (s23_tick >= S23_EXPLOSION_TICKS)) {
        s23_active = false; // cheap stop inside ISR
        s23_env_gain_q8 = 0;
      }
    } else {
      s23_tick_acc = acc;
    }

    // --- LFSR update only every S23_NOISE_DIV samples (darker + cheaper) ---
    uint16_t nd = (uint16_t)(s23_noise_div_ctr + 1);
    if (nd >= S23_NOISE_DIV) {
      nd = 0;

      // Galois LFSR (poly 0xB400)
      uint16_t x = s23_lfsr;
      uint16_t lsb = (uint16_t)(x & 1u);
      x >>= 1;
      if (lsb) x ^= 0xB400u;
      s23_lfsr = x;

      s23_white_hold = (int16_t)((int8_t)(x & 0xFF)); // -128..127
    }
    s23_noise_div_ctr = nd;

    // --- Brown-ish: integrate with leak ---
    int16_t bx = s23_brown_x;
    bx = (int16_t)(bx + s23_white_hold);
    bx = (int16_t)(bx - (bx >> S23_BROWN_LEAK_SHIFT));
    s23_brown_x = bx;

    // --- Cheap low-pass (power-of-two) ---
    int16_t y = s23_lp_y;
    y += (int16_t)((bx - y) >> S23_LP_SHIFT);
    s23_lp_y = y;

    // Clamp to 8-bit before scaling (keeps multiplies 16-bit-safe)
    int16_t y8 = y;
    if (y8 > 127) y8 = 127;
    if (y8 < -128) y8 = -128;

    // --- Envelope + gain (all 16-bit) ---
    uint8_t eg = (uint8_t)s23_env_gain_q8;      // 0..255
    int16_t s = (int16_t)(((int16_t)y8 * (int16_t)eg) >> 8);  // stays within int16
    s = (int16_t)(((int16_t)s * (int16_t)S23_GAIN_NUM) >> S23_GAIN_SHIFT);

    if (s > 127) s = 127;
    if (s < -128) s = -128;
    mix += s;
  }

  // Safety: if nothing is playing, force exact mid-rail (silence).
  if (!playing && !s23_active) {
    audioWrite6(32);
    return;
  }

  // Clamp to LUT range [-192..+192] then output 6-bit code (0..63), scaled to 8-bit PWM (0..252)
  if (mix > 192) mix = 192;
  else if (mix < -192) mix = -192;

  uint8_t code6 = mixToDac6[(uint16_t)(mix + 192)];
  audioWrite6(code6);
}

// -------------------- Timer setup --------------------
static void setupAudioTimer1_62500Hz() {
  cli();
  TCCR1A = 0;
  TCCR1B = 0;

  // CTC mode
  TCCR1B |= (1 << WGM12);
  // Prescaler = 1
  TCCR1B |= (1 << CS10);
  // OCR1A = 16e6/62500 - 1 = 255
  OCR1A = 255;
  // Enable compare match interrupt
  TIMSK1 |= (1 << OCIE1A);
  sei();
}

// -------------------- Serial command parsing --------------------
static bool parseSoundId(const String& s, uint8_t& out) {
  String t = s;
  t.trim();
  if (t.length() == 0) return false;
  if (t.equalsIgnoreCase("stop")) { out = 0xFF; return true; }
  if (t == "`") { out = 0xFF; return true; }

  // IMPORTANT: don't accept partial parses (e.g. "p1" -> 0). Only accept if the
  // entire trimmed string is a valid number.
  const char* c = t.c_str();
  const bool isHex = (t.startsWith("0x") || t.startsWith("0X"));
  char* end = nullptr;
  long val = strtol(c, &end, isHex ? 16 : 10);

  // No digits parsed.
  if (end == c) return false;

  // Allow trailing whitespace only (shouldn't be any after trim, but be safe).
  while (*end == ' ' || *end == '\t' || *end == '\r') end++;
  if (*end != '\0') return false;

  if (val < 0 || val > 23) return false;
  out = (uint8_t)val;
  return true;
}

// Handle one complete command line (already trimmed, non-empty).
static void handleCommandLine(String line) {
  line.trim();

  // Debug command: m<0-7> sets voice output mask (bit0=v0, bit1=v1, bit2=v2)
  if (line.length() >= 2 && (line[0] == 'm' || line[0] == 'M')) {
    char ch = line[1];
    uint8_t v = 0;
    if (ch >= '0' && ch <= '7') v = (uint8_t)(ch - '0');
    else if (ch >= 'a' && ch <= 'f') v = (uint8_t)(10 + (ch - 'a'));
    else if (ch >= 'A' && ch <= 'F') v = (uint8_t)(10 + (ch - 'A'));
    gVoiceOutMask = (v & 0x07);
    Serial.print(F("Voice mask set to 0x"));
    Serial.println(gVoiceOutMask, HEX);
    return;
  }

  // Debug command: d0 / d0s toggles sound00 logging
  if (line == "d0" || line == "D0") {
    gSound00LogTicks = !gSound00LogTicks;
    Serial.print(F("sound00 tick logging: "));
    Serial.println(gSound00LogTicks ? F("ON") : F("OFF"));
    return;
  }
  if (line == "d0s" || line == "D0S" || line == "D0s" || line == "d0S") {
    gSound00LogSegments = !gSound00LogSegments;
    Serial.print(F("sound00 segment logging: "));
    Serial.println(gSound00LogSegments ? F("ON") : F("OFF"));
    return;
  }

  uint8_t sid;
  if (parseSoundId(line, sid)) {
    if (sid == 0xFF) {
      galaga_stop_all();
      Serial.println(F("Stopped (all sounds)."));
      suppressRepeatOnce = false;
    } else {
      // IMPORTANT: trigger_sound() calls galaga_stop_all() first, so this should
      // interrupt any currently playing sound (including sound 23).
      lastSoundCmd = (int8_t)sid;
      trigger_sound(sid);
      suppressRepeatOnce = false;
      Serial.print(F("Play sound "));
      Serial.print(sid);
      Serial.print(F(" - "));
      if (sid < (sizeof(soundNames) / sizeof(soundNames[0]))) {
#if defined(ARDUINO_ARCH_AVR)
        char buf[48];
        strcpy_P(buf, (PGM_P)pgm_read_ptr(&soundNames[sid]));
        Serial.println(buf);
#else
        Serial.println(soundNames[sid]);
#endif
      } else {
        Serial.println(F("Unknown"));
      }
    }
  } else {
    // If it's a clean numeric command but out of range, report it.
    // Otherwise, treat unknown commands as STOP (safe default).
    String t = line;
    t.trim();
    const char* c = t.c_str();
    const bool isHex = (t.startsWith("0x") || t.startsWith("0X"));
    char* end = nullptr;
    long val = strtol(c, &end, isHex ? 16 : 10);
    if (end != c) {
      while (*end == ' ' || *end == '\t' || *end == '\r') end++;
      if (*end == '\0') {
        // Numeric but invalid range.
        Serial.print(F("Sound out of range: "));
        Serial.println(val);
        Serial.println(F("Valid range: 0-23"));
        suppressRepeatOnce = true;
        return;
      }
    }

    // Anything else stops all sounds (avoid accidental parse-to-0).
    galaga_stop_all();
    Serial.println(F("Stopped (all sounds)."));
      suppressRepeatOnce = false;
  }
}


void setup() {

  Serial.begin(115200);

  audioInit();
  delay(50);

  memset(states_, 0, sizeof(states_));
  memset(positions_, 0, sizeof(positions_));
  
  // Build ISR lookup tables (RAM) so audio interrupt stays lightweight
  for (uint16_t i = 0; i < 256; i++) {
    uint8_t prom = pgm_read_byte_near(galaga_wavetable_prom + i);
    prom4_ram[i] = (prom >> 4) & 0x0F;
  }
  for (uint16_t s = 0; s < 16; s++) {
    for (uint16_t v = 0; v < 16; v++) {
      mul4x4_ram[(s << 4) | v] = (uint8_t)(s * v);
    }
  }

// Build signed samp*vol LUT (centered around 0) for ISR (no multiply)
for (uint16_t s = 0; s < 16; s++) {
  int8_t sc = (int8_t)s - 8; // -8..+7
  for (uint16_t v = 0; v < 16; v++) {
    sampVolLUT[(s << 4) | v] = (int8_t)(sc * (int8_t)v); // -120..+105
  }
}

// Build mix->6bit DAC LUT: clamp [-192..+192], map to [0..63]
for (int16_t m = -192; m <= 192; m++) {
  // 384 steps wide; use 63 codes. Add half-divisor for rounding.
  uint16_t u = (uint16_t)(m + 192); // 0..384
  uint8_t out = (uint8_t)((u * 63 + 192) / 384);
  if (out > 63) out = 63;
  mixToDac6[(uint16_t)(m + 192)] = out;
}

  setupAudioTimer1_62500Hz();
  Serial.println(F("Galaga audio ready. Send 0-23 or 0x00-0x17. Send 'stop'."));

}

void loop() {
  // Galaga refresh tick using micros()
  uint32_t now = micros();
  
  const uint32_t SEQ_US = 8333UL; // 120 Hz (tuned)
  //const uint32_t SEQ_US = 16667UL;  // ~60 Hz

  while ((uint32_t)(now - last60) >= SEQ_US) {
    last60 += SEQ_US;
    galaga_refresh_tick();
  }

  // Non-blocking serial line reader.
  // This avoids Serial.readStringUntil() timeouts (which can feel like "queuing")
  // when the serial monitor isn't sending a newline.
  static char lineBuf[40];
  static uint8_t lineLen = 0;
  static uint32_t lastCharUs = 0;


  while (Serial.available()) {
    char c = (char)Serial.read();
    lastCharUs = micros();

    // End-of-line: process buffered command if present.
    // If the line is empty, repeat the last sound command.
    if (c == '\n' || c == '\r') {
      if (lineLen == 0) {
        if (suppressRepeatOnce) {
          suppressRepeatOnce = false;
          continue;
        }
        if (lastSoundCmd >= 0) {
          trigger_sound((uint8_t)lastSoundCmd);
        }
        continue;
      }
      lineBuf[lineLen] = ' ';
      String line = String(lineBuf);
      lineLen = 0;
      handleCommandLine(line);
      continue;
    }

    // Accumulate printable chars (ignore leading spaces).
    if (c == ' ' || c == '\t') {
      if (lineLen == 0) continue;
    }

    if (lineLen < (sizeof(lineBuf) - 1)) {
      lineBuf[lineLen++] = c;

      // If we're receiving raw digits with no newline (e.g. `screen`),
      // auto-trigger once we have 2 digits (max for 0-23).
      if (lineLen == 2) {
        lineBuf[lineLen] = '\0';
        handleCommandLine(String(lineBuf));
        lineLen = 0;
      }

    } else {
      // Overflow: reset buffer so we don't parse partial garbage.
      lineLen = 0;
    }
  }

  // If the sender doesn't use newlines (e.g. `screen`), allow single-digit commands
  // by parsing after a short inter-character timeout.
  if (lineLen > 0) {
    const uint32_t idleUs = (uint32_t)(micros() - lastCharUs);
    if (idleUs > 80000UL) { // 80 ms: long enough to type "23" but still feels instant
      lineBuf[lineLen] = '\0';
      handleCommandLine(String(lineBuf));
      lineLen = 0;
    }
  }

}