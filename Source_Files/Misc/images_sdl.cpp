/*
 *  images_sdl.cpp - Image management, SDL implementation (included by images.cpp)
 *
 *  Written in 2000 by Christian Bauer
 */

#include <SDL/SDL_endian.h>

#include "byte_swapping.h"
#include "screen_drawing.h"


// From screen_sdl.cpp
extern short interface_bit_depth;

// From screen_drawing_sdl.cpp
extern bool draw_clip_rect_active;
extern screen_rectangle draw_clip_rect;


/*
 *  Uncompress picture data
 */

// Uncompress (and endian-correct) scan line compressed by PackBits RLE algorithm
template <class T>
static const uint8 *unpack_bits(const uint8 *src, int row_bytes, T *dst)
{
	// Read source count
	int src_count;
	if (row_bytes > 250) {
		src_count = (src[0] << 8) | src[1];
		src += 2;
	} else
		src_count = *src++;

	while (src_count > 0) {

		// Read flag/count byte
		int c = (int8)*src++;
		src_count--;
		if (c < 0) {

			// RLE compressed run
			int size = -c + 1;
			T data;
			if (sizeof(T) == 1) {
				data = *src++;
				src_count--;
			} else {
				data = (src[0] << 8) | src[1];
				src += 2;
				src_count -= 2;
			}
			for (int i=0; i<size; i++)
				*dst++ = data;

		} else {

			// Uncompressed run
			int size = c + 1;
			for (int i=0; i<size; i++) {
				T data;
				if (sizeof(T) == 1) {
					data = *src++;
					src_count--;
				} else {
					data = (src[0] << 8) | src[1];
					src += 2;
					src_count -= 2;
				}
				*dst++ = data;
			}
		}
	}
	return src;
}

// 8-bit picture, one scan line at a time
static void uncompress_rle8(const uint8 *src, int row_bytes, uint8 *dst, int dst_pitch, int height)
{
	for (int y=0; y<height; y++) {
		src = unpack_bits(src, row_bytes, dst);
		dst += dst_pitch;
	}
}

// 16-bit picture, one scan line at a time, 16-bit chunks
static void uncompress_rle16(const uint8 *src, int row_bytes, uint8 *dst, int dst_pitch, int height)
{
	for (int y=0; y<height; y++) {
		src = unpack_bits(src, row_bytes, (uint16 *)dst);
		dst += dst_pitch;
	}
}

static void copy_component_into_surface(const uint8 *src, uint8 *dst, int count, int component)
{
#ifdef LITTLE_ENDIAN
	dst += 2 - component;
#else
	dst += component + 1;
#endif
	while (count--) {
		*dst = *src++;
		dst += 4;
	}
}

// 32-bit picture, one scan line, one component at a time
static void uncompress_rle32(const uint8 *src, int row_bytes, uint8 *dst, int dst_pitch, int height)
{
	uint8 *tmp = (uint8 *)malloc(row_bytes);
	if (tmp == NULL)
		return;

	int width = row_bytes / 4; 
	for (int y=0; y<height; y++) {
		src = unpack_bits(src, row_bytes, tmp);

		// "tmp" now contains "width" bytes of red, followed by "width"
		// bytes of green and "width" bytes of blue, so we have to copy them
		// into the surface in the right order
		copy_component_into_surface(tmp, dst, width, 0);
		copy_component_into_surface(tmp + width, dst, width, 1);
		copy_component_into_surface(tmp + width * 2, dst, width, 2);

		dst += dst_pitch;
	}
	free(tmp);
}

static void uncompress_picture(const uint8 *src, int row_bytes, uint8 *dst, int dst_pitch, int depth, int height, int pack_type)
{
	if (row_bytes < 8) {

		// Uncompressed data
		memcpy(dst, src, row_bytes * height);

	} else {

		// Compressed data
		if (depth == 8) {
			uncompress_rle8(src, row_bytes, dst, dst_pitch, height);
		} else {
			if (pack_type == 0) {
				if (depth == 16)
					pack_type = 3;
				else if (depth == 32)
					pack_type = 4;
			}
			switch (pack_type) {
				case 1:		// No packing
					memcpy(dst, src, row_bytes * height);
					if (depth == 16)
						byte_swap_memory(dst, _2byte, dst_pitch * height / 2);
					else if (depth == 32)
						byte_swap_memory(dst, _4byte, dst_pitch * height / 4);
					break;
				case 3:		// Run-length encoding by 16-bit chunks
					uncompress_rle16(src, row_bytes, dst, dst_pitch, height);
					break;
				case 4:		// Run-length encoding one component at a time
					uncompress_rle32(src, row_bytes, dst, dst_pitch, height);
					break;
				default:
					fprintf(stderr, "Unimplemented packing type %d in PICT resource\n", pack_type);
					exit(1);
					break;
			}
		}
	}
}


/*
 *  Convert picture resource to SDL surface
 */

SDL_Surface *picture_to_surface(LoadedResource &rsrc)
{
	if (!rsrc.IsLoaded())
		return NULL;

	SDL_Surface *s = NULL;

	// Open stream to picture resource
	SDL_RWops *p = SDL_RWFromMem(rsrc.GetPointer(), rsrc.GetLength());
	if (p == NULL)
		return NULL;
	SDL_RWseek(p, 10, SEEK_CUR);	// skip picSize and picRect

	// Read and parse picture opcodes
	bool done = false;
	while (!done) {
		uint16 opcode = SDL_ReadBE16(p);
		switch (opcode) {

			case 0x0000:	// NOP
			case 0x0011:	// VersionOp
			case 0x001e:	// DefHilite
			case 0x02ff:	// Version
				break;

			case 0x00a0:	// ShortComment
				SDL_RWseek(p, 2, SEEK_CUR);
				break;

			case 0x00a1: {	// LongComment
				SDL_RWseek(p, 2, SEEK_CUR);
				int size = SDL_ReadBE16(p);
				SDL_RWseek(p, size, SEEK_CUR);
				break;
			}

			case 0x0c00:	// HeaderOp
				SDL_RWseek(p, 24, SEEK_CUR);
				break;

			case 0x00ff:	// OpEndPic
				done = true;
				break;

			case 0x0001: {	// Clipping region
				uint16 size = SDL_ReadBE16(p);
				SDL_RWseek(p, size - 2, SEEK_CUR);
				break;
			}

			case 0x0098:	// Packed CopyBits with clipping rectangle
			case 0x009a: {
				// 1. PixMap
				if (opcode == 0x009a)
					SDL_RWseek(p, 4, SEEK_CUR);		// pmBaseAddr
				uint16 row_bytes = SDL_ReadBE16(p) & 0x3fff;	// the upper 2 bits are flags
				uint16 top = SDL_ReadBE16(p);
				uint16 left = SDL_ReadBE16(p);
				uint16 height = SDL_ReadBE16(p) - top;
				uint16 width = SDL_ReadBE16(p) - left;
				SDL_RWseek(p, 2, SEEK_CUR);			// pmVersion
				uint16 pack_type = SDL_ReadBE16(p);
				SDL_RWseek(p, 14, SEEK_CUR);		// packSize/hRes/vRes/pixelType
				uint16 pixel_size = SDL_ReadBE16(p);
				SDL_RWseek(p, 16, SEEK_CUR);		// cmpCount/cmpSize/planeBytes/pmTable/pmReserved
				//printf(" width %d, height %d, row_bytes %d, depth %d, pack_type %d\n", width, height, row_bytes, pixel_size, pack_type);

				// Allocate surface for picture
				uint32 Rmask, Gmask, Bmask;
				switch (pixel_size) {
					case 8:
						Rmask = Gmask = Bmask = 0xff;
						break;
					case 16:
						Rmask = 0x7c00;
						Gmask = 0x03e0;
						Bmask = 0x001f;
						break;
					case 32:
						Rmask = 0x00ff0000;
						Gmask = 0x0000ff00;
						Bmask = 0x000000ff;
						break;
				}
				s = SDL_CreateRGBSurface(SDL_SWSURFACE, width, height, pixel_size, Rmask, Gmask, Bmask, 0);
				if (s == NULL) {
					done = true;
					break;
				}

				// 2. ColorTable
				if (opcode == 0x0098) {
					SDL_Palette *pal = s->format->palette;
					SDL_RWseek(p, 6, SEEK_CUR);			// ctSeed/ctFlags
					int num_colors = SDL_ReadBE16(p) + 1;
					for (int i=0; i<num_colors; i++) {
						uint8 value = SDL_ReadBE16(p) & 0xff;
						pal->colors[value].r = SDL_ReadBE16(p) >> 8;
						pal->colors[value].g = SDL_ReadBE16(p) >> 8;
						pal->colors[value].b = SDL_ReadBE16(p) >> 8;
					}
				}

				// 3. source/destination Rect and transfer mode
				SDL_RWseek(p, 18, SEEK_CUR);

				// 4. graphics data
				uncompress_picture((uint8 *)rsrc.GetPointer() + SDL_RWtell(p), row_bytes, (uint8 *)s->pixels, s->pitch, pixel_size, height, pack_type);

				done = true;
				break;
			}

			default:
				fprintf(stderr, "Unimplemented opcode %04x in PICT resource\n", opcode);
				exit(1);
				break;
		}
	}

	// Close stream, return surface
	SDL_FreeRW(p);
	return s;
}


/*
 *  Draw picture resource centered on screen
 */

static void draw_picture(LoadedResource &rsrc)
{
	// Convert picture resource to surface, free resource
	SDL_Surface *s = picture_to_surface(rsrc);
	if (s == NULL)
		return;

	// Center picture on screen
	SDL_Rect dest_rect = {(SDL_GetVideoSurface()->w - s->w) / 2, (SDL_GetVideoSurface()->h - s->h) / 2, s->w, s->h};
	if (dest_rect.x < 0)
		dest_rect.x = 0;
	if (dest_rect.y < 0)
		dest_rect.y = 0;

	// Set clipping rectangle if desired
	if (draw_clip_rect_active)
		SDL_SetClipping(s, draw_clip_rect.top, draw_clip_rect.left, draw_clip_rect.bottom, draw_clip_rect.right);

	// Blit picture to screen
	SDL_BlitSurface(s, NULL, SDL_GetVideoSurface(), &dest_rect);

	// Reset clipping rectangle
	if (draw_clip_rect_active)
		SDL_SetClipping(s, 0, 0, 0, 0);

	// Update display and free picture surface
	SDL_UpdateRects(SDL_GetVideoSurface(), 1, &dest_rect);
	SDL_FreeSurface(s);
}


/*
 *  Get system color table
 */

const int NUM_SYS_COLORS = 8;

static rgb_color sys_colors[NUM_SYS_COLORS] = {
	{0x0000, 0x0000, 0x0000},
	{0xffff, 0x0000, 0x0000},
	{0x0000, 0xffff, 0x0000},
	{0xffff, 0xffff, 0x0000},
	{0x0000, 0x0000, 0xffff},
	{0xffff, 0x0000, 0xffff},
	{0x0000, 0xffff, 0xffff},
	{0xffff, 0xffff, 0xffff}
};

struct color_table *build_8bit_system_color_table(void)
{
	color_table *table = new color_table;
	table->color_count = NUM_SYS_COLORS;
	for (int i=0; i<NUM_SYS_COLORS; i++)
		table->colors[i] = sys_colors[i];
	return table;
}


/*
 *  Scroll image across screen
 */

#define SCROLLING_SPEED (MACHINE_TICKS_PER_SECOND / 20)

void scroll_full_screen_pict_resource_from_scenario(int pict_resource_number, boolean text_block)
{
	// Convert picture resource to surface, free resource
	LoadedResource rsrc;
	get_picture_resource_from_scenario(pict_resource_number, rsrc);
	SDL_Surface *s = picture_to_surface(rsrc);
	if (s == NULL)
		return;

	// Find out in which direction to scroll
	int picture_width = s->w;
	int picture_height = s->h;
	int screen_width = 640;
	int screen_height = 480;
	bool scroll_horizontal = picture_width > screen_width;
	bool scroll_vertical = picture_height > screen_height;

	if (scroll_horizontal || scroll_vertical) {

		// Flush events
		SDL_Event event;
		while (SDL_PollEvent(&event)) ;

		// Prepare source and destination rectangles
		SDL_Rect src_rect = {0, 0, scroll_horizontal ? screen_width : picture_width, scroll_vertical ? screen_height : picture_height};
		SDL_Rect dst_rect = {(SDL_GetVideoSurface()->w - screen_width) / 2, (SDL_GetVideoSurface()->h - screen_height) / 2, screen_width, screen_height};

		// Scroll loop
		bool done = false, aborted = false;
		uint32 start_tick = SDL_GetTicks();
		do {

			uint32 delta = (SDL_GetTicks() - start_tick) / (text_block ? (2 * SCROLLING_SPEED) : SCROLLING_SPEED);
			if (scroll_horizontal && delta > picture_width - screen_width) {
				delta = picture_width - screen_width;
				done = true;
			}
			if (scroll_vertical && delta > picture_height - screen_height) {
				delta = picture_height - screen_height;
				done = true;
			}

			// Blit part of picture
			src_rect.x = scroll_horizontal ? delta : 0;
			src_rect.y = scroll_vertical ? delta : 0;
			SDL_BlitSurface(s, &src_rect, SDL_GetVideoSurface(), &dst_rect);
			SDL_UpdateRects(SDL_GetVideoSurface(), 1, &dst_rect);

			// Give system time
			global_idle_proc();
			SDL_Delay(10);

			// Check for events to abort
			event.type = SDL_NOEVENT;
			SDL_PollEvent(&event);
			switch (event.type) {
				case SDL_MOUSEBUTTONDOWN:
				case SDL_KEYDOWN:
					aborted = true;
					break;
			}

		} while (!done && !aborted);
	}

	// Free surface
	SDL_FreeSurface(s);
}
