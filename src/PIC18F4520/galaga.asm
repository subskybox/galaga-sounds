;; 
;;  $Id: galaga.asm,v 1.12 2008/06/26 18:16:25 fvecoven Exp $
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


	; 
	; BANK 5
	;
var_5	UDATA	0x500

sound_number	res	1
voice_number 	res	1
num_voices	res	1
chunk_done	res	1
data_byte	res	1
data_next	res	1
bfreq		res	2
vol		res	3	; volume of each voice (v0, v1 and v2)
freq		res	6	; frequency of each voice
wavesels	res	3	; wavesel of each voice
offset		res	1
data0		res	1
data1		res	1
wavesel		res	1
sound05_count	res	1
sound05_vol	res	1
sound06_count	res	1
sound06_wavesel	res	1
states		res	43	; 9A00  : state information
do_sound	res	23	; 9AA0  : set to '1' to play the corresponding sound
in_sound	res	23	; 9AC0  : set by the code when sound is playing
positions	res	43	; 9A30  : tables of offsets
acc16		res	2
data16		res	2

	
; variables from main module
	extern	temp0, temp1, gType, gVol, hCount, Flags
	extern v0_hFreq_L, v0_hFreq_M, v0_hFreq_H, v0_hWaveSel, v0_hVol
	extern v1_hFreq_L, v1_hFreq_M, v1_hFreq_H, v1_hWaveSel, v1_hVol
	extern v2_hFreq_L, v2_hFreq_M, v2_hFreq_H, v2_hWaveSel, v2_hVol
; functions from main module
	extern fix_voices
	

galaga_code	CODE


;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; PLAY A GALAGA SOUND
;
; Arg: W = sound to play (0 - 16h)
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
galaga_sound
	GLOBAL	galaga_sound
	
	movlb	.5
	lfsr	FSR1, in_sound
	addwf	FSR1L, f
	tstfsz	INDF1
	return			; already in_sound
	
	lfsr	FSR1, do_sound
	addwf	FSR1L, f
	tstfsz	INDF1
	return			; already do_sound
	
	incf	INDF1, f
	return
	

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; INSTALL GALAGA WAVETABLE
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
galaga_wavetable
	GLOBAL	galaga_wavetable

	clrf	TBLPTRU
	movlw	high(galaga_wavetable_data)
	movwf	TBLPTRH
	movlw	low(galaga_wavetable_data)
	movwf	TBLPTRL
	lfsr	FSR1, 0x400		; bank 4
	clrf	temp0
cwt	tblrd*+
	movf	TABLAT, w
	movwf	POSTINC1
	decfsz	temp0
	bra	cwt
	lfsr	FSR2, 0x400		; FSR2 = pointer to wavetable
	return
		

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; GALAGA REFRESH
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
galaga_refresh
	GLOBAL	galaga_refresh
	
	movlb	.5		; galaga data is in bank 5
	
check_00
	; XXX : add this
check_13
	movlw	0x13
	movwf	sound_number
	tstfsz	do_sound+0x13
	bra	$+4
	bra	check_cont_13
	clrf	do_sound+0x13
	call	start_sound
	bra	check_0F
check_cont_13
	tstfsz	in_sound+0x13
	call	continue_sound
	
check_0F
	movlw	0x0F
	movwf	sound_number
	tstfsz	do_sound+0x0F
	bra	$+4
	bra	check_cont_0F
	clrf	do_sound+0xF
	call	start_sound
	bra	check_03
check_cont_0F
	tstfsz	in_sound+0x0F
	call	continue_sound
	
check_03	
	movlw	0x03
	movwf	sound_number
	tstfsz	do_sound+0x3
	bra	$+4
	bra	check_cont_03
	clrf	do_sound+0x3
	call	start_sound
	bra	check_02
check_cont_03
	tstfsz	in_sound+0x3
	call	continue_sound
	
check_02
	movlw	0x02
	movwf	sound_number
	tstfsz	do_sound+0x2
	bra	$+4
	bra	check_cont_02
	clrf	do_sound+0x2
	call	start_sound
	bra	check_04
check_cont_02
	tstfsz	in_sound+0x2
	call	continue_sound
	
check_04
	movlw	0x04
	movwf	sound_number
	tstfsz	do_sound+0x4
	bra	$+4
	bra	check_cont_04
	clrf	do_sound+0x4
	call	start_sound
	bra	check_01
check_cont_04
	tstfsz	in_sound+0x4
	call	continue_sound
	
check_01
	movlw	0x01
	movwf	sound_number
	tstfsz	do_sound+0x1
	bra	$+4
	bra	check_cont_01
	clrf	do_sound+0x1
	call	start_sound
	bra	check_12
check_cont_01
	tstfsz	in_sound+0x1
	call	continue_sound
	
check_12
	movlw	0x12
	movwf	sound_number
	tstfsz	do_sound+0x12
	bra	$+4
	bra	check_cont_12
	clrf	do_sound+0x12
	call	start_sound
	bra	check_05
check_cont_12
	tstfsz	in_sound+0x12
	call	continue_sound
	
check_05
	tstfsz	do_sound+0x5
	bra	$+4
	bra	not_05
	movlw	0x5
	movwf	sound_number
	call	play_sound_bis
	incf	sound05_count, f
	movlw	.7
	cpfslt	sound05_count
	bra	set_vol_05		; if (sound05_count <= 6) then
	clrf	sound05_count		;     sound05_count = 0
	movlw	.4
	cpfslt	sound05_vol		;     if (sound05_vol < 4) 
	bra	xx_05
	movlw	0xC
	movwf	sound05_vol		;     then sound05_vol = 0xC
	bra	set_vol_05
xx_05	decf	sound05_vol, f		;     else sound05_vol--
set_vol_05
	movf	sound05_vol, w
	movwf	vol+2			; voice2_vol = sound05_vol
	bra	check_06
not_05
	clrf	in_sound+0x5

check_06
	tstfsz	do_sound+0x6
	bra	$+4
	bra	not_06
	movlw	0x6
	movwf	sound_number
	call	play_sound_bis
	incf	sound06_count, f
	movlw	0x1C
	cpfseq	sound06_count
	bra	set_wav_06		; if (sound06_count == 0x1C) then
	clrf	sound06_count		;	sound06_count = 0
	incf	sound06_wavesel, f	;	sound06_wavesel++
set_wav_06
	movf	sound06_wavesel
	movwf	wavesels+2		; voice2_wavesel = sound06_wavesel
	bra	check_09
not_06
	clrf	in_sound+0x6	

check_09
	tstfsz	do_sound+0x9
	bra	$+4
	bra	not_09
	movlw	0x9
	movwf	sound_number
	call	play_sound_bis
	bra	check_07
not_09
	clrf	in_sound+0x9
	
check_07
	tstfsz	do_sound+0x7
	bra	$+4
	bra	check_11
	movlw	0x7
	movwf	sound_number
	call	play_sound
	
check_11
	tstfsz	do_sound+0x11
	bra	$+4
	bra	not_11
	movlw	0x11
	movwf	sound_number
	call	play_sound_bis
	bra	check_0D
not_11
	clrf	in_sound+0x11
	
check_0D
	tstfsz	do_sound+0xd
	bra	$+4
	bra	check_0E
	movlw	0xd
	movwf	sound_number
	call	play_sound
	
check_0E
	tstfsz	do_sound+0xe
	bra	$+4
	bra	check_0E_part2
	movlw	0xe
	movwf	sound_number
	call	play_sound
check_0E_part2
	tstfsz	do_sound+0xe
	bra	$+4
	bra	check_14
	movlw	.9
	movwf	vol+2
	movlw	.6
	movwf	vol+1 ; voice1 volume   (was vol+3)
	
check_14
	tstfsz	do_sound+0x14
	bra	$+4
	bra	check_15
	movlw	0x14
	movwf	sound_number
	call	play_sound
	
check_15
	tstfsz	do_sound+0x15
	bra	$+4
	bra	check_0A
	movlw	0x15
	movwf	sound_number
	call	play_sound
	
check_0A
	tstfsz	do_sound+0x0A
	bra	$+4
	bra	check_0B
	movlw	0x0A
	movwf	sound_number
	call	play_sound
	
check_0B
	tstfsz	do_sound+0x0B
	bra	$+4
	bra	check_10
	movlw	0x0B
	movwf	sound_number
	call	play_sound
	
check_10
	tstfsz	do_sound+0x10
	bra	$+4
	bra	not_10
	movlw	0x10
	movwf	sound_number
	call	play_sound_bis
	bra	check_0C
not_10
	clrf	do_sound+0x10
	
check_0C
	tstfsz	do_sound+0xC
	bra	$+4
	bra	check_16
	movlw	0xC
	movwf	sound_number
	call	play_sound
	
check_16
	tstfsz	do_sound+0x16
	bra	$+4
	bra	check_08
	movlw	0x16
	movwf	sound_number
	call	play_sound
	
check_08
	tstfsz	do_sound+0x8
	bra	$+4
	bra	check_done
	movlw	0x8
	movwf	sound_number
	call	play_sound

check_done		
	call	copy_to_hw
	return




;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; copy to hardware
;
copy_to_hw
#if 1
			; voice 0
	clrf	v0_hFreq_L
	movf	freq, w
	movwf	v0_hFreq_M
	movf	freq+1, w
	movwf	v0_hFreq_H
	movf	vol, w
	andlw	0xf
	movwf	v0_hVol
	movf	wavesels, w
	andlw	0x7
	movwf	v0_hWaveSel
#endif

#if 1
			; voice 1
	clrf	v1_hFreq_L
	movf	freq+2, w
	movwf	v1_hFreq_M
	movf	freq+3, w
	movwf	v1_hFreq_H
	movf	vol+1, w
	andlw	0xf
	movwf	v1_hVol
	movf	wavesels+1, w
	andlw	0x7
	movwf	v1_hWaveSel
#endif

#if 1

			; voice 2
	clrf	v2_hFreq_L
	movf	freq+4, w
	movwf	v2_hFreq_M
	movf	freq+5, w
	movwf	v2_hFreq_H
	movf	vol+2, w
	andlw	0xf
	movwf	v2_hVol
	movf	wavesels+2, w
	andlw	0x7
	movwf	v2_hWaveSel	
#endif

	call	fix_voices

	return


;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; play sound
;
play_sound
	call	get_data
	call	check_in_sound
ps_1	call	process_chunk
	decfsz	num_voices
	bra	ps_end
	tstfsz	chunk_done	; if (chunk_done == 0)
	bra	ps_2
	return			; then return
ps_2	clrf	chunk_done	; else chunk_done = 0
	lfsr	FSR1, in_sound
	movf	sound_number, w
	addwf	FSR1L, f
	clrf	INDF1		; in_sound[sound_number] = 0
	
	lfsr	FSR1, do_sound
	movf	sound_number, w
	addwf	FSR1L, f	; FSR1 : do_sound[sound_number]

			; sound 8	
	movlw	.8
	cpfseq	sound_number	; if (sound_number == 8)
	bra	ps_3		; then
	decf	INDF1, f	;   do_sound[sound_number]--
	return

ps_3
			; sound C : played twice and followed by 16
	movlw	0xC
	cpfseq	sound_number	; if (sound_number == 0xC)
	bra	ps_5		; then
	decf	INDF1, f	;   do_sound[sound_number]--
	btfsc	STATUS, Z	;   if (do_sound[sound_number] != 0)
	bra	ps_4		;   then
	btfsc	INDF1, 0	;     if (do_sound[sound_number] & 0x1 == 0)   
	return			;	then return
ps_4	movlw	.1
	movwf	do_sound+0x16	;   play sound 16
	return
	
ps_5
			; sound 15 : played once and followed by 13
	movlw	0x14		; if (sound_number == 0x14)
	cpfseq	sound_number	; then
	bra	ps_6
	clrf	INDF1		;    do_sound[sound_number] = 0
	movlw	.1
	movwf	do_sound+0x13	;    play sound 13
	return
			; all other sounds
ps_6
	clrf	INDF1		; default : do_sound[sound_number] = 0
	return

ps_end	incf	offset, f
	incf	voice_number, f
	bra	ps_1

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; STOP ALL GALAGA SOUNDS
;
; - Clears scheduler flags (do_sound / in_sound)
; - Clears per-sound state (states / positions)
; - Clears special looping counters (sound05/sound06)
; - Clears current voice params (vol/freq/wavesels) so copy_to_hw will go silent
; - Mutes hardware voices and forces silence via fix_voices
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
galaga_stop_all
    GLOBAL  galaga_stop_all

    movlb   .5              ; all these vars live in bank 5

    ; ---- clear do_sound[23] ----
    lfsr    FSR1, do_sound
    movlw   .23
    movwf   temp0
gs_do_loop
    clrf    POSTINC1
    decfsz  temp0, f
    bra     gs_do_loop

    ; ---- clear in_sound[23] ----
    lfsr    FSR1, in_sound
    movlw   .23
    movwf   temp0
gs_in_loop
    clrf    POSTINC1
    decfsz  temp0, f
    bra     gs_in_loop

    ; ---- clear states[43] ----
    lfsr    FSR1, states
    movlw   .43
    movwf   temp0
gs_states_loop
    clrf    POSTINC1
    decfsz  temp0, f
    bra     gs_states_loop

    ; ---- clear positions[43] ----
    lfsr    FSR1, positions
    movlw   .43
    movwf   temp0
gs_pos_loop
    clrf    POSTINC1
    decfsz  temp0, f
    bra     gs_pos_loop

    ; ---- clear special looping helpers ----
    clrf    sound05_count
    clrf    sound05_vol
    clrf    sound06_count
    clrf    sound06_wavesel

    ; ---- clear current computed voice params (what copy_to_hw uses) ----
    clrf    vol
    clrf    vol+1
    clrf    vol+2

    clrf    freq
    clrf    freq+1
    clrf    freq+2
    clrf    freq+3
    clrf    freq+4
    clrf    freq+5

    clrf    wavesels
    clrf    wavesels+1
    clrf    wavesels+2

    ; ---- mute hardware voices immediately ----
    ; these are externs from sound.asm
    clrf    v0_hVol
    clrf    v1_hVol
    clrf    v2_hVol
    call    fix_voices

    return


;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; start sound
;
start_sound
	lfsr	FSR1, in_sound
	movf	sound_number, w
	addwf	FSR1L, f
	incf	INDF1, f	; in_sound[sound_number] = 1

	call	get_data
	call	init_sound

	bra	continue_sound_2
	
	

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; continue sound
;
continue_sound
	call	get_data

continue_sound_2
	call	process_chunk
	decfsz	num_voices
	bra	cs_2
	tstfsz	chunk_done
	bra	cs_1
	return
cs_1	clrf	chunk_done
	lfsr	FSR1, in_sound
	movf	sound_number, w
	addwf	FSR1L, f
	clrf	INDF1		; in_sound[sound_number] = 0
	return
cs_2	incf	offset, f
	incf	voice_number, f
	bra	continue_sound_2


;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; play sound bis
;
play_sound_bis
	call	get_data
	call	check_in_sound
	bra	continue_sound_2


;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; check_in_sound
;
check_in_sound
	lfsr	FSR1, in_sound
	movf	sound_number, w
	addwf	FSR1L, f
	tstfsz	INDF1		; if (in_sound[sound_number] != 0)
	return			; then return

	incf	INDF1, f	; in_sound[sound_number] = 1
init_sound
	lfsr	FSR1, states
	movf	offset, w
	addwf	FSR1L, f
	movf	num_voices, w
cis_1	clrf	POSTINC1	; init states array
	decfsz	WREG
	bra	cis_1
	lfsr	FSR1, positions	; init positions array
	movf	offset, w
	addwf	FSR1L, f
	movf	num_voices, w
cis_2	clrf	POSTINC1
	decfsz	WREG
	bra	cis_2

	return
	

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; get data : get offset, num_voices and voice_number from ROM
;
get_data
	movlw	high(table3)
	movwf	TBLPTRH
	movlw	low(table3)
	movwf	TBLPTRL
	movf	sound_number, w
	addwf	WREG, w
	addwf	sound_number, w	; W = 3 * sound_number
	addwf	TBLPTRL, f
	btfsc	STATUS, C
	incf	TBLPTRH, f	; TBLPTR = table3[3 * sound_number]
	tblrd*+
	movf	TABLAT, w
	movwf	offset
	tblrd*+
	movf	TABLAT, w
	movwf	num_voices
	tblrd*+
	movf	TABLAT, w
	movwf	voice_number

	movlw	0xE
	cpfseq	sound_number	; sound_number E is special (echo effect)
	return
	
	movlw	.0
	cpfseq	positions+0x1C	; if (9A4C == 0)
	bra	gd_1
	movlw	.1		; then
	movwf	num_voices	;	num_voices = 1
	return
gd_1	movlw	.1
	cpfseq	positions+0x1C	; if (9A4C == 1)
	bra	gd_3
gd_2	movlw	.2		; then
	movwf	num_voices	;	num_voices = 2
	return
gd_3	tstfsz	positions+0x1D	; if (9A4D == 0)
	return
	bra	gd_2		; then num_voices = 2
	
	

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; process one chunk of data
;
process_chunk
	lfsr	FSR1, states
	movf	offset, w
	addwf	FSR1L, f
	incf	INDF1, f	; states[offset]++
	
				; read data0, data1 and wavesel
	movlw	high(table4)
	movwf	TBLPTRH
	movlw	low(table4)
	movwf	TBLPTRL		; TBLPTR = table4
	rlncf	offset, w	; W = 2 * offset
	addwf	TBLPTRL, f
	btfsc	STATUS, C
	incf	TBLPTRH, f	; TBLPTR = table4 + 2 * offset

	tblrd*+
	movf	TABLAT, w
	movwf	temp1
	tblrd*+
	movf	TABLAT, w
	movwf	temp0		; temp[01] = address of WAVExx

	movf	temp0, w
	movwf	TBLPTRH
	movf	temp1, w
	movwf	TBLPTRL		; TBLPTR = temp[01]
	tblrd*+
	movf	TABLAT, w
	movwf	data0		; read data0
	tblrd*+
	movf	TABLAT, w
	movwf	data1		; read data1
	tblrd*+
	movf	TABLAT, w
	movwf	wavesel		; read wavesel

	lfsr	FSR1, positions
	movf	offset, w
	addwf	FSR1L, f
	movf	INDF1, w	; W = positions[offset]
	addwf	TBLPTRL, f
	btfsc	STATUS, C
	incf	TBLPTRH, f	; TBLPTR += positions[offset]

	tblrd*+
	movf	TABLAT, w	; read one byte from ROM
	movwf	data_byte	; and save it.
	tblrd*+
	movf	TABLAT, w
	movwf	data_next	; and get the next byte
	
	movf	data_byte, w
	incfsz	WREG, w		; check if we got 0xFF
	bra	chunk_1		; no. goto chunk_1

	lfsr	FSR1, vol
	movf	voice_number, w
	addwf	FSR1L, f
	clrf	INDF1		; vol[voice_number] = 0
	movlw	.1
	movwf	chunk_done	; chunk_done = 1
	return

chunk_1
			;; frequency
	movlw	high(table0)
	movwf	TBLPTRH
	movlw	low(table0)
	movwf	TBLPTRL
	movf	data_byte, w
	andlw	0x0F
	rlncf	WREG, w
	addwf	TBLPTRL, f
	btfsc	STATUS, C
	incf	TBLPTRH, f	; TBLPTR = table0 + 2 * (data & 0xF)
	tblrd*+
	movf	TABLAT, w
	movwf	bfreq
	tblrd*+
	movf	TABLAT, w
	movwf	bfreq+1		; read frequency from TBLPTR

	swapf	data_byte, w
	andlw	0x0F		; W = data >> 4
	btfsc	STATUS, Z
	bra	chunk_3		; if (W == 0) goto chunk_3
	movwf	temp0
chunk_2				; else freq = freq >> W
	bcf	STATUS, C
	rrcf	bfreq+1, f
	rrcf	bfreq, f
	decfsz	temp0
	bra	chunk_2
chunk_3
	lfsr	FSR1, freq
	movf	voice_number, w
	rlncf	WREG, w
	addwf	FSR1L, f
	movf	bfreq, w
	movwf	POSTINC1
	movf	bfreq+1, w
	movwf	POSTINC1	; freq[voice_number] = bfreq

			;; volume
	movlw	0x0C
	subwf	data_byte, w	; W = data - 0xC
	btfsc	STATUS, Z
	bra	vol_zero	; W == 0xC, goto vol_zero

	movf	data0, w
	andwf	WREG, w
	btfsc	STATUS, Z
	bra	data0_zero	; data0 == 0
	decf	WREG, w
	btfsc	STATUS, Z
	bra	data0_one	; data0 == 1
data0_gt_one
	lfsr	FSR1, states
	movf	offset, w
	addwf	FSR1L, f	; FSR1 = states[offset]
	movlw	.6
	cpfslt	INDF1
	bra	data0_zero	; states[offset] > 6
	comf	INDF1, w
	bra	store_vol
data0_one
	lfsr	FSR1, states
	movf	offset, w
	addwf	FSR1L, f	; FSR1 = states[offset]
	movlw	.6
	cpfslt	INDF1
	bra	data0_zero	; states[offset] > 6
	movf	INDF1, w
	addwf	WREG, w
	bra	store_vol
data0_zero
	movf	data1, w
	andwf	WREG, w
	btfsc	STATUS, Z
	bra	data1_zero	; data1 == 0
	lfsr	FSR1, states
	movf	offset, w
	addwf	FSR1L, f	; FSR1 = states[offset]
	movlw	0xA
	cpfsgt	INDF1
	bra	data1_zero	; if (INDF1 < 0xB) vol = 0xA
	movlw	0x15
	cpfslt	INDF1
	bra	vol_zero	; if (INDF1 >= (0xB+0xA)) vol = 0 
	movf	INDF1, w
	sublw	.0
	addlw	0x15
	bra	store_vol
data1_zero
	movlw	0x0A
	bra	store_vol
vol_zero
	movlw	.0
	bra	store_vol
store_vol
	movwf	temp0		; save vol
	lfsr	FSR1, vol
	movf	voice_number, w
	addwf	FSR1L, f	; FSR1 = vol[voice_number]
	movf	temp0, w	; restore vol
	movwf	INDF1		; and save it

	;; wavesel
	lfsr	FSR1, wavesels
	movf	voice_number, w
	addwf	FSR1L, f	; FSR1 = wavesels[voice_number]
	movf	wavesel, w
	movwf	INDF1

	;; adjust tables
	movlw	high(table5)
	movwf	TBLPTRH
	movlw	low(table5)
	movwf	TBLPTRL
	movf	sound_number, w
	addwf	TBLPTRL, f
	btfsc	STATUS, C
	incf	TBLPTRH, f	; TBLPTR = table5[sound_number]
	tblrd*
	movf	TABLAT, w
	movwf	temp1		; temp1 = table5[sound_number]

	movf	data_next, w
	movwf	data16
	clrf	data16+1	; data16 = data_next on 16 bits
	clrf	acc16	
	clrf	acc16+1		; acc16 = 0
	movlw	.8
	movwf	temp0
chunk_4
	rrcf	temp1, f	; if (temp1 & 0x1)
	btfss	STATUS, C
	bra	chunk_5
	movf	data16, w	; then
	addwf	acc16, f
	movf	data16+1, w
	addwfc	acc16+1, f	;	acc16 += data16
chunk_5
	bcf	STATUS, C
	rlcf	data16, f
	rlcf	data16+1, f	; data16 = data16 << 1
	decfsz	temp0
	bra	chunk_4

	lfsr	FSR1, states
	movf	offset, w
	addwf	FSR1L, f	; FSR1 = states[offset]
	movf	INDF1, w
	cpfseq	acc16
	return
	clrf	INDF1		; states[offset] = 0
	lfsr	FSR1, positions
	movf	offset, w
	addwf	FSR1L, f	; FSR1 = positions[offset]
	incf	INDF1, f
	incf	INDF1, f	; positions[offset] += 2
	return
	
	
	
	
galaga_data	CODE_PACK

galaga_wavetable_data
        db      0x70, 0x70, 0xe0, 0xb0, 0x70, 0x70, 0x70, 0xa0
        db      0x90, 0x90, 0xe0, 0xd0, 0xa0, 0xe0, 0x80, 0xc0
        db      0xa0, 0xa0, 0xe0, 0xe0, 0xc0, 0xc0, 0xa0, 0xc0
        db      0xb0, 0xb0, 0xe0, 0xd0, 0xd0, 0x90, 0xc0, 0xa0
        db      0xc0, 0x70, 0xe0, 0xc0, 0xe0, 0xc0, 0xe0, 0x70
        db      0xd0, 0xd0, 0xe0, 0xa0, 0xd0, 0xe0, 0xd0, 0x70
        db      0xd0, 0xd0, 0xe0, 0x80, 0xc0, 0xa0, 0xc0, 0x80
        db      0xe0, 0x70, 0xe0, 0x80, 0xa0, 0x70, 0xc0, 0xb0
        db      0xe0, 0xe0, 0xe0, 0x80, 0x70, 0xc0, 0xb0, 0xd0
        db      0xe0, 0x70, 0xe0, 0xa0, 0x40, 0xf0, 0xa0, 0xe0
        db      0xd0, 0xd0, 0xe0, 0xc0, 0x20, 0xd0, 0x80, 0xd0
        db      0xd0, 0xd0, 0xe0, 0xd0, 0x10, 0x80, 0x70, 0xa0
        db      0xc0, 0x70, 0xe0, 0xe0, 0x00, 0xa0, 0x50, 0x60
        db      0xb0, 0xb0, 0xe0, 0xd0, 0x10, 0xb0, 0x60, 0x50
        db      0xa0, 0xa0, 0xe0, 0xb0, 0x20, 0x70, 0x70, 0x50
        db      0x90, 0x90, 0xe0, 0x80, 0x40, 0x20, 0x80, 0x70
        db      0x70, 0x70, 0x00, 0x40, 0x70, 0x80, 0x80, 0x90
        db      0x50, 0x50, 0x00, 0x20, 0xb0, 0xd0, 0x90, 0x90
        db      0x40, 0x70, 0x00, 0x10, 0xd0, 0x90, 0xa0, 0x80
        db      0x30, 0x30, 0x00, 0x20, 0xe0, 0x40, 0xb0, 0x40
        db      0x20, 0x70, 0x00, 0x30, 0xd0, 0x50, 0x90, 0x10
        db      0x10, 0x10, 0x00, 0x50, 0xb0, 0x70, 0x80, 0x00
        db      0x10, 0x70, 0x00, 0x70, 0x70, 0x20, 0x60, 0x10
        db      0x00, 0x00, 0x00, 0x70, 0x30, 0x00, 0x50, 0x30
        db      0x00, 0x70, 0x00, 0x70, 0x10, 0x30, 0x40, 0x60
        db      0x00, 0x00, 0x00, 0x50, 0x00, 0x80, 0x40, 0x70
        db      0x10, 0x70, 0x00, 0x30, 0x10, 0x50, 0x30, 0x70
        db      0x10, 0x10, 0x00, 0x20, 0x30, 0x10, 0x20, 0x40
        db      0x20, 0x70, 0x00, 0x10, 0x70, 0x30, 0x40, 0x20
        db      0x30, 0x30, 0x00, 0x20, 0xe0, 0x60, 0x60, 0x20
        db      0x40, 0x70, 0x00, 0x40, 0x70, 0x30, 0x80, 0x40
        db      0x50, 0x50, 0x00, 0x70, 0x00, 0x10, 0x90, 0x70


table0
	dw 8150h
	dw 8900h
	dw 9126h
	dw 99C8h
	dw 0A2ECh
	dw 0AC9Dh
	dw 0B6E0h
	dw 0C1C0h
	dw 0CD45h
	dw 0D97Ah
	dw 0E669h
	dw 0F41Ch
	dw 0

table1
	dw 130h	
	dw 168h
	dw 136h
	dw 1A8h
	dw 168h
	dw 200h
	dw 1ACh
	dw 208h

table2
	dw 0FE00h
	dw 0FE58h
	dw 0FE08h
	dw 0FE98h
	dw 0FE58h
	dw 0FED0h
	dw 0FE98h
	dw 0FED6h
	dw 5B00h
	dw 6C00h
	dw 5B00h
	dw 7E00h
	dw 6C00h
	dw 9700h
	dw 8100h
	dw 9900h
	dw 0D900h
	dw 0B600h
	dw 0D900h
	dw 9700h
	dw 0B600h
	dw 7E00h
	dw 9900h
	dw 8100h

table3
	db 0, 1, 0	; 0
	db 1, 1, 1	; 1
	db 2, 1, 1	; 2
	db 3, 1, 1	; 3
	db 4, 1, 1	; 4
	db 5, 1, 0	; 5
	db 6, 1, 0	; 6
	db 20h,	3, 0	; 7
	db 0Ah,	3, 0	; 8
	db 0Dh,	3, 0	; 9
	db 7, 3, 0	; A
	db 13h,	3, 0	; B
	db 16h,	3, 0	; C
	db 19h,	3, 0	; D
	db 1Ch,	3, 0	; E
	db 1Fh,	1, 2	; F
	db 2Ch,	3, 0	; 10
	db 10h,	3, 0	; 11
	db 23h,	1, 0	; 12
	db 24h,	1, 0	; 13
	db 25h,	3, 0	; 14
	db 28h,	1, 0	; 15
	db 29h,	3, 0	; 16

table4
	dw WAVE01
	dw WAVE02
	dw WAVE03
	dw WAVE04
	dw WAVE05
	dw WAVE06
	dw WAVE07
	dw WAVE08
	dw WAVE09
	dw WAVE10
	dw WAVE11
	dw WAVE12
	dw WAVE13
	dw WAVE14
	dw WAVE15
	dw WAVE16
	dw WAVE17
	dw WAVE18
	dw WAVE19
	dw WAVE20
	dw WAVE21
	dw WAVE22
	dw WAVE23
	dw WAVE24
	dw WAVE25
	dw WAVE26
	dw WAVE27
	dw wAVE28
	dw WAVE20
	dw WAVE20
	dw WAVE20
	dw WAVE29
	dw WAVE30
	dw WAVE31
	dw WAVE32
	dw WAVE33
	dw WAVE29
	dw WAVE34
	dw WAVE35
	dw WAVE36
	dw WAVE37
	dw WAVE38
	dw WAVE39
	dw WAVE40
	dw WAVE41
	dw WAVE42
	dw WAVE43
	
table5
	db 4, 2, 2, 2, 2, 4, 4,	0Ah, 7,	0Ch, 0Bh, 4, 0Ah, 0Dh, 4, 1, 4
	db 0Ch,	2, 6, 5, 2, 0Ah
	
WAVE01
	db 0FFh
WAVE04
	db 0, 0, 6, 71h, 1, 72h, 1, 73h, 1, 75h, 1, 74h, 1, 73h, 1, 72h
	db 1, 71h, 1, 70h, 1, 8Bh, 1, 8Ah, 1, 0Ch, 4, 86h, 1, 87h, 1, 88h
	db 1, 89h, 1, 8Ah, 1, 89h, 1, 88h, 1, 87h, 1, 86h, 1, 85h, 1, 84h
	db 1, 83h, 1, 0FFh
WAVE03
	db 0, 0, 4, 88h, 1, 8Ah, 1, 70h, 1, 71h, 1, 73h, 1, 75h, 1, 77h
	db 1, 78h, 1, 0Ch, 6, 74h, 1, 73h, 1, 72h, 1, 71h, 1, 70h, 1, 8Bh
	db 1, 0FFh
WAVE02
	db 0, 0, 7, 89h, 1, 8Ah, 1, 8Bh, 1, 0Ch, 1, 70h, 1, 71h, 1, 72h
	db 1, 0Ch, 1, 73h, 1, 74h, 1, 75h, 1, 0Ch, 3, 8Bh, 1, 70h, 1, 71h
	db 1, 0Ch, 1, 72h, 1, 73h, 1, 74h, 1, 0Ch, 1, 75h, 1, 76h, 1, 77h
	db 1, 0Ch, 3, 71h, 1, 72h, 1, 73h, 1, 0Ch, 1, 74h, 1, 75h, 1, 76h
	db 1, 0Ch, 1, 77h, 1, 78h, 1, 79h, 1, 0FFh
WAVE05
	db 0, 0, 5, 71h, 1, 72h, 1, 73h, 1, 0Ch, 1, 74h, 1, 75h, 1, 76h
	db 1, 0Ch, 1, 77h, 1, 78h, 1, 79h, 1, 0FFh
WAVE06
	db 0, 0, 4, 61h, 1, 7Ah, 1, 60h, 1, 78h, 1, 7Ah, 1, 76h, 1, 78h
	db 1, 75h, 1, 0FFh
WAVE07
	db 0, 0, 0, 76h, 1, 79h, 1, 60h, 1, 63h, 1, 66h, 1, 63h, 1, 60h
	db 1, 79h, 1, 0FFh
WAVE20
	db 0, 0, 7, 81h, 8, 81h, 1, 86h, 3, 88h, 9, 8Bh, 3, 8Ah, 9, 86h
	db 3, 88h, 9, 73h, 3, 71h, 9, 86h, 3, 88h, 9, 8Bh, 3, 8Ah, 9, 86h
	db 3, 71h, 9, 75h, 3, 76h, 9, 74h, 3, 72h, 9, 71h, 3, 8Bh, 9, 89h
	db 3, 88h, 9, 84h, 3, 74h, 9, 76h, 3, 74h, 9, 71h, 3, 73h, 4, 8Bh
	db 4, 88h, 4, 71h, 4, 8Ah, 4, 88h, 4, 0Ch, 10h,	0FFh
WAVE21
	db 0, 0, 6, 8Ah, 9, 81h, 3, 88h, 9, 83h, 3, 86h, 9, 81h, 3, 83h
	db 9, 85h, 3, 8Ah, 9, 81h, 3, 88h, 9, 83h, 3, 86h, 9, 81h, 3, 88h
	db 9, 71h, 3, 72h, 9, 71h, 3, 8Bh, 9, 89h, 3, 88h, 9, 86h, 3, 84h
	db 9, 88h, 3, 89h, 9, 8Bh, 3, 89h, 9, 86h, 3, 8Bh, 4, 88h, 4, 83h
	db 4, 88h, 4, 85h, 4, 83h, 4, 0Ch, 10h,	0FFh
WAVE22
	db 0, 0, 7, 81h, 0Ch, 83h, 9, 86h, 3, 85h, 0Ch,	81h, 0Ch, 86h
	db 0Ch,	88h, 9,	8Bh, 3,	8Ah, 0Ch, 88h, 0Ch, 89h, 0Ch, 88h, 9, 86h
	db 3, 84h, 0Ch,	89h, 0Ch, 74h, 0Ch, 71h, 9, 89h, 3, 88h, 0Ch, 71h
	db 9, 8Ah, 3, 0Ch, 10h,	0FFh
WAVE26
	db 2, 0, 3, 78h, 2, 0Ch, 1, 78h, 1, 79h, 1, 7Bh, 1, 61h, 3, 0Ch
	db 3, 0FFh
WAVE27
	db 2, 0, 3, 73h, 2, 0Ch, 1, 73h, 1, 74h, 1, 76h, 1, 78h, 3, 0Ch
	db 2, 0FFh
wAVE28
	db 2, 0, 3, 70h, 2, 0Ch, 1, 70h, 1, 71h, 1, 73h, 1, 75h, 3, 0Ch
	db 2, 0FFh
WAVE11
	db 1, 0, 4, 78h, 1, 7Ah, 1, 63h, 1, 78h, 1, 7Ah, 1, 63h, 1, 65h
	db 3, 0FFh
WAVE12
	db 1, 0, 5, 73h, 1, 78h, 1, 7Ah, 1, 73h, 1, 78h, 1, 7Ah, 1, 60h
	db 3, 0FFh
WAVE13
	db 1, 0, 7, 8Ah, 1, 73h, 1, 78h, 1, 8Ah, 1, 73h, 1, 78h, 1, 7Ah
	db 3, 0FFh
WAVE08
	db 1, 6, 4, 7Ah, 1, 78h, 1, 7Ah, 1, 61h, 1, 65h, 1, 68h, 3, 0FFh
WAVE09
	db 1, 6, 4, 78h, 1, 75h, 1, 78h, 1, 7Ah, 1, 61h, 1, 65h, 3, 0FFh
WAVE10
	db 1, 6, 4, 75h, 1, 71h, 1, 75h, 1, 78h, 1, 7Ah, 1, 60h, 3, 0FFh
WAVE14
	db 2, 4, 3, 7Ah, 1, 76h, 1, 78h, 1, 75h, 1, 76h, 1, 73h, 1, 75h
	db 1, 72h, 1, 73h, 1, 8Ah, 1, 8Bh, 1, 88h, 1, 86h, 1, 85h, 1, 83h
	db 1, 82h, 1, 83h, 1, 86h, 1, 85h, 1, 88h, 1, 86h, 1, 8Ah, 1, 88h
	db 1, 8Bh, 1, 8Ah, 1, 73h, 1, 72h, 1, 73h, 1, 75h, 1, 8Ah, 1, 70h
	db 1, 72h, 1, 0FFh
WAVE15
	db 2, 4, 3, 76h, 1, 73h, 1, 75h, 1, 72h, 1, 73h, 1, 70h, 1, 72h
	db 1, 8Ah, 1, 8Bh, 1, 86h, 1, 88h, 1, 85h, 1, 83h, 1, 82h, 1, 80h
	db 1, 9Ah, 1, 9Ah, 1, 83h, 1, 82h, 1, 85h, 1, 83h, 1, 86h, 1, 85h
	db 1, 88h, 1, 86h, 1, 8Ah, 1, 88h, 1, 8Bh, 1, 8Ah, 1, 88h, 1, 86h
	db 1, 85h, 1, 0FFh
WAVE16
	db 2, 10h, 3, 93h, 2, 9Ah, 2, 83h, 3, 9Ah, 1, 98h, 1, 96h, 1, 95h
	db 1, 93h, 2, 95h, 3, 96h, 2, 98h, 2, 9Ah, 2, 9Bh, 2, 9Ah, 2, 98h
	db 1, 96h, 1, 95h, 1, 92h, 1, 93h, 1, 95h, 1, 0FFh
WAVE17
	db 2, 4, 3, 7Ah, 1, 77h, 1, 78h, 1, 75h, 1, 77h, 1, 73h, 1, 75h
	db 1, 72h, 1, 73h, 1, 8Ah, 1, 80h, 1, 88h, 1, 87h, 1, 85h, 1, 83h
	db 1, 82h, 1, 83h, 1, 87h, 1, 85h, 1, 88h, 1, 87h, 1, 8Ah, 1, 88h
	db 1, 80h, 1, 8Ah, 1, 73h, 1, 72h, 1, 73h, 1, 75h, 1, 8Ah, 1, 70h
	db 1, 72h, 1, 0FFh
WAVE18
	db 2, 4, 3, 77h, 1, 73h, 1, 75h, 1, 72h, 1, 73h, 1, 70h, 1, 72h
	db 1, 8Ah, 1, 80h, 1, 87h, 1, 88h, 1, 85h, 1, 83h, 1, 82h, 1, 80h
	db 1, 9Ah, 1, 9Ah, 1, 83h, 1, 82h, 1, 85h, 1, 83h, 1, 87h, 1, 85h
	db 1, 88h, 1, 87h, 1, 8Ah, 1, 88h, 1, 80h, 1, 8Ah, 1, 88h, 1, 87h
	db 1, 85h, 1, 0FFh
WAVE19
	db 2, 10h, 3, 93h, 2, 9Ah, 2, 83h, 3, 9Ah, 1, 98h, 1, 97h, 1, 95h
	db 1, 93h, 2, 95h, 3, 97h, 2, 98h, 2, 9Ah, 2, 90h, 2, 9Ah, 2, 98h
	db 1, 97h, 1, 95h, 1, 92h, 1, 93h, 1, 95h, 1, 0FFh
WAVE30
	db 2, 4, 3, 7Ah, 1, 76h, 1, 78h, 1, 75h, 1, 76h, 1, 73h, 1, 75h
	db 1, 72h, 1, 73h, 1, 8Ah, 1, 8Ah, 1, 88h, 1, 86h, 1, 85h, 1, 83h
	db 1, 82h, 1, 83h, 1, 85h, 1, 86h, 1, 88h, 1, 86h, 1, 8Ah, 1, 70h
	db 1, 72h, 1, 73h, 4, 0FFh
WAVE31
	db 2, 4, 3, 76h, 1, 73h, 1, 75h, 1, 72h, 1, 73h, 1, 70h, 1, 72h
	db 1, 8Ah, 1, 8Ah, 1, 86h, 1, 86h, 1, 85h, 1, 83h, 1, 82h, 1, 80h
	db 1, 9Ah, 1, 9Ah, 1, 8Bh, 1, 80h, 1, 82h, 1, 83h, 1, 85h, 1, 86h
	db 1, 88h, 1, 8Ah, 4, 0FFh
WAVE32
	db 2, 10h, 3, 73h, 2, 75h, 2, 76h, 2, 75h, 2, 73h, 2, 72h, 2, 70h
	db 2, 72h, 2, 73h, 2, 8Bh, 2, 8Ah, 2, 86h, 2, 83h, 4, 0FFh
WAVE33
	db 0, 0, 4, 71h, 4, 73h, 4, 71h, 4, 73h, 4, 76h, 4, 78h, 4, 76h
	db 4, 78h, 4, 0FFh
WAVE29
	db 0, 0, 6, 56h, 1, 55h, 1, 54h, 1, 53h, 1, 52h, 1, 51h, 1, 50h
	db 1, 6Bh, 1, 6Ah, 1, 69h, 1, 68h, 1, 67h, 1, 66h, 1, 65h, 1, 64h
	db 1, 63h, 1, 62h, 1, 61h, 1, 60h, 1, 7Bh, 1, 7Ah, 1, 79h, 1, 78h
	db 1, 77h, 1, 76h, 1, 75h, 1, 74h, 1, 73h, 1, 72h, 1, 71h, 1, 70h
	db 1, 8Bh, 1, 8Ah, 1, 89h, 1, 88h, 1, 87h, 1, 86h, 1, 85h, 1, 84h
	db 1, 83h, 1, 0FFh
WAVE23
	db 2, 4, 5, 60h, 1, 78h, 1, 75h, 1, 71h, 1, 60h, 1, 78h, 1, 75h
	db 1, 71h, 1, 60h, 1, 78h, 1, 75h, 1, 71h, 1, 60h, 1, 78h, 1, 75h
	db 1, 71h, 1, 60h, 1, 78h, 1, 75h, 1, 71h, 1, 60h, 1, 78h, 1, 75h
	db 1, 71h, 1, 60h, 1, 0Ch, 1, 78h, 1, 7Ah, 1, 75h, 1, 78h, 1, 73h
	db 1, 75h, 1, 61h, 1, 7Ah, 1, 76h, 1, 73h, 1, 61h, 1, 7Ah, 1, 76h
	db 1, 73h, 1, 61h, 1, 7Ah, 1, 76h, 1, 73h, 1, 61h, 1, 7Ah, 1, 76h
	db 1, 73h, 1, 61h, 1, 79h, 1, 76h, 1, 73h, 1, 61h, 1, 79h, 1, 76h
	db 1, 73h, 1, 61h, 1, 0Ch, 1, 79h, 1, 61h, 1, 78h, 1, 79h, 1, 75h
	db 1, 78h, 1, 0FFh
WAVE38
	db 2, 2, 5, 60h, 1, 60h, 1, 60h, 1, 0FFh
WAVE24
	db 2, 4, 5, 61h, 2, 78h, 2, 78h, 2, 61h, 2, 78h, 2, 78h, 2, 61h
	db 2, 78h, 2, 78h, 2, 61h, 2, 78h, 2, 78h, 2, 61h, 2, 78h, 2, 7Ah
	db 2, 75h, 2, 63h, 2, 7Ah, 2, 7Ah, 2, 63h, 2, 7Ah, 2, 7Ah, 2, 63h
	db 2, 7Ah, 2, 79h, 2, 63h, 2, 79h, 2, 79h, 2, 63h, 2, 79h, 2, 76h
	db 2, 73h, 2, 0FFh
WAVE39
	db 2, 2, 5, 78h, 1, 78h, 1, 78h, 1, 0FFh
WAVE25
	db 2, 10h, 5, 85h, 6, 85h, 6, 85h, 6, 85h, 6, 85h, 4, 85h, 4, 86h
	db 6, 86h, 6, 86h, 6, 86h, 6, 86h, 4, 86h, 4, 0FFh
WAVE40
	db 2, 4, 5, 81h, 1, 81h, 1, 81h, 1, 0FFh
WAVE37
	db 2, 0, 7, 65h, 1, 0Ch, 1, 61h, 1, 0Ch, 1, 63h, 1, 0FFh
WAVE34
	db 2, 0, 5, 7Ah, 5, 0Ch, 1, 7Ah, 1, 0Ch, 1, 7Ah, 3, 0Ch, 1, 78h
	db 7, 0Ch, 1, 78h, 7, 0Ch, 1, 78h, 3, 0Ch, 1, 7Bh, 5, 0Ch, 1, 7Bh
	db 1, 0Ch, 1, 7Bh, 3, 0Ch, 1, 7Ah, 7, 0Ch, 1, 7Ah, 7, 0Ch, 1, 7Ah
	db 3, 0Ch, 1, 7Bh, 1, 0Ch, 1, 7Bh, 1, 0Ch, 3, 7Bh, 1, 0Ch, 1, 7Bh
	db 3, 0Ch, 1, 61h, 1, 0Ch, 1, 61h, 1, 0Ch, 3, 61h, 1, 0Ch, 1, 61h
	db 3, 0Ch, 1, 61h, 3, 0Ch, 1, 61h, 3, 0Ch, 1, 63h, 1, 0Ch, 1, 63h
	db 1, 0Ch, 3, 63h, 1, 0Ch, 1, 63h, 3, 0Ch, 1, 63h, 3, 0Ch, 1, 63h
	db 3, 0Ch, 1, 0FFh
WAVE35
	db 2, 0, 3, 86h, 2, 8Ah, 2, 71h, 2, 76h, 2, 86h, 2, 8Ah, 2, 71h
	db 2, 76h, 2, 86h, 2, 8Ah, 2, 71h, 2, 76h, 2, 86h, 2, 8Ah, 2, 71h
	db 2, 76h, 2, 86h, 2, 8Ah, 2, 71h, 2, 76h, 2, 86h, 2, 8Ah, 2, 71h
	db 2, 76h, 2, 86h, 2, 8Ah, 2, 71h, 2, 76h, 2, 86h, 2, 8Ah, 2, 71h
	db 2, 76h, 2, 77h, 1, 0Ch, 1, 77h, 1, 0Ch, 3, 77h, 1, 0Ch, 1, 77h
	db 3, 0Ch, 1, 69h, 1, 0Ch, 1, 69h, 1, 0Ch, 3, 69h, 1, 0Ch, 1, 69h
	db 3, 0Ch, 1, 69h, 3, 0Ch, 1, 69h, 3, 0Ch, 1, 8Bh, 2, 73h, 2, 76h
	db 2, 7Bh, 2, 7Bh, 2, 76h, 2, 73h, 2, 8Bh, 2, 0FFh
WAVE36
	db 0, 0, 2, 86h, 8, 81h, 8, 86h, 8, 81h, 8, 86h, 8, 81h, 8, 86h
	db 8, 81h, 8, 82h, 1, 0Ch, 1, 82h, 1, 0Ch, 3, 82h, 1, 0Ch, 1, 82h
	db 3, 0Ch, 1, 84h, 1, 0Ch, 1, 84h, 1, 0Ch, 3, 84h, 1, 0Ch, 1, 84h
	db 3, 0Ch, 1, 84h, 3, 0Ch, 1, 84h, 3, 0Ch, 1, 7Bh, 8, 76h, 4, 8Bh
	db 4, 0FFh
WAVE41
	db 0, 0Ch, 5, 75h, 0Ch,	71h, 0Ch, 8Ah, 0Ch, 86h, 0Ch, 0Ch, 9, 75h
	db 3, 71h, 9, 8Ah, 3, 86h, 4, 8Ah, 4, 71h, 4, 89h, 4, 70h, 4, 73h
	db 4, 8Bh, 0Ch,	73h, 0Ch, 76h, 0Ch, 78h, 0Ch, 0Ch, 9, 79h, 3, 76h
	db 9, 72h, 3, 8Bh, 4, 89h, 4, 86h, 4, 72h, 4, 89h, 4, 76h, 4, 0FFh
WAVE42
	db 0, 0Ch, 5, 71h, 0Ch,	8Ah, 0Ch, 86h, 0Ch, 85h, 0Ch, 0Ch, 9, 81h
	db 3, 8Ah, 9, 86h, 3, 85h, 4, 86h, 4, 8Ah, 4, 86h, 4, 89h, 4, 8Bh
	db 4, 88h, 0Ch,	8Bh, 0Ch, 73h, 0Ch, 76h, 0Ch, 0Ch, 9, 76h, 3, 72h
	db 9, 8Bh, 3, 8Ah, 4, 86h, 4, 82h, 4, 8Bh, 4, 89h, 4, 82h, 4, 0FFh
WAVE43
	db 0, 0, 3, 75h, 18h, 75h, 18h,	75h, 18h, 71h, 0Ch, 75h, 0Ch, 73h
	db 18h,	73h, 18h, 72h, 18h, 76h, 0Ch, 78h, 0Ch,	0FFh

	db 0FAh, 0FFh

        
        END
        