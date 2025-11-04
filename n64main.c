#include <libdragon.h>
#include <math.h>

#include "celeste.h"
#include "tilemap.h"

int pico8emu(CELESTE_P8_CALLBACK_TYPE call, ...);

/*
uint32_t base_palette[16] = {
	0x000000FF, 0x1D2B53FF, 0x7E2553FF, 0x008751FF, 0xAB5236FF, 0x5F574FFF, 0xC2C3C7FF, 0xFFF1E8FF,
	0xFF004DFF, 0xFFA300FF, 0xFFEC27FF, 0x00E436FF, 0x29ADFFFF, 0x83769CFF, 0xFF77A8FF, 0xFFCCAAFF,
};
*/

uint8_t test_tas[] = {
#include "test-tas.txt"
};
int tas_frame = 0;
bool play_tas = false;

uint16_t base_palette[16] = {
	0x0000,
	0x1955,
	0x7915,
	0x0415,
	0xaa8d,
	0x5a93,
	0xc631,
	0xffbb,
	0xf813,
	0xfd01,
	0xff49,
	0x070d,
	0x2d7f,
	0x83a7,
	0xfbab,
	0xfe6b,
};
__attribute__((aligned(8)))
static uint16_t base_palette_for_rsp[64];

// used when drawing filled rectangles or text, but not the CI4 textures
__attribute__((aligned(16)))
static uint16_t cur_palette[16];

static surface_t p8_fb;
static surface_t gfx_tex;
static surface_t font_tex;

static int8_t loaded_texture = -1;

static float scale = 1.0f;
static float scaling = 0.0f;

enum DrawMode {
	DRAW_MODE_COPY_TLUT,
	DRAW_MODE_STD_TLUT,
	DRAW_MODE_FILL,
	DRAW_MODE_FLAT,
	DRAW_MODE_TEXT
};

void set_draw_mode(const int mode) {
	static int cur_mode = -1;
	if (cur_mode == mode) return;
	cur_mode = mode;
	if (mode == -1) return;

	switch (mode) {
	case DRAW_MODE_COPY_TLUT:
		rdpq_set_mode_copy(true);
		rdpq_mode_tlut(TLUT_RGBA16);
		break;
	case DRAW_MODE_STD_TLUT:
		rdpq_set_mode_standard();
		rdpq_mode_alphacompare(1);
		rdpq_mode_tlut(TLUT_RGBA16);
		break;
	case DRAW_MODE_FILL:
		rdpq_set_mode_fill(RGBA32(0,0,0,0));
		break;
	case DRAW_MODE_FLAT:
		rdpq_set_mode_standard();
		rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
		break;
	case DRAW_MODE_TEXT:
		rdpq_set_mode_standard();
		rdpq_mode_combiner(RDPQ_COMBINER_TEX_FLAT);
		rdpq_mode_alphacompare(1);
		rdpq_mode_tlut(TLUT_NONE);
		break;
	default:
		assert(false);
		break;
	}
}

void draw() {
	rdpq_attach(&p8_fb, NULL);

	loaded_texture = -1;
	set_draw_mode(-1);
	Celeste_P8_draw();
	rdpq_detach_wait();

	rdpq_attach_clear(display_get(), NULL);
	rdpq_set_mode_standard();

	rdpq_blitparms_t p = { 0 };

	if (scaling > 0.01f || scaling < -0.01f) {
		scale += scaling;
		if (scale >= 2.0f) {
			scale = 2.0f;
			scaling = 0.0f;
		} else if (scale <= 1.0f) {
			scale = 1.0f;
			scaling = 0.0f;
		}
		p.filtering = true;
	}

	float centerx = (float) display_get_width() / 2;
	float centery = (float) display_get_height() / 2;
	float scaled = (128.f / 2.f) * scale;

	p.scale_x = scale;
	p.scale_y = scale;

	rdpq_tex_blit(&p8_fb, centerx - scaled, centery - scaled, &p);
	rdpq_detach_show();
}

static int buttons_state = 0;

static xm64player_t music[5] = {0};

struct sfx_file {
	int index;
	wav64_t wav;
};

static const int sfx_indices[] = {
	0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 13, 14, 15, 16, 23, 35, 37, 38, 40, 50, 51, 54, 55
};
static const int num_sounds = sizeof(sfx_indices) / sizeof(sfx_indices[0]);
static struct sfx_file sounds[(sizeof(sfx_indices) / sizeof(sfx_indices[0])) + 1] = { 0 };
static uint8_t sfx_channel = 0;

int main(int argc, char** argv) {
	display_init(RESOLUTION_320x240, DEPTH_16_BPP, 2, GAMMA_NONE, FILTERS_DISABLED);

	debug_init_isviewer();
	dfs_init(DFS_DEFAULT_LOCATION);
	rdpq_init();
	rdpq_debug_start();
	joypad_init();
	audio_init(44100, 4);
	mixer_init(32);
	wav64_init_compression(3);

	/*
	for (int i = 0; i < 5; i++) {
		char fname[32] = {0};
		snprintf(fname, 32, "rom://mus%d.xm64", i * 10);
		xm64player_open(&music[i], fname);
	}
	xm64player_play(&music[0], 0);
	*/

	for (int i = 0; i < num_sounds; i++) {
		char fname[32];
		snprintf(fname, 32, "rom://snd%d.wav64", sfx_indices[i]);
		sounds[i].index = sfx_indices[i];
		wav64_open(&sounds[i].wav, fname);
	}
	sounds[num_sounds].index = -1;

	// this is a bit janky
	// when uploading a TLUT, the address given needs to be aligned to 8 bytes
	// but since we're patching individual colors, we need an 8-byte-aligned copy of each base color
	for (int i = 0; i < 16; i++) {
		base_palette_for_rsp[i*4] = base_palette[i];
	}

	Celeste_P8_set_call_func(pico8emu);

	if (play_tas)
		Celeste_P8_set_rndseed(0);
	else
		Celeste_P8_set_rndseed(rand());

	memcpy(cur_palette, base_palette, 16 * sizeof(uint16_t));
	Celeste_P8_init();

	p8_fb = surface_alloc(FMT_RGBA16, 128, 128);

	gfx_tex = surface_alloc(FMT_CI4, 128, 64);
	FILE* gfxbin = fopen("rom://gfx.bin", "r");
	fread(gfx_tex.buffer, (gfx_tex.width * gfx_tex.height) / 2, 1, gfxbin);
	fclose(gfxbin);

	font_tex = surface_alloc(FMT_IA4, 128, 64);
	FILE* fontbin = fopen("rom://font.bin", "r");
	fseek(fontbin, 64 * 16, SEEK_SET);
	fread(font_tex.buffer, (font_tex.width * font_tex.height) / 2, 1, fontbin);
	fclose(fontbin);

	display_set_fps_limit(30);

	while (1) {
		joypad_poll();
		joypad_buttons_t btns = joypad_get_buttons(JOYPAD_PORT_1);
		if (play_tas) {
			static int t = 0;
			t++;
			if (t == 1) buttons_state = 1<<4;
			else if (t > 80) {
				if (tas_frame < sizeof(test_tas))
					buttons_state = test_tas[tas_frame++];
				else
					buttons_state = 0;
			} else buttons_state = 0;
		} else {
			buttons_state = 0;
			buttons_state |= ((btns.d_left & 1) << 0);
			buttons_state |= ((btns.d_right & 1) << 1);
			buttons_state |= ((btns.d_up & 1) << 2);
			buttons_state |= ((btns.d_down & 1) << 3);
			buttons_state |= ((btns.a & 1) << 4);
			buttons_state |= ((btns.b & 1) << 5);

			if (joypad_get_buttons_pressed(JOYPAD_PORT_1).z) {
				scaling = scale > 1.5f ? -0.1f : 0.1f;
			}
		}

		Celeste_P8_update();

		draw();
		mixer_try_play();
	}

	return 0;
}

bool enable_screenshake = true;

static int gettileflag(int tile, int flag) {
	return tile < sizeof(tile_flags)/sizeof(*tile_flags) && (tile_flags[tile] & (1 << flag)) != 0;
}

static int camera_x = 0, camera_y = 0;

void draw_tile(uint16_t tile, int16_t x, int16_t y, bool flip_x, bool flip_y) {
	int s = 8*(tile % 16);
	int t = 8*(tile / 16);

	rdpq_texparms_t p = { 0 };

	if (s > 63 && loaded_texture != 1) {
		rdpq_tex_upload_sub(0, &gfx_tex, &p, 64, 0, 128, 64);
		loaded_texture = 1;
	} else if (s < 64 && loaded_texture != 0) {
		rdpq_tex_upload_sub(0, &gfx_tex, &p, 0, 0, 64, 64);
		loaded_texture = 0;
	}

	// horizontally flipped texture rectangles don't work in copy mode
	if (flip_x) {
		set_draw_mode(DRAW_MODE_STD_TLUT);
		rdpq_texture_rectangle( 0, x + 8, y + (flip_y ? 8 : 0), x, y + (flip_y ? 0 : 8), s, t);
	} else {
		set_draw_mode(DRAW_MODE_COPY_TLUT);
		rdpq_texture_rectangle( 0, x, y + (flip_y ? 8 : 0), x + 8, y + (flip_y ? 0 : 8), s, t);
	}
}

void draw_line(color_t color, int x0, int y0, int x1, int y1) {
	set_draw_mode(DRAW_MODE_FLAT);
	rdpq_mode_antialias(AA_REDUCED);
	rdpq_set_prim_color(color);

	float vertices[] = {
		(float) x0, (float) y0,
		(float) x1, (float) y1,
		(float) x0 + 0.25, (float) y0,
		(float) x1 + 0.25, (float) y1
	};

	rdpq_triangle(&TRIFMT_FILL, &vertices[0], &vertices[2], &vertices[4]);
	rdpq_triangle(&TRIFMT_FILL, &vertices[2], &vertices[0], &vertices[6]);
}

color_t rgba16_to_rgba32(uint16_t in) {
	color_t out = {
		((in >> 11) & 0x1F) << 3,
		((in >> 6) & 0x1F) << 3,
		((in >> 1) & 0x1F) << 3,
		((in >> 0) & 0x1) > 0 ? 0xFF : 0x00,
	};
	return out;
}

static void p8_print(const char* str, int x, int y, int col) {
	set_draw_mode(DRAW_MODE_TEXT);
	color_t color = rgba16_to_rgba32(cur_palette[col]);
	color.a = 0xFF;
	rdpq_set_prim_color(color);

	if (loaded_texture != 2) {
		rdpq_texparms_t p = { 0 };
		rdpq_tex_upload(0, &font_tex, &p);
		loaded_texture = 2;
	}

	if (strcmp(str, "x+c") == 0)
		str = "a+b";

	for (char c = *str; c; c = *(++str)) {
		c &= 0x7F;

		int s = 8 * (c % 16);
		int t = (8 * (c / 16)) - 16;

		rdpq_texture_rectangle(0, x, y, x + 4, y + 8, s, t);

		x += 4;
	}
}

int pico8emu(CELESTE_P8_CALLBACK_TYPE call, ...) {
	if (!enable_screenshake) {
		camera_x = camera_y = 0;
	}

	va_list args;
	int ret = 0;
	va_start(args, call);

	#define   INT_ARG() va_arg(args, int)
	#define  BOOL_ARG() (Celeste_P8_bool_t)va_arg(args, int)
	#define RET_INT(_i)   do {ret = (_i); goto end;} while (0)
	#define RET_BOOL(_b) RET_INT(!!(_b))

	switch (call) {
		case CELESTE_P8_MUSIC: { //music(idx,fade,mask)
			int index = INT_ARG();
			int fade = INT_ARG();
			int mask = INT_ARG();

			(void) fade;

			(void)mask; //we do not care about this since sdl mixer keeps sounds and music separate

			if (index == -1) { //stop playing
				//if (music.ctx != NULL)
					//xm64player_close(&music);
			} else {
				//xm64player_open(&music, "rom://mus10.xm64");
				//xm64player_play(&music, 0);
			}
		} break;
		case CELESTE_P8_SPR: { //spr(sprite,x,y,cols,rows,flipx,flipy)
			int sprite = INT_ARG();
			int x = INT_ARG();
			int y = INT_ARG();
			int cols = INT_ARG();
			int rows = INT_ARG();
			int flipx = BOOL_ARG();
			int flipy = BOOL_ARG();

			for (int i = 0; i < rows; i++) {
				for (int j = 0; j < cols; j++) {
					int s = sprite + (i * 16) + j;
					if (s >= 0) {
						draw_tile(s, x + j * 8, y + i * 8, flipx, flipy);
					}
				}
			}
		} break;
		case CELESTE_P8_BTN: { //btn(b)
			int b = INT_ARG();
			assert(b >= 0 && b <= 5);
			RET_BOOL(buttons_state & (1 << b));
		} break;
		case CELESTE_P8_SFX: { //sfx(id)
			int id = INT_ARG();

			for (int i = 0; i < num_sounds; i++) {
				if (sounds[i].index == id) {
					wav64_play(&sounds[i].wav, sfx_channel);
					sfx_channel++;
					sfx_channel %= 4;
					break;
				}
			}
		} break;
		case CELESTE_P8_PAL: { //pal(a,b)
			int a = INT_ARG();
			int b = INT_ARG();
			if (a >= 0 && a < 16 && b >= 0 && b < 16) {
				//swap palette colors
				rdpq_tex_upload_tlut(&base_palette_for_rsp[b*4], a, 1);
				cur_palette[a] = base_palette[b];
			}
		} break;
		case CELESTE_P8_PAL_RESET: { //pal()
			rdpq_tex_upload_tlut(base_palette, 0, 16);
			memcpy(cur_palette, base_palette, 16 * sizeof(uint16_t));
		} break;
		case CELESTE_P8_CIRCFILL: { //circfill(x,y,r,col)
			int cx = INT_ARG() - camera_x;
			int cy = INT_ARG() - camera_y;
			int r = INT_ARG();
			int col = INT_ARG();

			set_draw_mode(DRAW_MODE_FILL);
			rdpq_set_fill_color(rgba16_to_rgba32(cur_palette[col]));

			if (r == 1) {
				rdpq_draw_pixel(cx, cy);
			} else if (r == 2) {
				rdpq_fill_rectangle(cx, cy - 1, cx + 1, cy + 2);
				rdpq_fill_rectangle(cx - 1, cy, cx + 2, cy + 1);
			} else {
				debugf("circfill(cx: %d, cy: %d, r: %d, col: %d)\n", cx, cy, r, col);
			}
		} break;
		case CELESTE_P8_PRINT: { //print(str,x,y,col)
			const char* str = va_arg(args, const char*);
			int x = INT_ARG() - camera_x;
			int y = INT_ARG() - camera_y;
			int col = INT_ARG() % 16;

			(void)str;
			(void)x;
			(void)y;
			(void)col;

			p8_print(str,x,y,col);
		} break;
		case CELESTE_P8_RECTFILL: { //rectfill(x0,y0,x1,y1,col)
			int x0 = INT_ARG() - camera_x;
			int y0 = INT_ARG() - camera_y;
			int x1 = INT_ARG() - camera_x;
			int y1 = INT_ARG() - camera_y;
			int col = INT_ARG();

			set_draw_mode(DRAW_MODE_FILL);
			rdpq_set_fill_color(rgba16_to_rgba32(cur_palette[col]));
			rdpq_fill_rectangle(x0, y0, x1, y1);
		} break;
		case CELESTE_P8_LINE: { //line(x0,y0,x1,y1,col)
			int x0 = INT_ARG() - camera_x;
			int y0 = INT_ARG() - camera_y;
			int x1 = INT_ARG() - camera_x;
			int y1 = INT_ARG() - camera_y;
			int col = INT_ARG();

			draw_line(rgba16_to_rgba32(cur_palette[col]), x0, y0, x1, y1);
		} break;
		case CELESTE_P8_MGET: { //mget(tx,ty)
			int tx = INT_ARG();
			int ty = INT_ARG();

			RET_INT(tilemap_data[tx+ty*128]);
		} break;
		case CELESTE_P8_CAMERA: { //camera(x,y)
			if (enable_screenshake) {
				camera_x = INT_ARG();
				camera_y = INT_ARG();
			}
		} break;
		case CELESTE_P8_FGET: { //fget(tile,flag)
			int tile = INT_ARG();
			int flag = INT_ARG();

			RET_INT(gettileflag(tile, flag));
		} break;
		case CELESTE_P8_MAP: { //map(mx,my,tx,ty,mw,mh,mask)
			int mx = INT_ARG(), my = INT_ARG();
			int tx = INT_ARG(), ty = INT_ARG();
			int mw = INT_ARG(), mh = INT_ARG();
			int mask = INT_ARG();

			for (int s = 0; s < 2; s++) {
				for (int x = 0; x < mw; x++) {
					for (int y = 0; y < mh; y++) {
						int tile = tilemap_data[x + mx + (y + my)*128];
						if ((tile % 16) > 7 && s == 1) continue;
						if ((tile % 16) < 8 && s == 0) continue;

						if (mask != 0 && (tile_flags[tile] & mask) != mask) continue;

						draw_tile(tile, tx+x*8 - camera_x, ty+y*8 - camera_y, false, false);
					}
				}
			}

		} break;
	}

	end:
	va_end(args);
	return ret;
}
