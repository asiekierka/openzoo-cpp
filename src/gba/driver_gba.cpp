#include <cstddef>
#include <cstdint>
#include <cstring>
extern "C" {
	#include <tonc.h>
	#include "4x6_bin.h"
	#include "4x8_bin.h"
	#include "8x8_bin.h"
}
#include "assets.h"
#include "driver_gba.h"
#include "filesystem_romfs.h"
#include "user_interface_slim.h"
#include "user_interface_super_zzt.h"
#include "gamevars.h"
#include "platform_hacks.h"
#include "ui_hacks_gba.h"

#include "gamevars.h"
#include "sounds.h"

using namespace ZZT;

static Game game = Game();
static GBADriver driver;

static uint16_t hsecs;
static uint16_t prev_key_input;
extern u32 __rom_end__;

#define DEBUG_CONSOLE

uint8_t ext_tile_memory[62 * 27 * sizeof(Tile)];
uint8_t ext_stat_memory[153 * sizeof(Stat)];

// sound logic

static const SoundDrum *drum = nullptr;
static int16_t drum_pos = 0;
static int16_t duration_counter = 0;

static inline void gba_play_sound(uint16_t freq) {
	if (freq < 64) {
		REG_SOUND2CNT_L = SSQR_DUTY1_2 | SSQR_IVOL(0);
		REG_SOUND2CNT_H = SFREQ_RESET;
	} else {
		REG_SOUND2CNT_L = SSQR_DUTY1_2 | SSQR_IVOL(15);
		REG_SOUND2CNT_H = (2048 - (131072 / (int)freq)) | SFREQ_RESET;
	}
}

GBA_CODE_IWRAM
static void irq_timer_drums(void) {
	REG_IE |= (IRQ_VBLANK);
	REG_IME = 1;

	if (drum_pos < drum->len) {
		// play
		gba_play_sound(drum->data[drum_pos++]);

		// set timer
		REG_TM1CNT_L = 65536 - 262;
		REG_TM1CNT_H = TM_FREQ_64 | TM_IRQ | TM_ENABLE;
	} else if (drum_pos == drum->len) {
		// finish
		gba_play_sound(0);

		// reset timer
		REG_TM1CNT_H = 0;
	}
}

static void irq_timer_soundbias(void) {
	if (REG_SOUNDBIAS > 0xC000) {
		uint16_t new_bias = REG_SOUNDBIAS - 1;
		REG_SOUNDBIAS = new_bias;

		// set timer
		REG_TM1CNT_L = 65536 - 64;
		REG_TM1CNT_H = TM_FREQ_64 | TM_IRQ | TM_ENABLE;
	} else {
		REG_TM1CNT_H = 0;
		irq_add(II_TIMER1, (fnptr) irq_timer_drums);
	}
}

// video/input

#define FONT_HEIGHT 6
#define MAP_Y_OFFSET 1
#define MAP_ADDR_OFFSET 0xC000

typedef struct {
	uint16_t hofs0, vofs0, hofs1, vofs1, hofs2, vofs2, hofs3, vofs3;
} hdma_ofs_t;

static hdma_ofs_t hdma_offsets[160];

static const u16 default_palette[] = {
	0x0000,
	0x5000,
	0x0280,
	0x5280,
	0x0014,
	0x5014,
	0x0154,
	0x5294,
	0x294A,
	0x7D4A,
	0x2BEA,
	0x7FEA,
	0x295F,
	0x7D5F,
	0x2BFF,
	0x7FFF
};

static bool blinking_enabled = false;
static uint8_t blink_ticks;
static uint8_t mode_6_lines;
static Force4x8ModeType force_4x8_mode = FORCE_4X8_MODE_NONE;
static bool mode_6_lines_inited = false;
static bool video_hidden = false;

void zoo_set_force_4x8_mode(Force4x8ModeType value) {
	force_4x8_mode = value;
}

GBA_CODE_IWRAM
static void vram_update_bgcnt(void) {
	int bg_cbb = !mode_6_lines ? 2 : 0;
	int fg_cbb = !mode_6_lines ? 2 : ((blinking_enabled && (blink_ticks & 16)) ? 1 : 0);

	REG_BG0CNT = BG_PRIO(3) | BG_CBB(bg_cbb) | BG_SBB((MAP_ADDR_OFFSET >> 11) + 0) | BG_4BPP | BG_SIZE0;
	REG_BG1CNT = BG_PRIO(2) | BG_CBB(bg_cbb) | BG_SBB((MAP_ADDR_OFFSET >> 11) + 1) | BG_4BPP | BG_SIZE0;
	REG_BG2CNT = BG_PRIO(1) | BG_CBB(fg_cbb) | BG_SBB((MAP_ADDR_OFFSET >> 11) + 2) | BG_4BPP | BG_SIZE0;
	REG_BG3CNT = BG_PRIO(0) | BG_CBB(fg_cbb) | BG_SBB((MAP_ADDR_OFFSET >> 11) + 3) | BG_4BPP | BG_SIZE0;
}

#define GET_VRAM_PTRS \
	u16* tile_bg_ptr = (u16*) (MEM_VRAM + MAP_ADDR_OFFSET + ((x&1) << 11) + (x & 0x3E) + ((y + MAP_Y_OFFSET) << 6)); \
	u16* tile_fg_ptr = &tile_bg_ptr[1 << 11]

#define GET_VRAM_PTRS_WIDE \
	u16* tile_bg_ptr = (u16*) (MEM_VRAM + MAP_ADDR_OFFSET + (1 << 11) + (x << 1) + ((y + MAP_Y_OFFSET) << 6)); \
	u16* tile_fg_ptr = &tile_bg_ptr[1 << 11]

GBA_CODE_IWRAM
static void vram_write_char(int16_t x, int16_t y, uint8_t col, uint8_t chr) {
	GET_VRAM_PTRS;

	*tile_bg_ptr = '\xDB' | ((col << 8) & 0x7000); 
	*tile_fg_ptr = chr | ((col & 0x80) << 1) | (col << 12);
}

GBA_CODE_IWRAM
static void vram_write_char_4x8(int16_t x, int16_t y, uint8_t col, uint8_t chr) {
	GET_VRAM_PTRS;

	*tile_bg_ptr = '\xDB' | ((col << 8) & 0x7000); 
	*tile_fg_ptr = chr | (col << 12);
}

GBA_CODE_IWRAM
static void vram_write_char_8x8(int16_t x, int16_t y, uint8_t col, uint8_t chr) {
	if (x >= 32) return;

	GET_VRAM_PTRS_WIDE;

	tile_bg_ptr[0] = '\xDB' | 0x100 | ((col << 8) & 0x7000); 
	tile_fg_ptr[0] = chr | 0x100 | (col << 12);

	// clear the other layers
	tile_bg_ptr -= (1 << 10);
	tile_fg_ptr -= (1 << 10);

	tile_bg_ptr[0] = 0;
	tile_fg_ptr[0] = 0;
}

// TODO: figure out how to adapt to the wide mode
GBA_CODE_IWRAM
static void vram_read_char(int16_t x, int16_t y, uint8_t *col, uint8_t *chr) {
	GET_VRAM_PTRS;

	*chr = (*tile_fg_ptr & 0xFF);
	*col = (*tile_fg_ptr >> 12) | ((*tile_bg_ptr >> 8) & 0x70) | ((*tile_fg_ptr >> 1) & 0x80);
}

GBA_CODE_IWRAM
static void vram_read_char_wide(int16_t x, int16_t y, uint8_t *col, uint8_t *chr) {
	GET_VRAM_PTRS_WIDE;

	*chr = (*tile_fg_ptr & 0xFF);
	*col = (*tile_fg_ptr >> 12) | ((*tile_bg_ptr >> 8) & 0x70);
}

#ifdef DEBUG_CONSOLE
static u16 console_x = 0;
static u16 console_y = 0;

#define CONSOLE_WIDTH 60
#define CONSOLE_HEIGHT 3
#define CONSOLE_YOFFSET 26

void platform_debug_puts(const char *text, bool status) {
	if (status) {
		// clear line
		int x = 0;
		int y = CONSOLE_YOFFSET + CONSOLE_HEIGHT;
		GET_VRAM_PTRS;
		
		memset32(tile_fg_ptr, 0, 32 * 2 / 4);
		memset32(tile_fg_ptr + (1 << 10), 0, 32 * 2 / 4);

		while (*text != '\0') {
			char c = *(text++);
			if (c == '\n') continue;
			vram_write_char(x++, CONSOLE_YOFFSET + CONSOLE_HEIGHT, 0x0A, c);
		}
	} else {
		while (*text != '\0') {
			char c = *(text++);

			if (c == '\n') {
				console_x = 0;
				console_y += 1;
				continue;
			}

			while (console_y >= CONSOLE_HEIGHT) {
				// scroll one up
				int x = 0;
				int y = CONSOLE_YOFFSET;
				GET_VRAM_PTRS;

				memcpy32(tile_fg_ptr, tile_fg_ptr + 32, 32 * (CONSOLE_HEIGHT - 1) * 2 / 4);
				memcpy32(tile_fg_ptr + (1 << 10), tile_fg_ptr + 32 + (1 << 10), 32 * (CONSOLE_HEIGHT - 1) * 2 / 4);

				memset32(tile_fg_ptr + (32*(CONSOLE_HEIGHT-1)), 0, 32 * 2 / 4);
				memset32(tile_fg_ptr + (32*(CONSOLE_HEIGHT-1)) + (1 << 10), 0, 32 * 2 / 4);

				console_y--;
			}

			vram_write_char(console_x, console_y + CONSOLE_YOFFSET, 0x0F, c);
			console_x += 1;
			if (console_x >= CONSOLE_WIDTH) {
				console_x = 0;
				console_y += 1;
			}
		}
	}
}
#endif

#include <malloc.h>
#include <unistd.h>

/* https://devkitpro.org/viewtopic.php?f=6&t=3057 */

extern uint8_t *fake_heap_end;
extern uint8_t *fake_heap_start;

static int platform_debug_free_memory(void) {
	struct mallinfo info = mallinfo();
	return info.fordblks + (fake_heap_end - (uint8_t*)sbrk(0));
}

static uint8_t last_hdma_offset = 0;

static void recalculate_hdma_offsets(bool debug) {
#ifdef DEBUG_CONSOLE
	uint8_t next_hdma_offset = debug ? 2 : 1;
	if (next_hdma_offset == last_hdma_offset) {
		return;
	}
	last_hdma_offset = next_hdma_offset;
#endif
	uint16_t disp_y_offset = /* (inst_state->game_state == GS_PLAY) */ 1
		? ((FONT_HEIGHT * MAP_Y_OFFSET) - ((SCREEN_HEIGHT - (FONT_HEIGHT * 26)) / 2))
		: ((FONT_HEIGHT * MAP_Y_OFFSET) - ((SCREEN_HEIGHT - (FONT_HEIGHT * 25)) / 2));
	int next_vcount = FONT_HEIGHT - disp_y_offset;
	if (debug) {
		disp_y_offset = (8 * 31) - (8 * 26) + 1;
		next_vcount = 5;
	}

	for (int i = 0; i < 160; i++) {
		if (i == next_vcount-1) {
			disp_y_offset += (8 - FONT_HEIGHT);	
			next_vcount += FONT_HEIGHT;	
		}
		hdma_offsets[i] = {
			4, disp_y_offset, 0, disp_y_offset, 4, disp_y_offset, 0, disp_y_offset
		};
	}
}

GBA_CODE_IWRAM
void ZZT::irq_vblank(void) {
	REG_DMA0CNT = 0;
	REG_BG0HOFS = 4;
	REG_BG0VOFS = 0;
	REG_BG1HOFS = 0;
	REG_BG1VOFS = 0;
	REG_BG2HOFS = 4;
	REG_BG2VOFS = 0;
	REG_BG3HOFS = 0;
	REG_BG3VOFS = 0;
	if (mode_6_lines && !video_hidden) {
		REG_DMA0SAD = (vu32) hdma_offsets;
		REG_DMA0DAD = (vu32) &REG_BG0HOFS;
		REG_DMA0CNT = DMA_AT_HBLANK | DMA_REPEAT | DMA_SRC_INC | DMA_DST_RELOAD | (sizeof(hdma_ofs_t) >> 2) | DMA_32 | DMA_ENABLE;

		if (blinking_enabled) {
			if (((blink_ticks++) & 15) == 0) {
				vram_update_bgcnt();
			}
		}

#ifdef DEBUG_CONSOLE
		if ((REG_KEYINPUT & KEY_R) == 0) {
			recalculate_hdma_offsets(true);
		} else {
			recalculate_hdma_offsets(false);
		}
#endif
	} else {
		int16_t vofs = 8;
		if ((REG_KEYINPUT & KEY_R) == 0) {
			vofs = (32 * 8) - (20 * 8);
		}
		REG_BG0VOFS = vofs;
		REG_BG1VOFS = vofs;
		REG_BG2VOFS = vofs;
		REG_BG3VOFS = vofs;
	}

	driver.update_joy();
}

void zoo_video_gba_hide(void) {
	video_hidden = true;
	REG_DMA0CNT = 0;
	REG_DISPCNT = DCNT_BLANK;
}

void zoo_video_gba_show(void) {
	video_hidden = false;
	VBlankIntrWait();
	REG_DISPCNT = DCNT_MODE0 | DCNT_BG0 | DCNT_BG1 | DCNT_BG2 | DCNT_BG3;
}

void zoo_video_gba_set_blinking(bool val) {
	blinking_enabled = val;
	vram_update_bgcnt();
}

// timer code

#define dbg_ticks() (REG_TM2CNT_L | (REG_TM3CNT_L << 16))

uint32_t tmp_ticks = 0;
bool tick_started = false;

void gba_on_tick_start() {
	tick_started = true;
	tmp_ticks = dbg_ticks();
}

void gba_on_tick_end() {
	if (!tick_started) return;
	tick_started = false;
	sstring<127> tmp;
	tmp_ticks = dbg_ticks() - tmp_ticks;
	if (tmp_ticks >= 0x80000000) tmp_ticks = 0xFFFFFFFF - tmp_ticks;
	siprintf(tmp, "%d ticks, %d%%, %d bytes", tmp_ticks, tmp_ticks * 10 / 184568, platform_debug_free_memory());
	platform_debug_puts(tmp, true);
}

GBA_CODE_IWRAM
void ZZT::irq_timer_pit(void) {
	REG_IE |= (IRQ_VBLANK);
	REG_IME = 1;

	hsecs += 11;

	if (!driver._queue.enabled) {
		driver._queue.is_playing = false;
		gba_play_sound(0);
	} else {
		if ((--duration_counter) <= 0) {
			gba_play_sound(0);
			uint16_t note, duration;
			if (!driver._queue.pop(note, duration)) {
				driver._queue.is_playing = false;
			} else {
				if (note >= NOTE_MIN && note < NOTE_MAX) {
					gba_play_sound(sound_notes[note - NOTE_MIN]);
				} else if (note >= DRUM_MIN && note < DRUM_MAX) {
					drum = &sound_drums[note - DRUM_MIN];
					drum_pos = 0;
					irq_timer_drums();
				}

				duration_counter = duration;
			}
		}
	}
}

static void zoo_video_gba_load_charset(uint32_t mem_offset, Charset &charset) {
	uint16_t *offset = (uint16_t*) (MEM_VRAM + mem_offset);
	memset32((uint32_t*) offset, 0x0000000, 256 * 32 / 4);

	if (charset.width() > 4) {
		Charset::Iterator charsetIterator = charset.iterate();
		while (charsetIterator.next()) {
			if (charsetIterator.value >= 128) {
				uint16_t *line = offset + (charsetIterator.x >> 2) + ((uint32_t) charsetIterator.y << 1) + ((uint32_t) charsetIterator.glyph << 4);
				*line |= (1 << ((charsetIterator.x & 0x03) << 2));
			}
		}
	} else {
		Charset::Iterator charsetIterator = charset.iterate();
		while (charsetIterator.next()) {
			if (charsetIterator.value >= 128) {
				uint16_t *line = offset + 1 + ((uint32_t) charsetIterator.y << 1) + ((uint32_t) charsetIterator.glyph << 4);
				*line |= (1 << (charsetIterator.x << 2));
			}
		}
	}
}

static void zoo_video_clear_fast(void) {
	memset32((void*) (MEM_VRAM + MAP_ADDR_OFFSET), 0x00000000, 32 * 32 * 2 * 4 / 4);
}

static void zoo_video_set_mode_6_lines(uint8_t value) {
	if (value == 0 && !mode_6_lines_inited) {
		// load the charsets here, so it doesn't slow down initial start
		Charset charset = Charset(256, 4, 8, 1, _4x8_bin);
		zoo_video_gba_load_charset(0x08000, charset);
		charset = Charset(256, 8, 8, 1, _8x8_bin);
		zoo_video_gba_load_charset(0x0A000, charset);
		mode_6_lines_inited = true;
	}
	if (mode_6_lines != value) {
		VBlankIntrWait();
		zoo_video_clear_fast();
		mode_6_lines = value;
		vram_update_bgcnt();
		VBlankIntrWait();
	}
}

void zoo_video_gba_install(void) {
	recalculate_hdma_offsets(false);

	// add interrupts
	irq_add(II_VBLANK, irq_vblank);
	
	// load charsets
	Charset charset = Charset(256, 4, 6, 1, _4x6_bin);
	zoo_video_gba_load_charset(0x00000, charset);

	// initialize 32KB of data for blinking - see VRAM layout
	memcpy32(((uint32_t*) (MEM_VRAM + (256*32))), ((uint32_t*) (MEM_VRAM)), 256 * 32 / 4);
	memcpy32(((uint32_t*) (MEM_VRAM + (512*32))), ((uint32_t*) (MEM_VRAM)), 256 * 32 / 4);
	memset32(((uint32_t*) (MEM_VRAM + (768*32))), 0x0000000, 256 * 32 / 4);

	// load palette
	for (int i = 0; i < 16; i++) {
		pal_bg_mem[(i<<4) | 0] = 0x0000;
		pal_bg_mem[(i<<4) | 1] = default_palette[i];
	}

	// initialize background registers
	vram_update_bgcnt();

	zoo_video_clear_fast();
}

GBADriver::GBADriver() {
}

static void zoo_sound_gba_init() {
	REG_SOUNDCNT_X = SSTAT_ENABLE;
	REG_SOUNDCNT_L = SDMG_LVOL(7) | SDMG_RVOL(7) | SDMG_LSQR2 | SDMG_RSQR2;
	REG_SOUNDCNT_H = SDS_DMG100;
}

void GBADriver::install(void) {
	zoo_video_gba_hide();
	irq_init(isr_master_nest);

	prev_key_input = 0;

	// init game speed timer
	hsecs = 0;
	REG_TM0CNT_L = 65536 - 14398;
	REG_TM0CNT_H = TM_FREQ_64 | TM_IRQ | TM_ENABLE;

#ifdef DEBUG_CONSOLE
	// init ticktime counter
	REG_TM2CNT_L = 0;
	REG_TM2CNT_H = TM_FREQ_1 | TM_ENABLE;
	REG_TM3CNT_L = 0;
	REG_TM3CNT_H = TM_FREQ_1 | TM_CASCADE | TM_ENABLE;
#endif

    zoo_video_gba_install();

	zoo_video_gba_set_blinking(true);
	zoo_video_gba_show();

	// init sound
	zoo_sound_gba_init();
	irq_add(II_TIMER1, (fnptr) irq_timer_soundbias);

	REG_SOUNDBIAS = 0xC200;
	irq_timer_soundbias();

	// init pit
	irq_add(II_TIMER0, irq_timer_pit);
}

void GBADriver::uninstall(void) {
    
}

void GBADriver::update_joy(void) {
	uint16_t ki = REG_KEYINPUT;
	if (ki == prev_key_input) return;
	prev_key_input = ki;

	set_joy_button_state(JoyButtonUp, (ki & KEY_UP) == 0, true);
	set_joy_button_state(JoyButtonDown, (ki & KEY_DOWN) == 0, true);
	set_joy_button_state(JoyButtonLeft, (ki & KEY_LEFT) == 0, true);
	set_joy_button_state(JoyButtonRight, (ki & KEY_RIGHT) == 0, true);
	set_joy_button_state(JoyButtonA, (ki & KEY_A) == 0, true);
	set_joy_button_state(JoyButtonB, (ki & KEY_B) == 0, true);
	set_joy_button_state(JoyButtonL, (ki & KEY_L) == 0, true);
	set_joy_button_state(JoyButtonR, (ki & KEY_R) == 0, true);
	set_joy_button_state(JoyButtonSelect, (ki & KEY_SELECT) == 0, true);
	set_joy_button_state(JoyButtonStart, (ki & KEY_START) == 0, true);
}

void GBADriver::set_text_input(bool enabled, InputPromptMode mode) {
	if (enabled) {
		zoo_set_force_4x8_mode(FORCE_4X8_MODE_ALWAYS);
		keyboard.open(-1, 2, 60, mode);
	} else {
		zoo_set_force_4x8_mode(FORCE_4X8_MODE_READ);
		keyboard.close();
	}
	zoo_set_force_4x8_mode(FORCE_4X8_MODE_NONE);
}

void GBADriver::update_input(void) {
    advance_input();
	if (keyboard.opened()) {
		zoo_set_force_4x8_mode(FORCE_4X8_MODE_ALWAYS);
		keyboard.update();
		keyPressed = keyboard.key_pressed;
		zoo_set_force_4x8_mode(FORCE_4X8_MODE_NONE);
	}
}

uint16_t GBADriver::get_hsecs(void) {
    return (hsecs >> 1);
}

void GBADriver::delay(int ms) {
	if (ms > 1) {
		do {
			VBlankIntrWait();
			ms -= 16;
		} while (ms >= 16);
	}
}

void GBADriver::idle(IdleMode mode) {
	if (mode == IMYield) return;
	if (mode == IMUntilPit) IntrWait(1, IRQ_TIMER0);
	VBlankIntrWait();
}

void GBADriver::sound_stop(void) {
	gba_play_sound(0);
}

GBA_CODE_IWRAM
void GBADriver::draw_char(int16_t x, int16_t y, uint8_t col, uint8_t chr) {
	if (!mode_6_lines) {
		if (x < 12 || (force_4x8_mode == FORCE_4X8_MODE_ALWAYS)) {
			vram_write_char_4x8(x, y, col, chr);
		} else if ((force_4x8_mode == FORCE_4X8_MODE_READ)) {
			vram_write_char_8x8(x >> 1, y, col, chr);			
		} else {
			vram_write_char_8x8(x - 6, y, col, chr);
		}
	} else {
		vram_write_char(x, y, col, chr);
	}
}

void GBADriver::read_char(int16_t x, int16_t y, uint8_t &col, uint8_t &chr) {
	if (!mode_6_lines) {
		if (x < 12 || (force_4x8_mode != FORCE_4X8_MODE_NONE)) {
			vram_read_char(x, y, &col, &chr);
		} else {
			vram_read_char_wide(x - 6, y, &col, &chr);
		}
		col &= 0x7F;
	} else {
		vram_read_char(x, y, &col, &chr);
	}
}

UserInterface *GBADriver::create_user_interface(Game &game, bool is_editor) {
	if (game.engineDefinition.engineType == ENGINE_TYPE_SUPER_ZZT) {
		zoo_video_set_mode_6_lines(0);
		zoo_set_force_4x8_mode(FORCE_4X8_MODE_NONE);
		return new UserInterfaceSuperZZTGBA(this);
	} else {
		zoo_video_set_mode_6_lines(1);
		return new UserInterfaceSlim(this);
	}
}

void GBADriver::move_chars(int srcX, int srcY, int width, int height, int destX, int destY) {
	uint16_t buffer[32];

	uint8_t col, chr;
	int ix_min = (srcX > destX) ? 0 : width - 1;
	int ix_max = (srcX > destX) ? width : -1;
	int ix_step = (srcX > destX) ? 1 : -1;
	int iy_min = (srcY > destY) ? 0 : height - 1;
	int iy_max = (srcY > destY) ? height : -1;
	int iy_step = (srcY > destY) ? 1 : -1;
	// optimize the Super ZZT case
	if (!mode_6_lines && force_4x8_mode == FORCE_4X8_MODE_NONE && srcX >= 12 && destX >= 12) {
		int x = -6;
		int y = iy_min;
		GET_VRAM_PTRS_WIDE;
		
		if (iy_step > 0) {
			for (int iy = iy_min; iy != iy_max; iy++, tile_bg_ptr += 32, tile_fg_ptr += 32) {
				memcpy16(buffer, tile_bg_ptr + srcX + (srcY << 5), width);
				memcpy16(tile_bg_ptr + destX + (destY << 5), buffer, width);
				memcpy16(buffer, tile_fg_ptr + srcX + (srcY << 5), width);
				memcpy16(tile_fg_ptr + destX + (destY << 5), buffer, width);
			}
		} else {
			for (int iy = iy_min; iy != iy_max; iy--, tile_bg_ptr -= 32, tile_fg_ptr -= 32) {
				memcpy16(buffer, tile_bg_ptr + srcX + (srcY << 5), width);
				memcpy16(tile_bg_ptr + destX + (destY << 5), buffer, width);
				memcpy16(buffer, tile_fg_ptr + srcX + (srcY << 5), width);
				memcpy16(tile_fg_ptr + destX + (destY << 5), buffer, width);
			}
		}
		return;
	}

    for (int iy = iy_min; iy != iy_max; iy += iy_step) {
        for (int ix = ix_min; ix != ix_max; ix += ix_step) {
            read_char(srcX + ix, srcY + iy, col, chr);
            draw_char(destX + ix, destY + iy, col, chr);
        }
    }
}

void zoo_gba_sleep() {
	VBlankIntrWait();
	zoo_video_gba_hide();
	REG_SOUNDCNT_X = 0;

	REG_KEYCNT = KCNT_IRQ | KCNT_AND | KEY_START | KEY_SELECT;

	uint16_t old_ie = REG_IE;
	REG_IE = (IRQ_KEYPAD);
	Stop();
	REG_IE = old_ie;
	
	REG_KEYCNT = 0;

	while ((REG_KEYINPUT & 0x3FF) != 0x3FF) {
		VBlankIntrWait();
	}
	
	zoo_video_gba_show();
	zoo_sound_gba_init();	
}

int main(void) {
	REG_WAITCNT = 0x4000;

	driver.keyboard.driver = &driver;
	driver.install();

	game.driver = &driver;
	game.filesystem = new RomfsFilesystemDriver(&__rom_end__);

	game.GameTitleLoop();

	delete game.filesystem;

	driver.uninstall();

	SoftReset();
	//return 0;
}
