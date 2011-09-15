/*
 * Write a new mac address in the snowball device.
 * This programs the 9221 Ethernet device mapped at 0x5000.0000 on 16 bits.
 * Note that you must have the ethernet down during this operation.
 *
 * Alessandro Rubini, 2011 (under contract with ST-Ericsson AG)
 * GNU GPL v2 or later.
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <ctype.h>
#include <termios.h>
#include <sys/types.h>
#include <sys/mman.h>

/*
 * Lazily, copy fnames from drivers/net/smsc911x.h
 */
#define GPIO_CFG			0x88
#define GPIO_CFG_LED3_EN_		0x40000000
#define GPIO_CFG_LED2_EN_		0x20000000
#define GPIO_CFG_LED1_EN_		0x10000000
#define GPIO_CFG_GPIO2_INT_POL_		0x04000000
#define GPIO_CFG_GPIO1_INT_POL_		0x02000000
#define GPIO_CFG_GPIO0_INT_POL_		0x01000000
#define GPIO_CFG_EEPR_EN_		0x00700000
#define GPIO_CFG_GPIOBUF2_		0x00040000
#define GPIO_CFG_GPIOBUF1_		0x00020000
#define GPIO_CFG_GPIOBUF0_		0x00010000
#define GPIO_CFG_GPIODIR2_		0x00000400
#define GPIO_CFG_GPIODIR1_		0x00000200
#define GPIO_CFG_GPIODIR0_		0x00000100
#define GPIO_CFG_GPIOD4_		0x00000020
#define GPIO_CFG_GPIOD3_		0x00000010
#define GPIO_CFG_GPIOD2_		0x00000004
#define GPIO_CFG_GPIOD1_		0x00000002
#define GPIO_CFG_GPIOD0_		0x00000001

#define E2P_CMD				0xB0
#define E2P_CMD_EPC_BUSY_		0x80000000
#define E2P_CMD_EPC_CMD_		0x70000000
#define E2P_CMD_EPC_CMD_READ_		0x00000000
#define E2P_CMD_EPC_CMD_EWDS_		0x10000000
#define E2P_CMD_EPC_CMD_EWEN_		0x20000000
#define E2P_CMD_EPC_CMD_WRITE_		0x30000000
#define E2P_CMD_EPC_CMD_WRAL_		0x40000000
#define E2P_CMD_EPC_CMD_ERASE_		0x50000000
#define E2P_CMD_EPC_CMD_ERAL_		0x60000000
#define E2P_CMD_EPC_CMD_RELOAD_		0x70000000
#define E2P_CMD_EPC_TIMEOUT_		0x00000200
#define E2P_CMD_MAC_ADDR_LOADED_	0x00000100
#define E2P_CMD_EPC_ADDR_		0x000000FF

#define E2P_DATA			0xB4
#define E2P_DATA_EEPROM_DATA_		0x000000FF


#define FATAL do { fprintf(stderr, "Error at line %d, file %s (%d) [%s]\n", \
  __LINE__, __FILE__, errno, strerror(errno)); exit(1); } while(0)

#define MAP_BASE 0x50000000
#define MAP_SIZE 0x00001000 /* 4k */

static void reg_w(void *base, int reg, unsigned long val)
{
	volatile unsigned short *regs = base;

	regs[reg] = val;
	regs[reg + 2] = val >> 16;
}

static unsigned long reg_r(void *base, int reg)
{
	volatile unsigned short *regs = base;
	unsigned long val;

	val = regs[reg];
	val |= regs[reg + 2] << 16;
	return val;
}

static unsigned long mac_r(void *base, int reg)
{
	reg_w(base, 0xA4, 0xc0000000 + reg);
	return reg_r(base, 0xA8);
}

static void dump_eep(void *base)
{
	int i;

	printf("Current eeprom:\n");
	for (i = 0; i < 32; i++) {
		while (reg_r(base, E2P_CMD) & E2P_CMD_EPC_BUSY_)
			;
		reg_w(base, E2P_CMD, 0
		      | E2P_CMD_EPC_CMD_READ_
		      | i);
		reg_w(base, E2P_CMD, 0
		      | E2P_CMD_EPC_BUSY_
		      | E2P_CMD_EPC_CMD_READ_
		      | i);
		while (reg_r(base, E2P_CMD) & E2P_CMD_EPC_BUSY_)
			;
		printf("%02lx", reg_r(base, E2P_DATA));
		putchar((i & 7) == 7 ? '\n' : ' ');
	}
}

int verbose = 0;
int main(int argc, char **argv)
{
	int fd, i;
	void *base;
	unsigned char macaddr[8];
	unsigned long macregs[2];

	if (getenv("VERBOSE")) verbose = 1;

	/* First, access current information */
	if((fd = open("/dev/mem", O_RDWR | O_SYNC)) < 0) FATAL;
	if (verbose) {
		printf("/dev/mem opened.\n");
		fflush(stdout);
	}

	base = mmap(0, MAP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd,
			MAP_BASE);

	if(base == (void *) -1) FATAL;
	if (verbose) {
		printf("Memory mapped at address %p.\n", base);
		fflush(stdout);
	}
	macregs[0] = mac_r(base, 2);
	macregs[1] = mac_r(base, 3);
	printf("Current mac registers: %08lx %08lx\n",
	       macregs[0], macregs[1]);
	memcpy(macaddr+0, macregs+1, 4);
	memcpy(macaddr+4, macregs+0, 4);
	printf("Current mac address %02x:%02x:%02x:%02x:%02x:%02x\n",
	       macaddr[0], macaddr[1], macaddr[2], macaddr[3], macaddr[4],
	       macaddr[5]);
	printf("Current status in E2P_CMD: %08lx\n", reg_r(base, 0xB0));
	/* Then, parse information */
	if(argc != 2) {
		fprintf(stderr, "\n%s: Use: \"%s <mac>\" or \"%s -r\"\n",
			argv[0], argv[0], argv[0]);
		exit(1);
	}
	if (!strcmp(argv[1], "-r")) {
		int fd;
		fd = open("/dev/urandom", O_RDONLY);
		if (fd < 0) FATAL;
		if (read(fd, macaddr, 6) != 6) FATAL;
		close(fd);
		macaddr[0] &= 0xfe;	/* clear multicast bit */
		macaddr[0] |= 0x02;	/* set local assignment bit (IEEE802) */
	} else {
		i = sscanf(argv[1], "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
			   macaddr+0, macaddr+1, macaddr+2, macaddr+3,
			   macaddr+4, macaddr+5, macaddr+6);
		if (i != 6) FATAL;
	}
	printf("Writing mac address %02x:%02x:%02x:%02x:%02x:%02x\n",
	       macaddr[0], macaddr[1], macaddr[2], macaddr[3], macaddr[4],
	       macaddr[5]);

	/*
	 * Finally, access eeprom -- note that GPIO_CFG must be fixed.
	 */

	printf("gpiocfg was %08lx\n", reg_r(base, GPIO_CFG));
	reg_w(base, GPIO_CFG, 0); /* to be able to access EEPROM */
	printf("gpiocfg  is %08lx\n", reg_r(base, GPIO_CFG));

	/* Dump, change, re-dump */
	dump_eep(base);

	while (reg_r(base, E2P_CMD) & E2P_CMD_EPC_BUSY_)
		;
	reg_w(base, E2P_CMD, 0
	      | E2P_CMD_EPC_CMD_EWEN_ /* enable write */
		);
	reg_w(base, E2P_CMD, 0
	      | E2P_CMD_EPC_BUSY_
	      | E2P_CMD_EPC_CMD_EWEN_ /* enable write */
		);
	while (reg_r(base, E2P_CMD) & E2P_CMD_EPC_BUSY_)
		;
	/* The previous one would timeout if eeprom is missing */
	if (reg_r(base, E2P_CMD) & E2P_CMD_EPC_TIMEOUT_)
		FATAL;

	printf("Write...\n");
	for (i = 0; i < 7; i++) { /* 0xa5 magic byte and 6 mac bytes */
		int datum;

		if (i == 0) datum = 0xa5;
		else datum = macaddr[i-1];

		while (reg_r(base, E2P_CMD) & E2P_CMD_EPC_BUSY_)
			;
		reg_w(base, E2P_DATA, datum);
		reg_w(base, E2P_CMD, 0
		      | E2P_CMD_EPC_CMD_WRITE_
		      | i);
		reg_w(base, E2P_CMD, 0
		      | E2P_CMD_EPC_BUSY_
		      | E2P_CMD_EPC_CMD_WRITE_
		      | i);
		while (reg_r(base, E2P_CMD) & E2P_CMD_EPC_BUSY_)
			;
	}

	reg_w(base, E2P_CMD, 0
	      | E2P_CMD_EPC_CMD_EWDS_ /* disable write */
		);


	dump_eep(base);

	exit(0);
}
