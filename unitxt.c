#include <stddef.h>
#include <stdint.h>

void init_txtmode(const uint8_t w, const uint8_t h, const uint8_t f, const uint8_t b, const uint8_t start_cur, const uint8_t end_cur);

oeo ;
#if defined(__linux__)
	#include <linux/module.h>
	#include <linux/kernel.h>
	#include <linux/init.h>
	MODULE_LICENSE("CC0");
	MODULE_AUTHOR("Niki");
	MODULE_DESCRIPTION("An alternative VGA textmode driver");
	MODULE_VERSION("0.1");
	module_init(init_txtmode);
	//module_exit()
	#include <sys/io.h>
#elif defined(__NetBSD__)
	#include <sys/bus.h>
#else
	#error unsupported system
#endif

static const uint8_t tab_size = 8;
static const uint16_t txtmode_addr = 0xB8000;

static uint8_t vga_w, vga_h, cur_x, cur_y, cur_col;
static uint16_t * buf;

static inline void _outb(const uint8_t value, const uint16_t port)
{
	#if defined(__linux__)
		outb(value, port);
	#elif defined(__NetBSD__)
		bus_space_handle_t port_handle;
		bus_space_map(port, 1, BUS_SPACE_MAP_LINEAR, &port_handle);
		bus_space_write_1(port_handle, 0, data);
		bus_space_unmap(port_handle, 1);
	#endif
}

static inline uint8_t vga_entry(const unsigned char c, const uint8_t col)
{
	return (uint16_t) c | (uint16_t) col << 8;
}

static inline uint8_t max(const uint8_t in, const uint8_t max)
{
	return in < max ? in : max;
}

static void set(const uint8_t start, const uint8_t end, const uint16_t * dt)
{
	uint8_t x = start;
	while (x < end)
	{
		buf[ln][x] = dt ? dt[x] : vga_entry(' ', cur_col);
		++x;
	}
}

static void def_cur(const uint8_t cur_start, const uint8_t cur_end) {
{
	outb(0x3D4, 0x0A);
	outb(0x3D5, (inb(0x3D5) & 0xC0) | cur_start);
	outb(0x3D4, 0x0B);
	outb(0x3D5, (inb(0x3D5) & 0xE0) | cur_end);
}

static void set_cur(const uint8_t x, const uint8_t y)
{
	const uint16_t pos = y * vga_w + x;
	outb(0x3D4, 0x0F);
	outb(0x3D5, (uint8_t) (pos & 0xFF));
	outb(0x3D4, 0x0E);
	outb(0x3D5, (uint8_t) ((pos >> 8) & 0xFF));
}

void move_cursor(const uint8_t x, const uint8_t y)
{
	cur_x = max(x, vga_w);
	cur_y = max(y, vga_h);
	set_cur(x, y);
}

// init_txtmode(80, 25, 7, 0, 14, 15)
void init_txtmode(const uint8_t w, const uint8_t h, const uint8_t f, const uint8_t b, const uint8_t start_cur, const uint8_t end_cur)
{
	vga_w = w;
	vga_h = h;
	move_cursor(0, 0);
	
	buf = (uint16_t *) txtmode_addr;
	cur_col = fg | bg << 4;
	
	set(0, vga_h * vga_w, NULL);
	
	def_cur(start_cur, end_cur);
}

static void shift_ln(void)
{
	if (cur_y + 1 == vga_h)
	{
		set(0, vga_w * (vga_h - 1), &buf[vga_w]);
	}
	else
	{
		cur_y++;
	}
	cur_x = 0;
}

void putchar(const char c)
{
	const uint16_t sp = (cur_y * vga_w) + cur_x;
	switch (c)
	{
		case '\r':
			cur_x = 0;
			set_cur(cur_x, cur_y);
		case '\n':
			shift_ln();
		case '\0':
			break;
		case '\b':
			if (cur_x != 0)
			{
				--cur_x;
				set(sp - 1, sp, NULL);
				set_cur(cur_x, cur_y);
			}
			break;
		case '\t':
			if (cur_x + tab_size < vga_w)
			{
				cur_x += tab_size;
			}
			else
			{
				shift_ln();
			}
			break;
		default:
			const uint16_t ent = vga_entry(c, cur_col);
			set(sp, sp + 1, &ent);
	}
}

void set_fg_color(const uint8_t col)
{
	cur_col &= col;
}

void set_bg_color(const uint8_t col)
{
	cur_col &= col << 4;
}

static uint64_t read_num(char ** cp, uint8_t *num)
{
	uint64_t f = 0;
	while (**cp >= '0' && **cp <= '9')
	{
		*num = *num * 10 + (**cp - '0');
		++*cp;
		++f;
	}
	return f;
}

static uint64_t ansi(char * cp)
{
	uint64_t mv  = 0;
	uint8_t  cou = 1;
	
	mv += read_num(&cp, &cou);
	
	switch (*cp)
	{
		case 'A':
			if (cou < cur_y)
			{
				cou = cur_y;
			}
			move_cursor(cur_x, cur_y - cou);
			break;
		case 'B':
			move_cursor(cur_x, cur_y + cou);
			break;
		case 'C':
			move_cursor(cur_x + cou, cur_y);
			break;
		case 'D':
			move_cursor(cur_x - cou, cur_y);
			break;
		case 'E':
			move_cursor(0, cur_y + cou);
			break;
		case 'F':
			move_cursor(0, cur_y - cou);
			break;
		case 'G':
			move_cursor(cou, cur_y);
			break;
		case ';':
			++cp;
			uint8_t sec = 1;
			read_num(&cp, &sec);
			if (*cp == 'H' || *cp == 'f')
			{
				move_cursor(sec, cou);
			}
			break;
		case 'J':
			switch (cou)
			{
				case 0:
					set((vga_w * cur_y) + cur_x, vga_h * vga_w, NULL);
					break;
				case 1:
					set(0, (vga_w * cur_y) + cur_x, NULL);
					break;
				case 2:
				case 3:
					set(0, vga_w * vga_h, NULL);
					break;
			}
			break;
		case 'K':
			switch (cou)
			{
				case 0:
					set((vga_w * cur_y) + cur_x, vga_w * (cur_y + 1), NULL);
					break;
				case 1:
					set(vga_w * cur_y, (vga_w * cur_y) + cur_x, NULL);
					break;
			}
		case 'S':
		case 'T':
			// undefined - this driver does not have a scrollback buffer
			break;
		case 'm':
			if (cou >= 30 && cou <= 37)
			{
				set_fg(cou - 30);
			}
			else if (cou >= 90 && cou <= 97)
			{
				set_fg((cou - 90) + 8);
			}
			else if (cou >= 40 && cou <= 47)
			{
				set_bg(cou - 40);
			}
			else if (cou >= 100 && cou <= 107)
			{
				set_bg((cou - 100) + 8);
			}
	}
	return mv;
}

void print(const char * str)
{
	uint64_t c = 0;
	uint8_t flags = 0x00;
	while (str[c] != '\0')
	{
		if (str[c] == '\033')
		{
			flags |= 1 << 1;
			continue;
		}
		else if (flags & 0x01 << 1 && str[c] == '[')
		{
			flags &= ~(0x01 << 1);
			flags |= 1 << 2;
		}
		else if (flags & 0x01 << 2)
		{
			c += ansi(&str[c]);
		}
		else
		{
			putchar(c);
		}
	}
}
