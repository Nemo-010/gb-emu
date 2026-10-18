; A small Game Boy demo written for this repository.
;
; It exists so the player has something to run that is unambiguously free of
; anyone else's rights: it draws a title, a bouncing block, and responds to the
; d-pad. Written in RGBDS assembly.

INCLUDE "hardware.inc"

SECTION "Header", ROM0[$100]
	jp EntryPoint

	ds $150 - @, 0

SECTION "Entry", ROM0[$150]
EntryPoint:
	di
	ld sp, $fffe

.wait_vblank:
	ldh a, [rLY]
	cp 144
	jr nz, .wait_vblank

	xor a
	ldh [rLCDC], a

	; Clear video RAM.
	ld hl, $8000
	ld bc, $2000
.clear_vram:
	xor a
	ld [hl+], a
	dec bc
	ld a, b
	or c
	jr nz, .clear_vram

	; Copy the tiles into video RAM.
	ld de, Tiles
	ld hl, $8000
	ld bc, TilesEnd - Tiles
.copy_tiles:
	ld a, [de]
	inc de
	ld [hl+], a
	dec bc
	ld a, b
	or c
	jr nz, .copy_tiles

	; A mostly blank background with a border row, so the title is legible.
	ld hl, $9800
	ld bc, 32 * 32
.fill_map:
	ld a, 2          ; tile 2 is blank
	ld [hl+], a
	dec bc
	ld a, b
	or c
	jr nz, .fill_map

	; A border tile across the top and bottom of the visible area.
	ld hl, $9800
	ld b, 20
.top:
	ld a, 1
	ld [hl+], a
	dec b
	jr nz, .top

	ld hl, $9800 + 32 * 17
	ld b, 20
.bottom:
	ld a, 1
	ld [hl+], a
	dec b
	jr nz, .bottom

	; Palette: 3 2 1 0, a soft four-shade ramp.
	ld a, %11100100
	ldh [rBGP], a

	; Sprite palette too.
	ldh [rOBP0], a

	; One sprite, parked in the middle of the screen.
	ld a, 76
	ld [$fe00], a          ; Y
	ld a, 80
	ld [$fe01], a          ; X
	xor a
	ld [$fe02], a          ; tile 0
	ld [$fe03], a          ; flags

	ld a, %10010011       ; LCD on, background on, sprites on, 8x8 tiles
	ldh [rLCDC], a

	call DrawTitle

MainLoop:
	call WaitVBlank

	; Move the sprite according to the d-pad.
	call ReadInput

	ld a, [ScrollX]
	ldh [rSCX], a
	ld a, [ScrollY]
	ldh [rSCY], a

	jr MainLoop

; ---------------------------------------------------------------- input

ReadInput:
	ld a, $20            ; select the d-pad
	ldh [rP1], a
	ldh a, [rP1]
	ldh a, [rP1]

	bit 0, a             ; right
	jr nz, .not_right
	ld hl, $fe01
	inc [hl]
.not_right:
	bit 1, a             ; left
	jr nz, .not_left
	ld hl, $fe01
	dec [hl]
.not_left:
	bit 2, a             ; up
	jr nz, .not_up
	ld hl, $fe00
	dec [hl]
.not_up:
	bit 3, a             ; down
	jr nz, .not_down
	ld hl, $fe00
	inc [hl]
.not_down:
	ret

WaitVBlank:
	ldh a, [rLY]
	cp 144
	jr nz, WaitVBlank
	ret

; ---------------------------------------------------------------- drawing

; Writes "GAME BOY" across the top of the background map.
DrawTitle:
	ld de, TitleText
	ld hl, $9800 + 32 * 2 + 3

.next:
	ld a, [de]
	and a
	ret z

	; Characters map onto tiles 10 onwards: A-Z, 0-9, space, ! and -.
	call CharToTile
	ld [hl+], a
	inc de
	jr .next

; Converts an ASCII character to the tile index used above.
CharToTile:
	cp 32            ; space
	jr z, .space
	cp 33            ; !
	jr z, .bang
	cp 45            ; -
	jr z, .dash
	cp 48            ; '0'
	jr c, .upper
	cp 58            ; '9' + 1
	jr nc, .upper
	sub 48 - 36      ; digits are tiles 36-45
	ret
.upper:
	sub 65 - 10      ; letters are tiles 10-35
	ret
.space:
	ld a, 46
	ret
.bang:
	ld a, 47
	ret
.dash:
	ld a, 48
	ret

; ---------------------------------------------------------------- data

SECTION "Data", ROM0

TitleText:
	db "GAME BOY", 0

ScrollX:
	db 0
ScrollY:
	db 0

; Tile 0: blank. Tile 1: a solid border block. Tiles 10-35: A-Z.
Tiles:
	; Tile 0 doubles as the sprite: a solid 8x8 block, so the player can see the
	; d-pad working.
	db %00000000, %00000000
	db %01111110, %01111110
	db %01111110, %01111110
	db %01111110, %01111110
	db %01111110, %01111110
	db %01111110, %01111110
	db %01111110, %01111110
	db %00000000, %00000000

	; a filled square with a one pixel edge
	db %11111111, %11111111
	db %10000001, %10000001
	db %10000001, %10000001
	db %10000001, %10000001
	db %10000001, %10000001
	db %10000001, %10000001
	db %10000001, %10000001
	db %11111111, %11111111

	; Tile 2 is the blank background tile.
	ds 16, 0

	; Tiles 3-9 are unused but must exist, or the letters land eight slots
	; early and the title comes out as "OIUM JWÉ".
	ds 16 * 7, 0

	; Tiles 10 onwards: A-Z, 0-9, space, ! and -.
	INCLUDE "tiles.inc"

TilesEnd:

SECTION "Sprite", ROM0
	ds 16, $ff
