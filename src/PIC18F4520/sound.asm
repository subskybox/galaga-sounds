;; 
;;  $Id: sound.asm,v 1.32 2008/06/26 07:20:06 fvecoven Exp $
;; 
;;  Copyright (C) 2008 Frederic Vecoven
;; 
;;  This file is part of Pacsound
;; 
;;  Pacsound is free software; you can redistribute it and/or modify
;;  it under the terms of the GNU General Public License as published by
;;  the Free Software Foundation; either version 3 of the License, or
;;  (at your option) any later version.
;; 
;;  Pacsound is distributed in the hope that it will be useful,
;;  but WITHOUT ANY WARRANTY; without even the implied warranty of
;;  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
;;  GNU General Public License for more details.
;; 
;;  You should have received a copy of the GNU General Public License
;;  along with this program. If not, see <http://www.gnu.org/licenses/>
;; 

	list p=18f4520
	#include p18f4520.inc


; timer 0 : update one voice
; 
; Pacman drives the audio FSM at 3MHz. It takes 64 cycles to update the 3 voices,
; the first voice gets 24 cycles, and the other two get 20 cycles. So the voice 
; refresh rate is about 150kHz.

#define TMR0_RELOAD	0xEA

; Timer1 reload presets (selected at runtime)
#define TMR1_PACMAN_H   0xAE
#define TMR1_PACMAN_L   0x9F

#define TMR1_GALAGA_H   0xD9
#define TMR1_GALAGA_L   0x04


; flags
#define F60HZ		Flags, 0
#define SOUND_PACMAN	Flags, 1	; 1 = Pacman, 0 = Galaga


var_g1	UDATA_ACS	0x0

temp0		res	1
temp1		res	1
galTemp		res	1
Flags		res	1	; flags
hCount		res	1	; 60Hz counter

; used in update_volume
gType		res	1
gVol		res	1


	GLOBAL	temp0, temp1, gType, gVol, hCount, Flags

var_g2	UDATA_ACS	0x8
; frequency, accumulator, wave select and volume for each voice
v0_hFreq_L	res	1	; address 0x08
v0_hAcc_L	res	1
v0_hFreq_M	res	1
v0_hAcc_M	res	1
v0_hFreq_H	res	1
v0_hAcc_H	res	1
v0_hWaveSel	res	1
v0_hVol		res	1
v1_hFreq_L	res	1	; address 0x10
v1_hAcc_L	res	1
v1_hFreq_M	res	1
v1_hAcc_M	res	1
v1_hFreq_H	res	1
v1_hAcc_H	res	1
v1_hWaveSel	res	1
v1_hVol		res	1
v2_hFreq_L	res	1	; address 0x18
v2_hAcc_L	res	1
v2_hFreq_M	res	1
v2_hAcc_M	res	1
v2_hFreq_H	res	1
v2_hAcc_H	res	1
v2_hWaveSel	res	1
v2_hVol		res	1
				; address 0x20

	GLOBAL v0_hFreq_L, v0_hFreq_M, v0_hFreq_H, v0_hWaveSel, v0_hVol
	GLOBAL v1_hFreq_L, v1_hFreq_M, v1_hFreq_H, v1_hWaveSel, v1_hVol
	GLOBAL v2_hFreq_L, v2_hFreq_M, v2_hFreq_H, v2_hWaveSel, v2_hVol

; Timer1 reload bytes (kept in ACCESS RAM, placed after voice block)
var_g3  UDATA_ACS 0x20
t1_reload_h     res 1
t1_reload_l     res 1

; 
; BANK 4 : wave table
;
var_4	UDATA	0x400

table		res	0xff


; extern functions from pacman module
	extern	pacman_refresh, pacman_wavetable, pacman_stop_all

; extern functions from galaga module
    extern  galaga_refresh, galaga_wavetable, galaga_sound, galaga_stop_all

	
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; ENTRY POINTS
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
	ORG	0x0000
	nop
	goto	main
	
	ORG	0x0008
	goto	int_handler
	
	
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; INTERRUPT HANDLER
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
	ORG 	0x0100
	
int_code	CODE

int_handler
	btfss	PIR1, TMR1IF
	bra	int_test_0
	call	int_timer1
	bcf	PIR1, TMR1IF
int_test_0
	btfss	INTCON, TMR0IF
	bra	int_test_2
	call	int_timer0
	bcf	INTCON, TMR0IF
int_test_2
	retfie	FAST
	

;
; TIMER0
;
; Implements the namco hardware which runs at 1.5MHz.
; The namco hardware implements 3 adders (1x20 and 2x16 bits)
; using a 4-bits adder, 2 64x4 bits RAM, and a prom as a
; sequencer. The first adder is 20 bits by constraint, the 4 LSB
; are always 0.
; There are 3 voices, which are controlled by a frequency
; setting (16 bits), a volume (4 bits) and a wave select (3 bits).
; At each step, the frequency is added to a 16-bits accumulator.
; The upper 5 bits are concatenated with the 3 bits of wave select,
; and a lookup is done in a prom (256x4bits). That gives the 4 LSB
; to output to the DAC. The 4 MSB are the volume bits.
;
; Our implementation is highly-optimized, because it's hard to get
; 3 voices from a 10MHz PIC. Basically, we want to have the shortest
; code here, this avoiding any test and minimizing the number of 
; instructions. Since voice 0 requires a 20 bits adder, we have
; a 24 bits adder. Frequency data, when set in voice 0, is left
; shifted of 4 bits, so the 24 bits adder becomes a 20 bits adder.
; For voices 1 and 2, the data is shifted by 8 bits, giving a 16
; bits addition. Note that it is not worth checking for voices 1 and
; 2 here, the overhead of having a 24 bits adder is just 2 instructions.
;
; The voices registers are accessed using indirect register, with
; post-increment mode to get to the next data. To detect when voice 2
; is completed, we use a bit test instruction. This works because the
; data is put in memory at a boundary : when the last byte of voice 2
; has been accessed, the FSR0L value is 0x20. So we can simply test
; if bit 5 is set, in which case we reload FSR0 to the first data of
; voice 0.

;
; We assume that the lookup table (256 bytes) is aligned.
;
int_timer0
	movlw	TMR0_RELOAD		; reload timer
	movwf	TMR0L
	
	movf	POSTINC0, w		; freq low byte
	addwf	POSTINC0, f		; added to acc low byte
	movf	POSTINC0, w		; freq middle byte
	addwfc	POSTINC0, f		; added to acc middle byte + carry
	movf	POSTINC0, w		; freq high byte
	addwfc	INDF0, f		; added to acc high byte + carry

	movf	POSTINC0, w		; get acc high byte
	andlw	0xF8			; keep upper 5 bits
	iorwf	POSTINC0, w		; set wavelsel to lower 3 bits
	movwf	FSR2L			; so we get the location in the table
	movf	INDF2, w		; do the table lookup (high nibble)
	iorwf	POSTINC0, w		; add volume bits (low nibble)
	movwf	LATD			; set it !

	btfss	FSR0L, 5		; smart location in memory
	return				; to allow easy detection of end

	lfsr	FSR0, v0_hFreq_L 	; back to voice 0
	return


; 
; TIMER1
;
; every 1/60s. simply set a flag and do the work in the main loop
;
int_timer1
	movf    t1_reload_h, w
	movwf   TMR1H
	movf    t1_reload_l, w
	movwf   TMR1L
	
	incf	hCount, f		; increment 60Hz counter
	bsf	F60HZ
	return

	
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; MAIN CODE
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
main_code	CODE

main
	; init ports
	clrf	TRISD
	clrf	LATD
	; RB0 = sound select: 1 = Pacman, 0 = Galaga
	bsf	TRISB, 0		; RB0 input
	btfsc	PORTB, 0
	bsf	SOUND_PACMAN
	btfss	PORTB, 0
	bcf	SOUND_PACMAN

	; ---- set Timer1 reload preset based on initial mode ----
	btfss   SOUND_PACMAN
	bra     init_t1_galaga

	; Pacman
	movlw   TMR1_PACMAN_H
	movwf   t1_reload_h
	movlw   TMR1_PACMAN_L
	movwf   t1_reload_l
	bra     init_t1_done

init_t1_galaga
	movlw   TMR1_GALAGA_H
	movwf   t1_reload_h
	movlw   TMR1_GALAGA_L
	movwf   t1_reload_l

init_t1_done

	; init timer0
	movlw	b'11001000'		; on, 8-bit, internal clock, no prescale
	movwf	T0CON
	
	; init timer1
	movlw	b'10110001'		; on, 16-bit, internal clock, 1:8 prescale
	movwf	T1CON
	movf    t1_reload_h, w
	movwf   TMR1H
	movf    t1_reload_l, w
	movwf   TMR1L
	
	; init interrupts
	clrf	INTCON
	bsf	INTCON, TMR0IE		; enable timer 0 interrupt
	bsf	INTCON, IPEN		; enable peripheral interrupt
	bsf	PIE1, TMR1IE		; enable timer 1 interrupt

	; init RAM
	lfsr	FSR1, 0x000
	movlw	.128
ram0	clrf	POSTINC0
	decfsz	WREG
	bra	ram0
	lfsr	FSR1, 0x100
	call	clear_bank
	lfsr	FSR1, 0x200
	call	clear_bank
	lfsr	FSR1, 0x300
	call	clear_bank
	lfsr	FSR1, 0x500
	call	clear_bank

	lfsr	FSR0, v0_hFreq_L 	; init FSR0
	; select wavetable at runtime: SOUND_PACMAN=1 -> pacman
	btfss	SOUND_PACMAN
	bra	call_galaga_wt
	call	pacman_wavetable
	bra	after_wt
call_galaga_wt
	call	galaga_wavetable
after_wt
	call    apply_mode_t1
	bsf	INTCON, GIE		; enable interrupts	
	call	debug_setup
		
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; MAIN LOOP
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
main_loop
	btfsc	F60HZ			; work to do ?
	call	timer_60hz

	nop
	goto	main_loop

; Force Timer1 reload to match SOUND_PACMAN (no change detection)
apply_mode_t1
    btfss   SOUND_PACMAN
    bra     amt_galaga

    ; Pacman
    bcf     PIE1, TMR1IE
    movlw   TMR1_PACMAN_H
    movwf   t1_reload_h
    movlw   TMR1_PACMAN_L
    movwf   t1_reload_l
    movf    t1_reload_h, w
    movwf   TMR1H
    movf    t1_reload_l, w
    movwf   TMR1L
    bsf     PIE1, TMR1IE
    return

amt_galaga
    bcf     PIE1, TMR1IE
    movlw   TMR1_GALAGA_H
    movwf   t1_reload_h
    movlw   TMR1_GALAGA_L
    movwf   t1_reload_l
    movf    t1_reload_h, w
    movwf   TMR1H
    movf    t1_reload_l, w
    movwf   TMR1L
    bsf     PIE1, TMR1IE
    return

;
; 60Hz tasks
;
timer_60hz
	; poll RB0 and reload wavetable on change (60Hz)
	btfsc	PORTB, 0
	bra	rb_one

	; RB0 == 0 -> want Galaga (SOUND_PACMAN = 0)
	btfss	SOUND_PACMAN		; if already 0, no change needed
	bra	no_rb_change
	bcf	SOUND_PACMAN		; was 1, switch to 0

	; ---- switch Timer1 reload to Galaga preset (apply immediately) ----
	bcf     PIE1, TMR1IE
	movlw   TMR1_GALAGA_H
	movwf   t1_reload_h
	movlw   TMR1_GALAGA_L
	movwf   t1_reload_l
	movf    t1_reload_h, w
	movwf   TMR1H
	movf    t1_reload_l, w
	movwf   TMR1L
	bsf     PIE1, TMR1IE

	call	galaga_wavetable
	bra	no_rb_change

rb_one
	; RB0 == 1 -> want Pacman (SOUND_PACMAN = 1)
	btfsc	SOUND_PACMAN		; if already 1, no change needed
	bra	no_rb_change
	bsf	SOUND_PACMAN		; was 0, switch to 1

	; ---- switch Timer1 reload to Pacman preset (apply immediately) ----
	bcf     PIE1, TMR1IE
	movlw   TMR1_PACMAN_H
	movwf   t1_reload_h
	movlw   TMR1_PACMAN_L
	movwf   t1_reload_l
	movf    t1_reload_h, w
	movwf   TMR1H
	movf    t1_reload_l, w
	movwf   TMR1L
	bsf     PIE1, TMR1IE

	call	pacman_wavetable

no_rb_change

	; runtime select refresh: Pacman or Galaga
	btfss	SOUND_PACMAN
	bra	call_galaga_ref
	call	pacman_refresh
	bra	after_ref
call_galaga_ref
	call	galaga_refresh
after_ref

	btfsc	PORTC,6
	call	debug_setup

	bcf	F60HZ
	return



; clear bank pointer by FSR1
;
clear_bank
	clrf	temp0
cb_1	clrf	POSTINC1
	decfsz	temp0
	bra	cb_1
	return



; make voices with 0 volume really silent
;
fix_voices
	GLOBAL	fix_voices
	
	tstfsz	v0_hVol
	bra	fv1
	clrf	v0_hFreq_L
	clrf	v0_hFreq_M
	clrf	v0_hFreq_H
	clrf	v0_hAcc_L
	clrf	v0_hAcc_M
	clrf	v0_hAcc_H
fv1	tstfsz	v1_hVol
	bra	fv2
	clrf	v1_hFreq_L
	clrf	v1_hFreq_M
	clrf	v1_hFreq_H
	clrf	v1_hAcc_L
	clrf	v1_hAcc_M
	clrf	v1_hAcc_H	
fv2	tstfsz	v2_hVol
	return
	clrf	v2_hFreq_L
	clrf	v2_hFreq_M
	clrf	v2_hFreq_H
	clrf	v2_hAcc_L
	clrf	v2_hAcc_M
	clrf	v2_hAcc_H
	return


; DEBUG START ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

; extern functions
	extern Effect, Wave
                                        ; RC0-4 : 0-31 number
										; RC6 : Triger
                                        ; RC7 : 1 = effect   0 = wave

debug_setup
	; runtime: if SOUND_PACMAN=1 use Pacman debug handling, else Galaga
	btfss	SOUND_PACMAN
	bra	call_galaga_sound

	; -------------------------
	; PACMAN MODE
	; -------------------------
	; Read selector once: RC0-4 = 0..31
	movf	PORTC, w
	andlw	0x1F
	movwf	temp1

	; If selector == 0x1F => STOP ALL PACMAN SOUNDS
	movlw	0x1F
	cpfseq	temp1
	bra	pacman_dispatch

	call	pacman_stop_all
	bra	set_done

pacman_dispatch
	; Pacman debug handler: RC7 = 1 -> effect, 0 -> wave
	btfss	PORTC, 7
	bra	set_wave

set_effect
	; temp1 already holds selector (0..31)
	movlb	.1
	movlw	.3
	cpfslt	temp1			; effect 1-2 are channel 1
	movlb	.2
	movf	temp1, w
	movwf	Effect
	bra	set_done

set_wave
	; temp1 already holds selector (0..31)
	movf	temp1, w
	incf	WREG, w
	rlncf	WREG, w
	movlb	.1
	decf	WREG, w
	movwf	Wave
	movlb	.2
	incf	WREG, w
	movwf	Wave
	bra	set_done


	; -------------------------
	; GALAGA MODE (your existing code)
	; -------------------------
call_galaga_sound
	movf	PORTC, w
	andlw	0x1F
	movwf	temp1			; keep selector in temp1

	movlw	0x1F
	cpfseq	temp1			; skip next if temp1 == 0x1F
	bra	do_galaga_play

	; selector == 31 => STOP ALL
	call	galaga_stop_all
	bra	set_done

do_galaga_play
	movf	temp1, w
	call	galaga_sound
	bra	set_done

set_done
	return
        
; DEBUG END ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;


	END
	