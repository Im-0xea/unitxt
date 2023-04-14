/*          *
 *  unitxt  *
 *          */


/* INCLUDES */
#if defined(__linux__)
	#include <linux/types.h>
	#include <linux/module.h>
	#include <linux/slab.h>
	#include <linux/kernel.h>
	#include <linux/init.h>
	#include <linux/fs.h>
	
	//#if defined(__arm__) || defined(__aarch64__)
	#include <asm/io.h>
	//#else
	//#include <sys/io.h>
	
	#define _INIT __init
	#define _EXIT __exit
#elif defined(__NetBSD__) || defined(__OpenBSD__)
	#include <sys/types.h>
	#include <sys/conf.h>
	#include <sys/device.h>
	#include <sys/kernel.h>
	#include <sys/systm.h>
	#include <sys/module.h>
	#include <syslog.h>
	//#if defined(__arm__) || defined(__aarch64__)
	//#else
	#include <machine/pio.h>
	
	#define _INIT
	#define _EXIT
#else
	#error unsupported system
#endif


/* CONSTANTS */
#define DEVICE_NAME  "unitxt"
static const uint8_t tab_size = 8;
static uint16_t *    txtmode_addr = (uint16_t *) 0xB8000;


/* GLOBALS */
static uint8_t    vga_w, vga_h, cur_x, cur_y, cur_col;
static uint16_t * buf;

static int        major;


/* PROTOTYPES */
static int  _INIT  unitxt_start(void);
static void _EXIT  unitxt_end(void);
static int         unitxt_init_chardev(void);
static int         unitxt_stop_chardev(void);
static void        unitxt_init_txtmode(const uint8_t, const uint8_t, const uint8_t, const uint8_t, const uint8_t, const uint8_t);
static void        txt_print(char *);
#if defined(__linux__)
	static int     unitxt_open(struct inode *, struct file *);
	static int     unitxt_close(struct inode *, struct file *);
	static ssize_t unitxt_read(struct file *, char __user *, size_t, loff_t *);
	static ssize_t unitxt_write(struct file *, const char __user *, size_t, loff_t *);
#elif defined(__NetBSD__) || defined(__OpenBSD__)
	static int     unitxt_open(dev_t, int, int, struct lwp *);
	static int     unitxt_close(dev_t, int, int, struct lwp *);
	static int     unitxt_write(dev_t, struct uio *, int);
	static int     unitxt_read(dev_t, struct uio *, int);
#endif
static void        txt_putchar(const char);
static void        txt_set(const uint8_t, const uint8_t, const uint16_t *);
static uint64_t    ansi_interpreter(char *);
static uint64_t    read_num(char **, uint8_t *);
static void        shift_ln(void);
static void        def_cur(const uint8_t, const uint8_t);
static void        set_cur(const uint8_t, const uint8_t);
static void        move_cursor(const uint8_t, const uint8_t);


/* MODULE SETUP */
#if defined(__linux__)
	MODULE_LICENSE     ("CC0");
	MODULE_AUTHOR      ("Niki");
	MODULE_DESCRIPTION ("An alternative VGA textmode driver");
	MODULE_VERSION     ("0.1");
	
	module_init(unitxt_start);
	module_exit(unitxt_end);
#elif defined(__NetBSD__) || defined(__OpenBSD__)
	MODULE(MODULE_CLASS_DRIVER, unitxt, NULL);
#endif


/* UNTILS */
static inline void p_outb(const uint16_t port, const uint8_t value)
{
	#if defined(__linux__)
		outb(value, port);
	#elif defined(__NetBSD__) || defined(__OpenBSD__)
		outb(port, value);
	#endif
}

static inline uint8_t p_inb(const uint16_t port)
{
	#if defined(__linux__)
		return 0;
		//return outb(value, port)
	#elif defined(__NetBSD__) || defined(__OpenBSD__)
		return outb(port, value);
	#endif
}

static void * p_malloc(const size_t size)
{
	#if defined(__linux__)
		return kmalloc(size, GFP_KERNEL);
	#elif defined(__NetBSD__) || defined(__OpenBSD__)
		return kmem_alloc(size, KM_SLEEP);
	#endif
}

static int p_ucopy(char * dest, char * src, size_t count)
{
	#if defined(__linux__)
		return copy_from_user(dest, src, count);
	#elif defined(__NetBSD__) || defined(__OpenBSD__)
		uio.move(dest, count);
	#endif
}

static inline void p_notice(const char * msg)
{
	#if defined(__linux__)
		printk(KERN_INFO "%s\n", msg);
	#elif defined(__NetBSD__) || defined(__OpenBSD__)
		kern_msg(LOG_NOTICE, msg);
	#endif
}

static inline void p_fail(const char * msg)
{
	#if defined(__linux__)
		printk(KERN_ERR "%s\n", msg);
	#elif defined(__NetBSD__) || defined(__OpenBSD__)
		kern_msg(LOG_ERR, msg);
	#endif
}


/* MODULE LOAD - UNLOAD */
#if defined(__NetBSD__) || defined(__OpenBSD__)
	static int unitxt_modcmd(modcmd_t cmd, void * args)
	{
		switch (cmd)
		{
			case MODULE_CMD_INIT:
				unitxt_start();
				break;
			case MODULE_CMD_FINI:
				unitxt_end();
				break;
			default:
				break;
		}
		return 0;
	}
#endif

static int _INIT unitxt_start(void)
{
	unitxt_init_txtmode(80, 25, 7, 0, 14, 15);
	
	return unitxt_init_chardev();
}

static void _EXIT unitxt_end(void)
{
	unitxt_stop_chardev();
	return;
}


/* CHAR DEV */
#if defined(__linux__)
	static struct file_operations unitxt_fops =
	{
		.owner    = THIS_MODULE,
		.open     = unitxt_open,
		.release  = unitxt_close,
		.read     = unitxt_read,
		.write    = unitxt_write
	};
#elif defined(__NetBSD__) || defined(__OpenBSD__)
	dev_type_open(unitxt_open);
	dev_type_close(unitxt_close);
	dev_type_write(unitxt_write);
	dev_type_read(unitxt_read);
	static struct cdevsw unitxt_cdevsw =
	{
		.d_open   = unitxt_open,
		.d_close  = unitxt_close,
		.d_read   = unitxt_read,
		.d_write  = unitxt_write,
		
		.d_ioctl    = noioctl,
		.d_stop     = nostop,
		.d_tty      = notty,
		.d_poll     = nopoll,
		.d_mmap     = nommap,
		.d_kqfilter = nokqfilter,
		.d_discard  = nodiscard,
		.d_flag     = D_OTHER
	};
#endif

static int unitxt_init_chardev(void)
{
	#if defined(__linux__)
		major = register_chrdev(0, DEVICE_NAME, &unitxt_fops);
	#elif defined(__NetBSD__) || defined(__OpenBSD__)
		devsw_attach("rperm", NULL, &bmajor, &unitxt_cdevsw, &cmajor);
	#endif
	return 0;
}

static int unitxt_stop_chardev(void)
{
	#if defined(__linux__)
		unregister_chrdev(major, DEVICE_NAME);
	#elif defined(__NetBSD__) || defined(__OpenBSD__)
		devsw_detach(NULL, &unitxt_cdevsw);
	#endif
	return 0;
}

#if defined(__linux__)
	static int unitxt_open(struct inode * inode, struct file * file)
	{
		return 0;
	}
#elif defined(__NetBSD__) || defined(__OpenBSD__)
	static int unitxt_open(dev_t self, int flag, int mod, struct lwp * l)
	{
		return 0;
	}
#endif

#if defined(__linux__)
	static int unitxt_close(struct inode * inode, struct file * file)
	{
		return 0;
	}
#elif defined(__NetBSD__) || defined(__OpenBSD__)
	static int unitxt_close(dev_t self, int flag, int mod, struct lwp * l)
	{
		return 0;
	}
#endif

static ssize_t unitxt_write(struct file * file, const char __user * buf, size_t count, loff_t * offset)
{
	char *  data;
	
	if (!(data = p_malloc(count)))
	{
		p_fail("failed to allocate");
		return -ENOMEM;
	}
	
	if (copy_from_user(data, buf, count))
	{
		kfree(data);
		return -EFAULT;
	}
	
	txt_print(data);
	
	kfree(data);
	return count;
}

static ssize_t unitxt_read(struct file * file, char __user * buf, size_t count, loff_t * offset)
{
	const char   msg[] = "Unitxt Running";
	const size_t len   = strlen(msg);
	
	if (*offset >= len)
	{
		return 0;
	}
	
	if (count > len - *offset)
	{
		count = len - *offset;
	}
	
	if (copy_to_user(buf, msg + *offset, count))
	{
		p_fail("copying to userspace failed");
		return -EFAULT;
	}
	
	*offset += count;
	
	return count;
}

/* TEXTMODE INTERFACE */
static inline uint8_t vga_entry(const unsigned char c, const uint8_t col)
{
	return (uint16_t) c | (uint16_t) col << 8;
}

static void txt_print(char * str)
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
			c += ansi_interpreter(&str[c]);
		}
		else
		{
			txt_putchar(c);
		}
	}
}

static void txt_putchar(const char c)
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
				txt_set(sp - 1, sp, NULL);
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
			txt_set(sp, sp + 1, &ent);
	}
}


/* ANSI */
static inline void set_fg(const uint8_t col)
{
	cur_col &= col;
}

static inline void set_bg(const uint8_t col)
{
	cur_col &= col << 4;
}

static uint64_t ansi_interpreter(char * cp)
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
					txt_set((vga_w * cur_y) + cur_x, vga_h * vga_w, NULL);
					break;
				case 1:
					txt_set(0, (vga_w * cur_y) + cur_x, NULL);
					break;
				case 2:
				case 3:
					txt_set(0, vga_w * vga_h, NULL);
					break;
			}
			break;
		case 'K':
			switch (cou)
			{
				case 0:
					txt_set((vga_w * cur_y) + cur_x, vga_w * (cur_y + 1), NULL);
					break;
				case 1:
					txt_set(vga_w * cur_y, (vga_w * cur_y) + cur_x, NULL);
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


/* TEXTMODE INTERNALS */
static void unitxt_init_txtmode(const uint8_t w, const uint8_t h, const uint8_t f, const uint8_t b, const uint8_t start_cur, const uint8_t end_cur)
{
	vga_w = w;
	vga_h = h;
	move_cursor(0, 0);
	
	buf = txtmode_addr;
	cur_col = f | b << 4;
	
	txt_set(0, vga_h * vga_w, NULL);
	
	def_cur(start_cur, end_cur);
	p_notice("unitxt initialized");
}
// init_txtmode(80, 25, 7, 0, 14, 15)

static void txt_set(const uint8_t start, const uint8_t end, const uint16_t * dt)
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
	p_outb(0x3D4, 0x0A);
	p_outb(0x3D5, (_inb(0x3D5) & 0xC0) | cur_start);
	p_outb(0x3D4, 0x0B);
	p_outb(0x3D5, (_inb(0x3D5) & 0xE0) | cur_end);
}

static void set_cur(const uint8_t x, const uint8_t y)
{
	const uint16_t pos = y * vga_w + x;
	p_outb(0x3D4, 0x0F);
	p_outb(0x3D5, (uint8_t) (pos & 0xFF));
	p_outb(0x3D4, 0x0E);
	p_outb(0x3D5, (uint8_t) ((pos >> 8) & 0xFF));
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
		txt_set(0, vga_w * (vga_h - 1), &buf[vga_w]);
	}
	else
	{
		cur_y++;
	}
	cur_x = 0;
}
