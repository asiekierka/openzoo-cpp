#include "user_interface_osk.h"
#include "utils/mathutils.h"

using namespace ZZT;

#define OSK_SPECIAL_SHIFT 0xE000

#define ENTRIES_DIGIT_COUNT 12
static const OSKEntry entries_digit[ENTRIES_DIGIT_COUNT] = {
	{0,  0, '1', '1'},
	{2,  0, '2', '2'},
	{4,  0, '3', '3'},
	{0,  2, '4', '4'},
	{2,  2, '5', '5'},
	{4,  2, '6', '6'},
	{0,  4, '7', '7'},
	{2,  4, '8', '8'},
	{4,  4, '9', '9'},
	{0,  6, 8, 27},
	{2,  6, '0', '0'},
	{4,  6, 13, 26}
};
static const OSKLayout layout_digit = {
#ifdef __GBA__
	.width = 6,
#else
    .width = 5,
#endif
    .height = 7,
    .entries = entries_digit,
    .entry_count = ENTRIES_DIGIT_COUNT,
};

#define ENTRIES_ALPHANUM_COUNT 40
static const OSKEntry entries_alphanum[ENTRIES_ALPHANUM_COUNT] = {
	{0,  0, '1', '1'},
	{2,  0, '2', '2'},
	{4,  0, '3', '3'},
	{6,  0, '4', '4'},
	{8,  0, '5', '5'},
	{10, 0, '6', '6'},
	{12, 0, '7', '7'},
	{14, 0, '8', '8'},
	{16, 0, '9', '9'},
	{18, 0, '0', '0'},
	{20, 0, '-', '-'},
	{1,  1, 'q', 'Q'},
	{3,  1, 'w', 'W'},
	{5,  1, 'e', 'E'},
	{7,  1, 'r', 'R'},
	{9,  1, 't', 'T'},
	{11, 1, 'y', 'Y'},
	{13, 1, 'u', 'U'},
	{15, 1, 'i', 'I'},
	{17, 1, 'o', 'O'},
	{19, 1, 'p', 'P'},
	{21, 1, 8,   27},
	{0, 2, OSK_SPECIAL_SHIFT, 24},
	{2,  2, 'a', 'A'},
	{4,  2, 's', 'S'},
	{6,  2, 'd', 'D'},
	{8,  2, 'f', 'F'},
	{10, 2, 'g', 'G'},
	{12, 2, 'h', 'H'},
	{14, 2, 'j', 'J'},
	{16, 2, 'k', 'K'},
	{18, 2, 'l', 'L'},
	{20, 2, 13,  26},
	{3,  3, 'z', 'Z'},
	{5,  3, 'x', 'X'},
	{7,  3, 'c', 'C'},
	{9,  3, 'v', 'V'},
	{11, 3, 'b', 'B'},
	{13, 3, 'n', 'N'},
	{15, 3, 'm', 'M'}
};
static const OSKLayout layout_alphanum = {
    .width = 22,
    .height = 4,
    .entries = entries_alphanum,
    .entry_count = ENTRIES_ALPHANUM_COUNT,
};

#define ENTRIES_TEXT_COUNT 48
static const OSKEntry entries_text[ENTRIES_TEXT_COUNT] = {
	{0,  0, '1', '!'},
	{2,  0, '2', '@'},
	{4,  0, '3', '#'},
	{6,  0, '4', '$'},
	{8,  0, '5', '%'},
	{10, 0, '6', '^'},
	{12, 0, '7', '&'},
	{14, 0, '8', '*'},
	{16, 0, '9', '('},
	{18, 0, '0', ')'},
	{20, 0, '-', '_'},
	{22, 0, '=', '+'},
	{24, 0, 8,   27},
	{1,  1, 'q', 'Q'},
	{3,  1, 'w', 'W'},
	{5,  1, 'e', 'E'},
	{7,  1, 'r', 'R'},
	{9,  1, 't', 'T'},
	{11, 1, 'y', 'Y'},
	{13, 1, 'u', 'U'},
	{15, 1, 'i', 'I'},
	{17, 1, 'o', 'O'},
	{19, 1, 'p', 'P'},
	{21, 1, '[', '{'},
	{23, 1, ']', '}'},
	{0, 2, OSK_SPECIAL_SHIFT, 24},
	{2,  2, 'a', 'A'},
	{4,  2, 's', 'S'},
	{6,  2, 'd', 'D'},
	{8,  2, 'f', 'F'},
	{10, 2, 'g', 'G'},
	{12, 2, 'h', 'H'},
	{14, 2, 'j', 'J'},
	{16, 2, 'k', 'K'},
	{18, 2, 'l', 'L'},
	{20, 2, ';', ':'},
	{22, 2, '\'', '"'},
	{24, 2, 13,  26},
	{3,  3, 'z', 'Z'},
	{5,  3, 'x', 'X'},
	{7,  3, 'c', 'C'},
	{9,  3, 'v', 'V'},
	{11, 3, 'b', 'B'},
	{13, 3, 'n', 'N'},
	{15, 3, 'm', 'M'},
	{17, 3, ',', '<'},
	{19, 3, '.', '>'},
	{21, 3, '/', '?'}
};
static const OSKLayout layout_text = {
#ifdef __GBA__
	.width = 26,
#else
    .width = 25,
#endif
    .height = 4,
    .entries = entries_text,
    .entry_count = ENTRIES_TEXT_COUNT,
};

static const uint8_t osk_border[9] = {
    214, 196, 183,
    186, 32, 186,
    211, 196, 189
};

OnScreenKeyboard::OnScreenKeyboard() {
    this->backup = nullptr;
}

OnScreenKeyboard::~OnScreenKeyboard() {
    this->close();
}

void OnScreenKeyboard::draw(bool redraw) {
    int width = layout->width + 6;
    int height = layout->height + 4;
    int x_offset = 3;
    int y_offset = 2;

    if (redraw) {
        for (int iy = 0; iy < height; iy++) {
            int ofs_y = (iy == 0) ? 0 : ((iy == (height - 1)) ? 6 : 3);
            driver->draw_char(this->x, this->y + iy, 0x1F, osk_border[ofs_y]);
            for (int ix = 1; ix < width - 1; ix++) {
                driver->draw_char(this->x + ix, this->y + iy, 0x1F, osk_border[ofs_y + 1]);
            }
            driver->draw_char(this->x + width - 1, this->y + iy, 0x1F, osk_border[ofs_y + 2]);
        }
    }

    for (int i = 0; i < layout->entry_count; i++) {
        const OSKEntry &entry = layout->entries[i];
		driver->draw_char(x + x_offset + entry.x, y + y_offset + entry.y, e_pos == i ? 0x71 : 0x1F,
			(entry.char_regular >= 32 && entry.char_regular < 128) ? (shifted ? entry.char_shifted : entry.char_regular) : entry.char_shifted);
    }
}

void OnScreenKeyboard::open(int16_t x, int16_t y, int16_t width, InputPromptMode mode) {
    close();

    // int16_t dw, dh;
    // driver->get_video_size(dw, dh);

	if (mode == InputPMNumbers) this->layout = &layout_digit;
    else if (mode == InputPMAlphanumeric) this->layout = &layout_alphanum;
	else this->layout = &layout_text;

    this->x = x < 0 ? (width - (layout->width + 6)) >> 1 : x;
#ifdef __GBA__
	this->x &= ~1;
#endif
    this->y = y < 0 ? (-y) - (layout->height + 4) : y;
    this->backup = new VideoCopy(layout->width + 6, layout->height + 4);
    driver->copy_chars(*backup, this->x, this->y, backup->width(), backup->height(), 0, 0);
    this->e_pos = 0;
    this->shifted = false;

    draw(true);
}

void OnScreenKeyboard::update() {
    key_pressed = 0;
    
    if (!opened()) return;

	int delta_x = 0;
	int delta_y = 0;
	int i, dist, min_dist, min_pos;
    int old_e_pos = e_pos;
	const OSKEntry *entry;

	if (driver->joy_button_pressed(JoyButtonB, false)) {
		key_pressed = KeyEscape;
		return;
	} else if (driver->joy_button_pressed(JoyButtonX, false)) {
		key_pressed = KeyBackspace;
		return;
	} else if (driver->joy_button_pressed(JoyButtonY, false)) {
		shifted = !shifted;
		draw(false);
		return;
	}

	entry = &(layout->entries[e_pos]);

	if (driver->joy_button_pressed(JoyButtonA, false)) {
		if (entry->char_regular == OSK_SPECIAL_SHIFT) {
			shifted = !shifted;
			old_e_pos = -1; // force draw
		} else {
            key_pressed = (shifted && (entry->char_regular >= 32 && entry->char_regular < 128)) ? entry->char_shifted : entry->char_regular;
		}
	}

	delta_x = driver->deltaX;
	delta_y = driver->deltaY;

	if (delta_y != 0) {
		min_dist = (1 << 30);
		min_pos = -1;
		for (i = 0; i < layout->entry_count; i++) {
			if (i == e_pos) {
				continue;
			}
        	const OSKEntry *i_entry = &(layout->entries[i]);
			if (Signum(i_entry->y - entry->y) == Signum(delta_y))
			{
				dist = (Abs(i_entry->y - entry->y) << 2) + Abs(i_entry->x - entry->x);
				if (!((Signum(i_entry->x - entry->x) == Signum(delta_y) || entry->x == i_entry->x))) {
					// put all wrong-direction options after right-direction options
					dist += 10000;
				}
				if (dist < min_dist) {
					min_dist = dist;
					min_pos = i;
				}
			}
		}
		if (min_pos >= 0) {
			e_pos = min_pos;
		} 
	}
	
	if (delta_x != 0) {
		min_dist = (1 << 30);
		min_pos = -1;
		for (i = 0; i < layout->entry_count; i++) {
			if (i == e_pos) {
				continue;
			}
        	const OSKEntry *i_entry = &(layout->entries[i]);
			if (i_entry->y == entry->y && Signum(i_entry->x - entry->x) == Signum(delta_x)) {
				dist = Abs(i_entry->x - entry->x);
				if (dist < min_dist) {
					min_dist = dist;
					min_pos = i;
				}
			}
		}
		if (min_pos >= 0) {
			e_pos = min_pos;
		} 
	}

	if (e_pos != old_e_pos) {
        draw(false);
	}
}

void OnScreenKeyboard::close() {
    if (opened()) {
        driver->paste_chars(*backup, 0, 0, backup->width(), backup->height(), x, y);
        delete backup;
		backup = nullptr;
    }
}
