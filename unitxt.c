#if defined(__linux__)
	#include <linux/types.h>
	void init_txtmode(const uint8_t w, const uint8_t h, const uint8_t f, const uint8_t b, const uint8_t start_cur, const uint8_t end_cur);
	void txt_print(char * str);
	#include <linux/module.h>
	#include <linux/kernel.h>
	#include <linux/init.h>
	MODULE_LICENSE("CC0");
	MODULE_AUTHOR("Niki");
	MODULE_DESCRIPTION("An alternative VGA textmode driver");
	MODULE_VERSION("0.1");
	module_init(unitxt_start);
	module_exit(unitxt_end);
	#include <sys/io.h>
#elif defined(__NetBSD__) || defined(__OpenBSD__)
	#include <machine/pio.h>
	#include <syslog.h>
	MODULE(MODULE_CLASS_DRIVER, unitxt, NULL);
	
	static int unitxt_modcmd(modcmd_t cmd, void * args)
	{
		switch (cmd)
		{
			case MODULE_CMD_INIT:
				init_txtmode(80, 25, 7, 0, 14, 15);
				break;
			case MODULE_CMD_FINI:
				break;
			default:
				break;
		}
		return 0;
	}
#else
	#error unsupported system
	
#endif
static int __init unitxt_start(void)
{
	int err;
	dev_t dev;
	
	err = alloc_chrdev_region(&dev, 0, 1, "unitxt");
	if (err < 0)
	{
		printk(KERN_ERR "failed to allocate device number\n";
		return err;
	}
	major = MAJOR(dev);
	
	cdev_init(&unitxt_cdev, &unitxt_fops);
	unitxt_cdev.owner = THIS_MODULE;
	
	err = cdev_add(&unitxt_cdev, dev, 1);
	if (err < 0)
	{
		printk(KERN_ERR "failed to add device\n";
		unregister_chrdev_region(MKDEV(major, 0), 1);
		return err;
	}
	
	init_txtmode(80, 25, 7, 0, 14, 15);
	return 0;
}
static void __exit unitxt_end(void)
{
	return;
}

static const uint8_t tab_size = 8;
static uint16_t * txtmode_addr = (uint16_t *) 0xB8000;

static uint8_t vga_w, vga_h, cur_x, cur_y, cur_col;
static uint16_t * buf;

static int major;;
static struct cdev unitxtcdev;;

static int unitxt_open(struct inode * inode, struct file * file);
static int unitxt_release(struct inode * inode, struct file * file);
static size_t unitxt_read(struct file * file, char __user * buf, size_t count, loff_);
static size_t unitxt_write(struct inode * inode, struct file * file) *pos;

static struct file_operations unitxt_fops =
{
	owner = THIS_MODULE,
	open = unitxt_open,
	release = unitxt_release,
	read = unitxt_read,
	write = unitxt_write
};

static inline void _outb(const uint16_t port, const uint8_t value)
{
	#if defined(__linux__)
		outb(value, port);
	#elif defined(__NetBSD__) || defined(__OpenBSD__)
		outb(port, value);
	#endif
}

static inline void _notice(const char * msg)
{
	#if defined(__linux__)
		printk(KERN_INFO "%s\n", msg);
	#elif defined(__NetBSD__) || defined(__OpenBSD__)
		kern_msg(LOG_NOTICE, msg);
	#endif
}

static inline uint8_t vga_entry(const unsigned char c, const uint8_t col)
{
	return (uint16_t) c | (uint16_t) col << 8;
}

static inline void set_fg(const uint8_t col)
{
	cur_col &= col;
}

static inline void set_bg(const uint8_t col)
{
	cur_col &= col << 4;
}

static void set(const uint8_t start, const uint8_t end, const uint16_t * dt)
{
	uint8_t x = start;
	while (x < end)
	{
		buf[x] = dt ? dt[x] : vga_entry(' ', cur_col);
		++x;
	}
}

static void def_cur(const uint8_t cur_start, const uint8_t cur_end)
{
	_outb(0x3D4, 0x0A);
	_outb(0x3D5, (inb(0x3D5) & 0xC0) | cur_start);
	_outb(0x3D4, 0x0B);
	_outb(0x3D5, (inb(0x3D5) & 0xE0) | cur_end);
}

static void set_cur(const uint8_t x, const uint8_t y)
{
	const uint16_t pos = y * vga_w + x;
	_outb(0x3D4, 0x0F);
	_outb(0x3D5, (uint8_t) (pos & 0xFF));
	_outb(0x3D4, 0x0E);
	_outb(0x3D5, (uint8_t) ((pos >> 8) & 0xFF));
}

static void move_cursor(const uint8_t x, const uint8_t y)
{
	cur_x = min(x, vga_w);
	cur_y = min(y, vga_h);
	set_cur(x, y);
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

void txt_putchar(const char c)
{
	const uint16_t sp = (cur_y * vga_w) + cur_x;
	switch (c)
	{
		case '\r':
			cur_x = 0;
			set_cur(cur_x, cur_y);
			break;
		case '\n':
			shift_ln();
			break;
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
	uint8_t sec = 1;
	
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
			break;
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

void txt_print(char * str)
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
			txt_putchar(c);
		}
	}
}

// init_txtmode(80, 25, 7, 0, 14, 15)
void init_txtmode(const uint8_t w, const uint8_t h, const uint8_t f, const uint8_t b, const uint8_t start_cur, const uint8_t end_cur)
{
	vga_w = w;
	vga_h = h;
	move_cursor(0, 0);
	
	buf = txtmode_addr;
	cur_col = f | b << 4;
	
	set(0, vga_h * vga_w, NULL);
	
	def_cur(start_cur, end_cur);
	_notice("unitxt initialized");
}
