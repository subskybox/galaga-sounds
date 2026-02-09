# Galaga Sounds

A faithful emulation of the Namco Galaga arcade game sound chip, implemented for Arduino and PIC18 microcontrollers.

## Overview

This project recreates the complete sound palette of the classic 1981 Galaga arcade cabinet using the original Namco wavetable data and synthesis parameters. It supports all 24 sounds from the game, including special effects like the explosion and electronic noise synthesis.

**Features:**
- 24 authentic Galaga sound effects (0-23)
- 3-voice polyphonic synthesis
- Original Namco wavetable data
- Two audio output modes (PWM DAC or resistor DAC)
- 62.5 kHz sample rate audio synthesis
- Serial-based sound triggering
- Minimal hardware requirements

## Hardware

### Arduino Implementation

**Microcontroller:** Arduino Uno or compatible (ATmega328P)

**Audio Output Options:**

**Option 1: PWM DAC (Pin D11)**
- Simplest and most universal approach
- Uses Timer2 PWM (~62.5 kHz carrier, 8-bit resolution)
- Low-pass filter recommended:
  - Resistor: 10 kΩ (series from D11)
  - Capacitor: 10 nF (to ground)
  - Cutoff: ~1.6 kHz
- Optional larger capacitor (22 nF) for smoother/warmer sound

**Option 2: 6-bit Resistor DAC (Pins D2-D7)**
- Binary-weighted resistor network (Namco-style)
- Recommended resistor values (MSB → LSB):
  - D2 → 4.7 kΩ
  - D3 → 2.2 kΩ
  - D4 → 1.0 kΩ
  - D5 → 470 Ω
  - D6 → 220 Ω
  - D7 → 100 Ω or 120 Ω
- All resistors sum at a single node
- Optional 1-4.7 nF smoothing capacitor from output to ground

**Select output mode** in code via `#define AUDIO_OUT_MODE`:
- `AUDIO_OUT_PWM_D11` (default)
- `AUDIO_OUT_R2R_6BIT_D2_D7`

### PIC18 Implementation

Source code provided for PIC18F4520 microcontroller:
- `src/PIC18F4520/galaga.asm` - Main sound engine
- `src/PIC18F4520/sound.asm` - Audio synthesis
- `src/PIC18F4520/pacman.asm` - Related implementation

## Usage

### Serial Interface

Connect via USB/Serial at **115,200 baud**.

**Commands:**
- `0-23` or `0x00-0x17` - Play sound (hex or decimal)
- `stop` or `` ` `` - Stop all sounds
- `Enter` (empty line) - Repeat last sound
- `m0-7` - Set voice output mask (debug, bitmask for v0/v1/v2)
- `d0` / `d0s` - Toggle logging for sound 0 (debug)

**Examples:**
```
15       Play sound 15 (Shooting)
0x0A     Play sound 10 (Extra Life)
stop     Stop all sounds
```

Supports both newline-terminated and raw digit input (auto-triggers after 2 digits).

## Sound Reference

| # | Name | Description |
|---|------|-------------|
| 0 | Ambience | Background hum |
| 1 | Hit Capture Ship | Red target hit |
| 2 | Hit Red Ship | Enemy destroyed |
| 3 | Hit Blue Ship | Enemy destroyed |
| 4 | Hit Capture/Green | Mixed ship hit |
| 5 | Tractor Beam | Beam start |
| 6 | Tractor Beam Capture | Beam loops |
| 7 | Fighter Destroyed | Game over tone |
| 8 | Credit | Insert coin |
| 9 | Fighter Captured | Player captured |
| 10 | Extra Life | Bonus awarded |
| 11 | Game Start | Game beginning |
| 12 | Name Entry (1st) | High score entry |
| 13 | Challenge Start | Wave intro |
| 14 | Challenge Results | Wave end with echo |
| 15 | Shooting | Player fire |
| 16 | Name Entry (2nd-5th) | Score entry 2-5 |
| 17 | Fighter Rescued | Tractor beam rescue |
| 18 | Free Life/Triple | Bonus formation |
| 19 | Enemy Flying | Ambient UFO |
| 20 | Challenge Perfect | Perfect wave bonus |
| 21 | Stage Flag | Stage marker |
| 22 | Name Entry End | High score complete |
| 23 | Explosion | Noise (custom) |

## Technical Details

### Architecture

- **Audio sampling rate:** 62.5 kHz (Timer1 CTC @ 16 MHz / 256)
- **Scheduler:** 120 Hz game tick (updates synthesizer state)
- **Voice synthesis:**
  - 16-bit phase accumulator per voice
  - Wavetable lookup with volume envelope
  - Automatic phase/frequency scaling
  - Mix and quantize to 4-bit for authentic "crunch"
- **Volume quantization:** 4-bit per voice, 6-bit output DAC

### Compilation

**Define Custom Output Mode:**
```bash
# (Use ARDUINO_OUT_MODE compiler flag or edit galaga_sounds_pwm_or_r2r_configurable.ino)
Arduino IDE: Sketch → Properties → Compiler options → Custom flags
```

### Notable Implementations

- **Sound 0 (Ambience):** Continuous frequency sweep using lookup tables
- **Sound 5 & 6 (Tractor Beam):** Looping with automatic volume/waveform cycling
- **Sound 14 (Echo):** Progressive 3-voice echo effect (voices enabled sequentially)
- **Sound 23 (Explosion):** Brown noise with envelope decay (not in original tables, custom tuned)
- **Sound 10 (Extra Life):** Multi-voice with envelope alignment and tail fade

### ROM Data

Original Galaga waveform and frequency tables extracted from arcade hardware:
- Wavetable PROM: 256-byte lookup (4-bit unipolar samples)
- Frequency table: 13 base frequencies with octave shifts
- Script sequences: 1907 bytes of voice control data

## Files

```
.
├── README.md                         # This file
├── LICENSE                           # GNU General Public License v3
├── src/
│   ├── arduino/
│   │   ├── galaga_sounds_pwm_or_r2r_configurable/
│   │   │   └── galaga_sounds_pwm_or_r2r_configurable.ino
│   │   └── game_sounds_trigger/
│   │       └── game_sounds_trigger.ino
│   └── PIC18F4520/
│       ├── galaga.asm
│       ├── pacman.asm
│       └── sound.asm
└── reference/
    ├── galaga wavetables/
    ├── PIC18F4520_PinOut.html
    └── [Web references to Galaga docs]
```

## Building and Flashing

### Arduino

1. Open `src/arduino/galaga_sounds_pwm_or_r2r_configurable/galaga_sounds_pwm_or_r2r_configurable.ino` in Arduino IDE
2. Configure audio output mode (search for `#define AUDIO_OUT_MODE`)
3. Select board: **Arduino Uno**
4. Select port (e.g., `/dev/cu.usbserial-xxx`)
5. Click **Upload**

### PIC18F4520

Use a PIC programmer with MPLAB IDE or compatible assembler:
```bash
gpasm -p 18f4520 galaga.asm
```

Requires PIC18 development environment (ICD3/ICD4 programmer or equivalent).

## Attribution

- **Original concept:** Namco sound chip (1981 Galaga arcade)
- **PIC18 reference:** [Fred Vecoven's Pacsound project](https://www.vecoven.com/elec/galaga/code/gal_pic.html)
- **Arduino port & PWM/DAC implementations:** Modern adaptation with extended features
- **Wavetable data:** From original Galaga arcade ROM

## License

GNU General Public License v3.0 - See LICENSE file for details.

## Notes

- The explosion sound (23) is a custom implementation, not from original arcade tables
- Audio feeds high-impedance input (amp/powered speaker); do not drive speakers directly
- Volume is applied in software and quantized back to 4-bit for authentic crunchy character
- Pitch scaling matches original (increment multiplied by 393/256 ≈ 1.535×)
