#include <cstddef>
#include <cstring>
extern "C" {
	#include <tonc.h>
	#include "4x6_bin.h"
}
#include "assets.h"
#include "driver_gba.h"
#include "filesystem_romfs.h"
#include "user_interface_slim.h"
#include "platform_hacks.h"

#include "gamevars.h"
#include "sounds.h"

using namespace ZZT;

static Game game = Game();
static GBADriver driver;

static uint16_t hsecs;
static uint16_t prev_key_input;
extern u32 __rom_end__;

// #define DEBUG_CONSOLE

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
		REG_SOUND2CNT_L = SSQR_DUTY1_2 | SSQR_IVOL(12);
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

// video/input

#define FONT_HEIGHT 6
#define MAP_Y_OFFSET 1
#define MAP_ADDR_OFFSET 0x8000

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

GBA_CODE_IWRAM
static void vram_update_bgcnt(void) {
	int fg_cbb = (blinking_enabled && (blink_ticks & 16)) ? 1 : 0;

	REG_BG0CNT = BG_PRIO(3) | BG_CBB(0) | BG_SBB((MAP_ADDR_OFFSET >> 11) + 0) | BG_4BPP | BG_SIZE0;
	REG_BG1CNT = BG_PRIO(2) | BG_CBB(0) | BG_SBB((MAP_ADDR_OFFSET >> 11) + 1) | BG_4BPP | BG_SIZE0;
	REG_BG2CNT = BG_PRIO(1) | BG_CBB(fg_cbb) | BG_SBB((MAP_ADDR_OFFSET >> 11) + 2) | BG_4BPP | BG_SIZE0;
	REG_BG3CNT = BG_PRIO(0) | BG_CBB(fg_cbb) | BG_SBB((MAP_ADDR_OFFSET >> 11) + 3) | BG_4BPP | BG_SIZE0;
}

#define GET_VRAM_PTRS \
	u16* tile_bg_ptr = (u16*) (MEM_VRAM + MAP_ADDR_OFFSET + ((x&1) << 11) + (x&(~1)) + ((y + MAP_Y_OFFSET) << 6)); \
	u16* tile_fg_ptr = &tile_bg_ptr[1 << 11]

GBA_CODE_IWRAM static void vram_write_char(int16_t x, int16_t y, uint8_t col, uint8_t chr) {
	GET_VRAM_PTRS;

	*tile_bg_ptr = '\xDB' | (((col >> 4) & 0x07) << 12);
	*tile_fg_ptr = chr | ((col & 0x80) << 1) | (col << 12);
}

static void vram_read_char(int16_t x, int16_t y, uint8_t *col, uint8_t *chr) {
	GET_VRAM_PTRS;

	*chr = (*tile_fg_ptr & 0xFF);
	*col = (*tile_fg_ptr >> 12) | ((*tile_bg_ptr >> 8) & 0x70) | ((*tile_fg_ptr >> 1) & 0x80);
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
		
		memset32(tile_fg_ptr, 0, 16);
		memset32(tile_fg_ptr + (1 << 10), 0, 16);

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

				memcpy32(tile_fg_ptr, tile_fg_ptr + 32, 16 * (CONSOLE_HEIGHT - 1));
				memcpy32(tile_fg_ptr + (1 << 10), tile_fg_ptr + 32 + (1 << 10), 16 * (CONSOLE_HEIGHT - 1));

				memset32(tile_fg_ptr + (32*(CONSOLE_HEIGHT-1)), 0, 16);
				memset32(tile_fg_ptr + (32*(CONSOLE_HEIGHT-1)) + (1 << 10), 0, 16);

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

GBA_CODE_IWRAM void ZZT::irq_vblank(void) {
	REG_DMA0CNT = 0;
	REG_BG0HOFS = 4;
	REG_BG0VOFS = 0;
	REG_BG1HOFS = 0;
	REG_BG1VOFS = 0;
	REG_BG2HOFS = 4;
	REG_BG2VOFS = 0;
	REG_BG3HOFS = 0;
	REG_BG3VOFS = 0;
	REG_DMA0SAD = (vu32) hdma_offsets;
	REG_DMA0DAD = (vu32) &REG_BG0HOFS;
	REG_DMA0CNT = DMA_AT_HBLANK | DMA_REPEAT | DMA_SRC_INC | DMA_DST_RELOAD | (sizeof(hdma_ofs_t) >> 2) | DMA_32 | DMA_ENABLE;

#ifdef DEBUG_CONSOLE
	if ((REG_KEYINPUT & KEY_R) == 0) {
		recalculate_hdma_offsets(true);
	} else {
		recalculate_hdma_offsets(false);
	}
#endif

	driver.update_joy();

	if (blinking_enabled) {
		if (((blink_ticks++) & 15) == 0) {
			vram_update_bgcnt();
		}
	}

#ifdef DEBUG_CONSOLE
	sstring<20> num_str;
	StrFromInt(num_str, platform_debug_free_memory());
	platform_debug_puts(num_str, true);
#endif
}

void zoo_video_gba_hide(void) {
	REG_DISPCNT = DCNT_BLANK;
}

void zoo_video_gba_show(void) {
	VBlankIntrWait();
	REG_DISPCNT = DCNT_MODE0 | DCNT_BG0 | DCNT_BG1 | DCNT_BG2 | DCNT_BG3;
}

void zoo_video_gba_set_blinking(bool val) {
	blinking_enabled = val;
	vram_update_bgcnt();
}

// timer code

GBA_CODE_IWRAM void ZZT::irq_timer_pit(void) {
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

#define dbg_ticks() (REG_TM2CNT_L | (REG_TM3CNT_L << 16))

void zoo_video_gba_install(const uint8_t *charset_bin) {
	recalculate_hdma_offsets(false);

	// add interrupts
	irq_add(II_VBLANK, irq_vblank);
	
	// load 4x6 charset
	memset32(((uint32_t*) (MEM_VRAM)), 0x0000000, 256 * 32);
	Charset charset = Charset(256, 4, 6, 1, _4x6_bin);
	Charset::Iterator charsetIterator = charset.iterate();
	while (charsetIterator.next()) {
		if (charsetIterator.value >= 128) {
			uint16_t &line = *(((uint16_t*) MEM_VRAM) + 1 + ((uint32_t) charsetIterator.y << 1) + ((uint32_t) charsetIterator.glyph << 4));
			line |= (1 << (charsetIterator.x << 2));
		}
	}

	// 32KB is used to faciliate blinking:
	// chars 0-255: charset, not blinking
	// chars 256-511: charset, blinking, visible
	// chars 512-767: [blink] charset, not blinking
	// chars 768-1023: [blink] empty space, not visible
	memcpy32(((uint32_t*) (MEM_VRAM + (256*32))), ((uint32_t*) (MEM_VRAM)), 256 * 32);
	memcpy32(((uint32_t*) (MEM_VRAM + (512*32))), ((uint32_t*) (MEM_VRAM)), 256 * 32);
	memset32(((uint32_t*) (MEM_VRAM + (768*32))), 0x0000000, 256 * 32);

	// load palette
	for (int i = 0; i < 16; i++) {
		pal_bg_mem[(i<<4) | 0] = 0x0000;
		pal_bg_mem[(i<<4) | 1] = default_palette[i];
	}

	// initialize background registers
	vram_update_bgcnt();

	// clear display
	memset32((void*) (MEM_VRAM + MAP_ADDR_OFFSET), 0x00000000, 64 * 32 * 2);
}

GBADriver::GBADriver() {
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

    zoo_video_gba_install(_4x6_bin);

	zoo_video_gba_set_blinking(true);
	zoo_video_gba_show();

	// init sound
	REG_SOUNDCNT_X = SSTAT_ENABLE;
	REG_SOUNDCNT_L = SDMG_LVOL(7) | SDMG_RVOL(7) | SDMG_LSQR2 | SDMG_RSQR2;
	REG_SOUNDCNT_H = SDS_DMG100;
	REG_SOUNDBIAS = 0xC200;
	irq_add(II_TIMER1, (fnptr) irq_timer_drums);

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
		keyboard.open(-1, 2, mode);
	} else {
		keyboard.close();
	}
}

void GBADriver::update_input(void) {
    advance_input();
	if (keyboard.opened()) {
		keyboard.update();
		keyPressed = keyboard.key_pressed;
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
	VBlankIntrWait();
}

void GBADriver::sound_stop(void) {
	gba_play_sound(0);
}

void GBADriver::draw_char(int16_t x, int16_t y, uint8_t col, uint8_t chr) {
	vram_write_char(x, y, col, chr);
}

void GBADriver::read_char(int16_t x, int16_t y, uint8_t &col, uint8_t &chr) {
	vram_read_char(x, y, &col, &chr);
}

void GBADriver::get_video_size(int16_t &width, int16_t &height) {
	width = 60;
	height = 26;
}

int main(void) {
	driver.keyboard.driver = &driver;
	driver.install();

	game.driver = &driver;
	game.filesystem = new RomfsFilesystemDriver(&__rom_end__);
	game.interface = new UserInterfaceSlim(&driver);

	game.GameTitleLoop();

	delete game.interface;
	delete game.filesystem;

	driver.uninstall();

	while(1);
	//return 0;
}
