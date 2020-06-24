/*
 * Simple Watchdog Program
 * (C) 2020  Cplus Shen <cplus.shen@advantech.com.tw>
 *       
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 * 
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/io.h>
#include <unistd.h>

#define WATCHDOG_TIMEOUT 60

#define W83627HF_LD_WDT         0x08
#define W83627HF_WDT_CONTROL    0xf5
#define W83627HF_WDT_TIMEOUT    0xf6
#define W836X7HF_WDT_CSR        0xf7

#define WDT_EFER (wdt_io+0)	/* Extended Function Enable Registers */
#define WDT_EFIR (wdt_io+0)	/* Extended Function Index Register (same as EFER) */
#define WDT_EFDR (WDT_EFIR+1)	/* Extended Function Data Register */
#define IOPORT_DELAY 300000

struct watchdog_device {
    unsigned int timeout;
    unsigned int min_timeout;
    unsigned int max_timeout;
};

static struct watchdog_device wdt_dev = {
    .timeout = WATCHDOG_TIMEOUT,
    .min_timeout = 1,
    .max_timeout = 255,
};


static int early_disable = 0;
static int wdt_io = 0x2e;
static int cr_wdt_timeout;	/* WDT timeout register */
static int cr_wdt_control;	/* WDT control register */
static int cr_wdt_csr;		/* WDT control & status register */

static void superio_outb(int reg, int val)
{
    printf("%s reg 0x%02x, value 0x%02x\n",__FUNCTION__,reg,val);
    outb(reg, WDT_EFER);
    usleep(IOPORT_DELAY);
    outb(val, WDT_EFDR);
    usleep(IOPORT_DELAY);
}

static inline int superio_inb(int reg)
{
    outb(reg, WDT_EFER);
    usleep(IOPORT_DELAY);
    return inb(WDT_EFDR);
}

static int superio_enter(void)
{
    if (ioperm(wdt_io, 2, 1)) {
	perror("ioperm open");
	exit(1);
    }
    printf(">>>%s\n",__FUNCTION__);
    outb(0x87, WDT_EFER);	/* Enter extended function mode */
    usleep(IOPORT_DELAY);
    outb(0x87, WDT_EFER);	/* Again according to manual */
    usleep(IOPORT_DELAY);

    return 0;
}

static void superio_select(int ld)
{
    printf("%s\n",__FUNCTION__);
    superio_outb(0x07, ld);
}

static void superio_exit(void)
{
    printf("<<<%s\n",__FUNCTION__);
    outb(0xAA, WDT_EFER);	/* Leave extended function mode */
    usleep(IOPORT_DELAY);
    if (ioperm(wdt_io, 2, 0)) {
	perror("ioperm close");
	exit(1);
    }
}

static int wdt_set_time(unsigned int timeout)
{
    int ret;

    printf("%s:%d timeout %d\n", __FUNCTION__, __LINE__, timeout);
    ret = superio_enter();
    if (ret) {
	printf("%s:%d ret %d\n", __FUNCTION__, __LINE__, ret);
	return ret;
    }

    superio_select(W83627HF_LD_WDT);
    superio_outb(cr_wdt_timeout, timeout);
    superio_exit();

    printf("%s:%d select device 0x%x reg 0x%x timeout %d\n", __FUNCTION__, __LINE__, W83627HF_LD_WDT, cr_wdt_timeout, timeout);
    return 0;
}

static int wdt_start(struct watchdog_device *wdog)
{
    printf("%s:%d\n", __FUNCTION__, __LINE__);
    return wdt_set_time(wdog->timeout);
}

static int wdt_stop(struct watchdog_device *wdog)
{
    int ret;
    unsigned char t;

    ret = superio_enter();
    if (ret) {
	printf("%s:%d ret %d\n", __FUNCTION__, __LINE__, ret);
	return ret;
    }

    superio_select(W83627HF_LD_WDT);
    t = superio_inb(cr_wdt_control) & ~0x02; /* disable the WDT#1 output */
    superio_outb(cr_wdt_control, t);
    superio_exit();

    return wdt_set_time(0);
}

static unsigned int wdt_get_time(void)
{
    unsigned int timeleft;
    int ret;

    printf("%s:%d\n", __FUNCTION__, __LINE__);
    ret = superio_enter();
    if (ret)
	return 0;

    superio_select(W83627HF_LD_WDT);
    timeleft = superio_inb(cr_wdt_timeout);
    superio_exit();

    printf("%s:%d timeleft %d\n",__FUNCTION__,__LINE__,timeleft);
    return timeleft;
}

static int w83627hf_init(struct watchdog_device *wdog)
{
    int ret;
    unsigned char t;

    printf("%s:%d\n", __FUNCTION__, __LINE__);

    ret = superio_enter();
    if (ret)
        return ret;
    
    superio_select(W83627HF_LD_WDT);

    /* set CR30 bit 0 to activate WDT1 */
    t = superio_inb(0x30);

    if (!(t & 0x01))
	superio_outb(0x30, t | 0x01);

    /*
     * These chips have a fixed WDTO# output pin (W83627UHG),
     * or support more than one WDTO# output pin.
     * Don't touch its configuration, and hope the BIOS
     * does the right thing.
     */
    t = superio_inb(cr_wdt_control);
    t |= 0x02;			/* enable the WDTO# output low pulse
				 * to the KBRST# pin */
    superio_outb(cr_wdt_control, t);

    t = superio_inb(cr_wdt_timeout);
    if (t != 0) {
	if (early_disable) {
	    printf("Stopping previously enabled watchdog until userland kicks in\n");
	    superio_outb(cr_wdt_timeout, 0);
	} else {
	    printf("Watchdog already running(timeout %d). Resetting timeout to %d sec\n", t, wdog->timeout);
	    superio_outb(cr_wdt_timeout, wdog->timeout);
	}
    }


    /* set second mode & disable keyboard turning off watchdog */
    t = superio_inb(cr_wdt_control) & ~0x0C;
    superio_outb(cr_wdt_control, t);

    /* reset trigger, disable keyboard & mouse turning off watchdog */
    t = superio_inb(cr_wdt_csr) & ~0xD0;
    superio_outb(cr_wdt_csr, t);

    superio_exit();

    return 0;
}

static int wdt_init(void)
{
    int ret;
    wdt_io = 0x2e;

    cr_wdt_timeout = W83627HF_WDT_TIMEOUT;
    cr_wdt_control = W83627HF_WDT_CONTROL;
    cr_wdt_csr = W836X7HF_WDT_CSR;

    ret = w83627hf_init(&wdt_dev);
    if (ret) {
	printf("failed to initialize watchdog (err=%d)\n", ret);
	return ret;
    }
    return ret;

}

int main(int argc, char *argv[])
{
    int timeout = WATCHDOG_TIMEOUT;
    int feed = 0;
    int i;

    if (argc < 3) {
        printf("Usage: %s <timeout> <feed>\n",argv[0]);
        return 0;
    }

    timeout = strtoul(argv[1],NULL,0);
    feed = strtoul(argv[2],NULL,0);

    /* init watchdog */
    wdt_init();
    wdt_start(&wdt_dev);
    
    /* feed watchdog */
    for (i=0; i<feed; i++) {
        wdt_set_time(timeout);
        sleep(3);
    }

    /* stop watchdog */
    wdt_stop(&wdt_dev);

    return 0;
}
