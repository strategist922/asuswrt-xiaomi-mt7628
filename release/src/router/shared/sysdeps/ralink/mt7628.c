/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */
#include <stdio.h>	     
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <bcmnvram.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <shutils.h>
#include <shared.h>
#include <utils.h>
#include <linux/config.h>
#include <ralink.h>
#include "mt7628.h"
#include "ra_ioctl.h"

#define GPIO_DEV	"/dev/gpio0"
#define NR_WANLAN_PORT	5


struct wifi_if_vid_s {
	int wl_vid[2];
	int wl_wds_vid[2];
};

enum {
	gpio_in,
	gpio_out,
};

#if 0
#if defined(RTN14U) || defined(RTAC51U) ||defined(RTN54U) || defined(RPAC56) || defined(RTAC1200)
/// RT-N14U mapping
enum {
	WAN_PORT=0,
	LAN1_PORT=1,
	LAN2_PORT=2,
	LAN3_PORT=3,
	LAN4_PORT=4,
	P5_PORT=5,
	CPU_PORT=6,
	P7_PORT=7,
};
#elif defined(RTAC52U)
/// RT-AC52U mapping
enum {
	WAN_PORT=0,
	LAN1_PORT=3,
	LAN2_PORT=4,
	LAN3_PORT=2,
	LAN4_PORT=1,
	P5_PORT=5,
	CPU_PORT=6,
	P7_PORT=7,
};
#define PORT_W4321

#elif defined(RTN11P)
/// RT-N11P mapping
enum {
	WAN_PORT=4,
	LAN1_PORT=3,
	LAN2_PORT=2,
	LAN3_PORT=1,
	LAN4_PORT=0,
	P5_PORT=5,
	CPU_PORT=6,
	P7_PORT=7,
};
#endif
#endif

#if defined(RT4C)
enum {
	WAN_PORT=1,
	LAN1_PORT=4,
	LAN2_PORT=2,
	LAN3_PORT=0,
	LAN4_PORT=3,
	P5_PORT=5,
	CPU_PORT=6,
	P7_PORT=7,
};
#elif defined(RTCONFIG_WAN_AT_P4)
enum {
	WAN_PORT=4,
	LAN1_PORT=3,
	LAN2_PORT=2,
	LAN3_PORT=1,
	LAN4_PORT=0,
	P5_PORT=5,
	CPU_PORT=6,
	P7_PORT=7,
};
//#define PORT_W4321  1
#else
enum {
	WAN_PORT=0,
	LAN1_PORT=1,
	LAN2_PORT=2,
	LAN3_PORT=3,
	LAN4_PORT=4,
	P5_PORT=5,
	CPU_PORT=6,
	P7_PORT=7,
};
#endif

#define BIT(n)	(1 << (n))
/* 0: LAN, 1:WAN or STB */
static const int lan_wan_partition[] = {
	BIT( WAN_PORT ),					// WAN
	BIT( WAN_PORT ) | BIT( LAN1_PORT ),			// WAN + LAN1
	BIT( WAN_PORT ) | BIT( LAN2_PORT ),			// WAN + LAN2
	BIT( WAN_PORT ) | BIT( LAN3_PORT ),			// WAN + LAN3
	BIT( WAN_PORT ) | BIT( LAN4_PORT ),			// WAN + LAN4
	BIT( WAN_PORT ) | BIT( LAN1_PORT ) | BIT( LAN2_PORT ),	// WAN + LAN1+2
	BIT( WAN_PORT ) | BIT( LAN3_PORT ) | BIT( LAN4_PORT ),	// WAN + LAN3+4
	0,							// ALL LAN
};

/* Final model-specific LAN/WAN/WANS_LAN partition definitions.
 * bit0: P0, bit1: P1, bit2: P2, bit3: P3, bit4: P4
 */
static unsigned int lan_mask = 0;	/* LAN only. Exclude WAN, WANS_LAN, and generic IPTV port. */
static unsigned int wan_mask = 0;	/* wan_type = WANS_DUALWAN_IF_WAN. Include generic IPTV port. */
static unsigned int wans_lan_mask = 0;	/* wan_type = WANS_DUALWAN_IF_LAN. */



int esw_fd;


/* RT-N56U's P0, P1, P2, P3, P4 = LAN4, LAN3, LAN2, LAN1, WAN
 * ==> Model-specific port number.
 */
static int switch_port_mapping[] = {
	LAN4_PORT,	//0000 0000 0001 LAN4
	LAN3_PORT,	//0000 0000 0010 LAN3
	LAN2_PORT,	//0000 0000 0100 LAN2
	LAN1_PORT,	//0000 0000 1000 LAN1
	WAN_PORT,	//0000 0001 0000 WAN
	P5_PORT,	//0000 0010 0000 -
	P5_PORT,	//0000 0100 0000 -
	P5_PORT,	//0000 1000 0000 -
	CPU_PORT,	//0001 0000 0000 RGMII LAN
	P7_PORT,	//0010 0000 0000 RGMII WAN
};


#ifdef RTCONFIG_WAN_AT_P4

const int lan_id_to_port_mapping[NR_WANLAN_PORT] = {
	LAN4_PORT,
	LAN3_PORT,
	LAN2_PORT,
	LAN1_PORT,
	WAN_PORT,	/* not used */
};
#else

/* Model-specific LANx ==> Model-specific PortX mapping */
const int lan_id_to_port_mapping[NR_WANLAN_PORT] = {
	WAN_PORT,	/* not used */
	LAN1_PORT,
	LAN2_PORT,
	LAN3_PORT,
	LAN4_PORT,
};
#endif

/* Model-specific LANx ==> Model-specific PortX */
static inline int lan_id_to_port_nr(int id)
{
	return lan_id_to_port_mapping[id];
}

/**
 * Get WAN port mask
 * @wan_unit:	wan_unit, if negative, select WANS_DUALWAN_IF_WAN
 * @return:	port bitmask
 */
static unsigned int get_wan_port_mask(int wan_unit)
{
	char nv[] = "wanXXXports_maskXXXXXX";

	if (nvram_get_int("sw_mode") == SW_MODE_REPEATER)
		return 0;

	if (wan_unit <= 0 || wan_unit >= WAN_UNIT_MAX)
		strcpy(nv, "wanports_mask");
	else
		sprintf(nv, "wan%dports_mask", wan_unit);

	return nvram_get_int(nv);
}

/**
 * Get LAN port mask
 * @return:	port bitmask
 */
static unsigned int get_lan_port_mask(void)
{
	int sw_mode = nvram_get_int("sw_mode");
	unsigned int m = nvram_get_int("lanports_mask");

	if (sw_mode == SW_MODE_AP)
		m = 0x1F;

	return m;
}

/**
 * Create string based portmap base on mask parameter.
 * @mask:	port bit mask.
 * 		bit0: P0, bit1: P1, etc
 * @portmap:	pointer to char array. minimal length is 9 bytes.
 */
static void __create_port_map(unsigned int mask, char *portmap)
{
	int i;
	char *p;
	unsigned int m;

	for (i = 0, m = mask, p = portmap; i < 8; ++i, m >>= 1, ++p)
		*p = '0' + (m & 1);
	*p = '\0';
}

int switch_init(void)
{
	esw_fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (esw_fd < 0) {
		perror("socket");
		return -1;
	}
	return 0;
}

void switch_fini(void)
{
	close(esw_fd);
}

int mt7628_reg_read(int offset, unsigned int *value)
{
	struct ifreq ifr;
	esw_reg reg;

	if (value == NULL)
		return -1;
	reg.off = offset;
	strncpy(ifr.ifr_name, "eth2", 5);
	ifr.ifr_data = (void*) &reg;
	if (-1 == ioctl(esw_fd, RAETH_ESW_REG_READ, &ifr)) {
		_dprintf("%s: read esw register fail. errno %d (%s)\n", __func__, errno, strerror(errno));
		close(esw_fd);
		return -1;
	}
	*value = reg.val;

//	printf("mt7628_reg_read()...offset=%4x, value=%8x\n", offset, *value);

	return 0;
}


int mt7628_reg_write(int offset, int value)
{
	struct ifreq ifr;
	esw_reg reg;

	printf("mt7628_reg_write()..offset=%4x, value=%8x\n", offset, value);

	reg.off = offset;
	reg.val = value;
	strncpy(ifr.ifr_name, "eth2", 5);
	ifr.ifr_data = (void*) &reg;
	if (-1 == ioctl(esw_fd, RAETH_ESW_REG_WRITE, &ifr)) {
		perror("ioctl");
		close(esw_fd);
		exit(0);
	}
	return 0;
}

static void write_VTCR(unsigned int value)
{
	int i;
	unsigned int check;

	value |= 0x80000000;
	mt7628_reg_write(REG_ESW_VLAN_VTCR, value);

	for (i = 0; i < 20; i++) {
		mt7628_reg_read(REG_ESW_VLAN_VTCR, &check);
		if ((check & 0x80000000) == 0 ) //table busy
			break;
		usleep(1000);
	}
	if (i == 20)
	{
		cprintf("VTCR timeout.\n");
	}
	else if(check & (1<<16))
	{
		cprintf("%s(%08x) invalid\n", __func__, value);
	}
}

/**
 * Find first available vlan slot or find vlan slot by vid.
 * VLAN slot 0~2 are reserved.
 * If VLAN ID is not equal to index + 1, it is assumed unavailable.
 * @vid:
 * 	>0:	Find vlan slot by vid.
 * 	<=0:	Find first available vlan slot.
 * @vawd1:	pointer to a unsigned integer.
 * 		If vlan slot found and vawd1 is not null, save VAWD1 value here.
 * @return:
 * 	0~15:	vlan slot index.
 * 	<0:	not found
 */
#if defined(RTCONFIG_RALINK_MT7628)
static int find_vlan_slot(int vid, unsigned int *vmsc)
#else
static int find_vlan_slot(int vid, unsigned int *vawd1)
#endif
{
#if defined(RTCONFIG_RALINK_MT7628)
	int idx;
	unsigned int i, r, v, vmsc_value, value;

//	for (i = 3, idx = -1; idx < 0 && i < 16; ++i) { // RTAC1200 not support Dual Wan. Mark it.
	for (i = 2, idx = -1; idx < 0 && i < 16; ++i) {
		if ((r = mt7628_reg_read(REG_ESW_VLANI0 + 4*(i>>1), &value))) {
			_dprintf("read VLANI0-7, i %d offset %x fail. (r = %d)\n", i, REG_ESW_VLANI0 + 4*(i>>1), r);
			continue;
		}

		if (!(i&1))
			v = value & 0xfff;
		else
			v = (value  >> 12) & 0xfff;
		if ((vid <= 0 && v != (i + 1)) || (vid > 0 && v != vid))
			continue;
		/* Read VMSC */
		mt7628_reg_read(REG_ESW_VMSC0 + 4*(i>>2), &vmsc_value);

		value = (vmsc_value >> (8*(i%4))) & 0xff;
		if ((vid <= 0) && v == (i + 1) && value == 0xff)
			idx = i;
		else if (vid > 0 && v == vid)
			idx = i;

		if (idx >= 0 && vmsc)
			*vmsc = vmsc_value;
	}

	return idx;
#else
	int idx;
	unsigned int i, r, v, value;

	for(i = 3, idx = -1; idx < 0 && i < 16; ++i) {	/* skip vlan index 0~2 */
		if ((r = mt7628_reg_read(REG_ESW_VLAN_ID_BASE + 4*(i>>1), &value))) {
			_dprintf("read VTIM1~8, i %d offset %x fail. (r = %d)\n", i, REG_ESW_VLAN_ID_BASE + 4*(i>>1), r);
			continue;
		}

		if (!(i&1))
			v = value & 0xfff;
		else
			v = (value >> 12) & 0xfff;

		if ((vid <= 0 && v != (i + 1)) && (vid > 0 && v != vid))
			continue;

		value = (0x0 << 12) + i; //read VAWD#
		write_VTCR(value);
		mt7628_reg_read(REG_ESW_VLAN_VAWD1, &value);

		if ((vid <= 0 ) && v == (i + 1) && !(value & 1))	/* find available vlan slot */
			idx = i;
		else if (vid > 0 && v == vid)				/* find vlan slot by vid */
			idx = i;

		if (idx >= 0 && vawd1)
			*vawd1 = value;
	}

	return idx;
#endif
}

/**
 * Set a VLAN
 * @idx:	VLAN table index
 *  >= 0:	specify VLAN table index.
 *  <  0:	find available VLAN table.
 * @vid:	VLAN ID
 * @portmap:	Port member. string length must greater than or equal to 8.
 * 		Only '0' and '1' are accepted.
 * 		First digit means port 0, second digit means port1, etc.
 * 		P0, P1, P2, P3, P4, P5, P6, P7
 * @stag:	Service tag
 * @return
 * 	0:	success
 *     -1:	no vlan entry available
 *     -2:	invalid parameter
 */
int mt7628_vlan_set(int idx, int vid, char *portmap, int stag)
{
#if defined(RTCONFIG_RALINK_MT7628)
	unsigned int i, value, mbr, vmsc;

	if (idx < 0) {
		if ((idx = find_vlan_slot(vid, &vmsc)) < 0 && (idx = find_vlan_slot(-1, &vmsc)) < 0) {
			_dprintf("%s: no empty vlan entry for vid %d portmap %s stag %d!\n",
				__func__, vid, portmap, stag);
			return -1;
		}
	}
	else {
		mt7628_reg_read(REG_ESW_VMSC0 + 4*(idx>>2), &vmsc);
	}

	_dprintf("%s: idx=%d, vid=%d, portmap=%s, stag=%d\n", __func__, idx, vid, portmap, stag);

	//set vlan identifier
	mt7628_reg_read(REG_ESW_VLANI0 + 4*(idx>>1), &value);
	if (idx % 2 == 0) {
		value &= 0xfff000;
		value |= vid;
	}
	else {
		value &= 0xfff;
		value |= (vid << 12);
	}
	mt7628_reg_write(REG_ESW_VLANI0 + 4*(idx>>1), value);
	mt7628_reg_read(REG_ESW_VLANI0 + 4*(idx>>1), &value);
	_dprintf("%s: IDX value = 0x%x\n", __func__, value);

	//set vlan member
	mbr = 0;
	for (i = 0; i < 8; i++)
		mbr += (*(portmap + i) - '0') * (1 << i);
	if (idx % 4 == 0) {	
		vmsc &= 0xffffff00;
	}
	else if (idx % 4 == 1) {
		vmsc &= 0xffff00ff;
	}
	else if (idx % 4 == 2) {
		vmsc &= 0xff00ffff;
	}
	else {
		vmsc &= 0x00ffffff;
	}

	vmsc |= (mbr << 8*(idx%4));
	mt7628_reg_write(REG_ESW_VMSC0 + 4*(idx>>2), vmsc);

	return 0;
#else
	unsigned int i, mbr, value, vawd1;

	if (idx < 0) { //auto
		if ((idx = find_vlan_slot(vid, &vawd1)) < 0 && (idx = find_vlan_slot(-1, &vawd1)) < 0) {
			_dprintf("%s: no empty vlan entry for vid %d portmap %s stag %d!\n",
				__func__, vid, portmap, stag);
			return -1;
		}
	}

	_dprintf("%s: idx=%d, vid=%d, portmap=%s, stag=%d\n", __func__, idx, vid, portmap, stag);

	//set vlan identifier
	mt7628_reg_read(REG_ESW_VLAN_ID_BASE + 4*(idx/2), &value);
	if (idx % 2 == 0) {
		value &= 0xfff000;
		value |= vid;
	}
	else {
		value &= 0xfff;
		value |= (vid << 12);
	}
	mt7628_reg_write(REG_ESW_VLAN_ID_BASE + 4*(idx/2), value);
	//set vlan member
	mbr = 0;
	for (i = 0; i < 8; i++)
		mbr += (*(portmap + i) - '0') * (1 << i);
	value = (mbr << 16);		//PORT_MEM
	value |= (1 << 30);		//IVL
	value |= (1 << 27);		//COPY_PRI
	value |= ((stag & 0xfff) << 4);	//S_TAG
	value |= 1;			//VALID
	mt7628_reg_write(REG_ESW_VLAN_VAWD1, value);

	value = (0x80001000 + idx); //w_vid_cmd
	write_VTCR(value);

	return 0;
#endif
}

/**
 * Disable a VLAN by vid and restore VID to initial value.
 * @vid:	VLAN ID to be deleted.
 * @return:
 * 	0:	success
 */
int mt7628_vlan_unset(int vid)
{
#if defined(RTCONFIG_RALINK_MT7628)
	int idx = -1;
	unsigned int value, vmsc;
	if ((idx = find_vlan_slot(vid, &vmsc)) < 0)
		return -1;

	/* Reset VMSC */
	vmsc |= (0xfff << (8*(idx%4)));
	mt7628_reg_write(REG_ESW_VMSC0 + 4*(idx>>2), vmsc);

	/* Restore VLAN identifier */
	mt7628_reg_read(REG_ESW_VLANI0 + 4*(idx>>1), &value);
	if (idx % 2 == 0) {
		value &= 0xfff000;
		value |= idx+1;
	}
	else {
		value &= 0xfff;
		value |= ((idx+1) << 12);
	}
	mt7628_reg_write(REG_ESW_VLANI0 + 4*(idx>>1), value);

#else
	int idx = -1;
	unsigned int value, vawd1;

	if ((idx = find_vlan_slot(vid, &vawd1)) < 0)
		return -1;

	//_dprintf("%s: delete vid=%d at idx=%d\n", __func__, vid, idx, vid);

	/* disable VLAN */
	mt7628_reg_write(REG_ESW_VLAN_VAWD1, 0);
	value = (0x80001000 + idx); //w_vid_cmd
	write_VTCR(value);

	/* restore vlan identifier */
	mt7628_reg_read(REG_ESW_VLAN_ID_BASE + 4*(idx/2), &value);
	if (idx % 2 == 0) {
		value &= 0xfff000;
		value |= idx+1;
	}
	else {
		value &= 0xfff;
		value |= ((idx+1) << 12);
	}
	mt7628_reg_write(REG_ESW_VLAN_ID_BASE + 4*(idx/2), value);

#endif
	return 0;
}


#if defined(RTN14U) || defined(RTAC52U) || defined(RTAC51U) || defined(RTN11P) || defined(RTN54U) 
/**
 * Get TX or RX byte count of WAN and WANS_LAN
 * @unit:	WAN unit.
 * @tx:
 * @rx:
 * @return:
 * 	-1:	invalid parameter.
 * 	0:	success
 */
int __mt7628_wan_bytecount(int unit, unsigned long *tx, unsigned long *rx)
{
	int dir, i;
	unsigned long count[2];
	unsigned int m, v, addr;

	if (unit < 0 || unit >= WAN_UNIT_MAX || !tx || !rx) {
		_dprintf("%s: invalid parameter! (%d, %p, %p)", unit, tx, rx);
		return -1;
	}
 
	if (switch_init() < 0)
		return -1;

	for (dir = 0; dir <= 1; ++dir) {
		count[dir] = 0;
		addr = dir? REG_ESW_PORT_RGOCN_P0:REG_ESW_PORT_TGOCN_P0;
		m = get_wan_port_mask(unit) & ((1U << NR_WANLAN_PORT) - 1);
		for (i = 0; m && i < NR_WANLAN_PORT; ++i, m >>= 1, addr += 0x100) {
			if (!(m & 1))
				continue;

			v = 0;
			mt7628_reg_read(addr, &v);
			count[dir] += v;
		}
	}

	switch_fini();

	*tx = count[0];
	*rx = count[1];

	return 0;
}   
#endif

/**
 * Get linkstatus in accordance with port bit-mask.
 * @mask:	port bit-mask.
 * 		bit0 = P0, bit1 = P1, etc.
 * @linkStatus:	link status of all ports that is defined by mask.
 * 		If one port of mask is linked-up, linkStatus is true.
 */
static void get_mt7628_esw_phy_linkStatus(unsigned int mask, unsigned int *linkStatus)
{
	int i;
	unsigned int value = 0, m;

	if (switch_init() < 0)
		return;

	m = mask & ((1U << NR_WANLAN_PORT) - 1);
	for (i = 0; m && !value && i < NR_WANLAN_PORT; ++i, m >>= 1) {
		if (!(m & 1))
			continue;
#if 1
		mt7628_reg_read(REG_ESW_POA, &value);
		//printf("offset=%4x, value=%8x\n", REG_ESW_POA, value);
		//value &= (0x1 << (REG_ESW_POA_LINK_STATUS_SHIFT + i));
		value = (value >> (REG_ESW_POA_LINK_STATUS_SHIFT + i)) & 0x1;
#endif
#if 0
		mt7628_reg_read(REG_ESW_FPA, &value);
		printf("offset=%4x, value=%8x\n", REG_ESW_FPA, value);
		value &= (0x400000 << i);
		printf("value=%8x\n", value);
#endif
		//value &= 0x1;
	}
	*linkStatus = value;

	switch_fini();
}

static void build_wan_lan_mask(int stb)
{
	int unit;
	int wanscap_lan = get_wans_dualwan() & WANSCAP_LAN;
	int wans_lanport = nvram_get_int("wans_lanport");
	int sw_mode = nvram_get_int("sw_mode");
	char prefix[8], nvram_ports[20];

	if (sw_mode == SW_MODE_AP || sw_mode == SW_MODE_REPEATER)
		wanscap_lan = 0;

	if (stb == 100 && (sw_mode == SW_MODE_AP || __mediabridge_mode(sw_mode)))
		stb = 7;	/* Don't create WAN port. */

	if (wanscap_lan && (wans_lanport < 0 || wans_lanport > 4)) {
		_dprintf("%s: invalid wans_lanport %d!\n", __func__, wans_lanport);
		wanscap_lan = 0;
	}

	lan_mask = wan_mask = wans_lan_mask = 0;
	if(stb < 0 || stb >= ARRAY_SIZE(lan_wan_partition))
	{
		_dprintf("%s: invalid partition index: %d\n", __func__, stb);
		stb = 0;
	}

	wan_mask = lan_wan_partition[stb];
	lan_mask = ((1<<NR_WANLAN_PORT) -1) & ~lan_wan_partition[stb];

	//DUALWAN
	if (wanscap_lan) {
		wans_lan_mask = 1U << lan_id_to_port_nr(wans_lanport);
		lan_mask &= ~wans_lan_mask;
	}

	for (unit = WAN_UNIT_FIRST; unit < WAN_UNIT_MAX; ++unit) {
		sprintf(prefix, "%d", unit);
		sprintf(nvram_ports, "wan%sports_mask", (unit == WAN_UNIT_FIRST)?"":prefix);

		if (get_dualwan_by_unit(unit) == WANS_DUALWAN_IF_WAN) {
			nvram_set_int(nvram_ports, wan_mask);
		}
		else if (get_dualwan_by_unit(unit) == WANS_DUALWAN_IF_LAN) {
			nvram_set_int(nvram_ports, wans_lan_mask);
		}
		else
			nvram_unset(nvram_ports);
	}
	nvram_set_int("lanports_mask", lan_mask);
}

static void set_Vlan_untag(int idx, int untag_mask) {

	_dprintf("[%s][%d] idx = %d, untag_mask = %d\n", __func__, __LINE__, idx, untag_mask);
	unsigned int value, mask = 0;

	if (switch_init() < 0)
		return;

	/* Set POC2 use untag enable bitmap in Vlan table. */
	mt7628_reg_read(REG_ESW_POC2, &value);
	value &= 0xffff7fff;
	value += 0x8000;

	/* Set untag bitmap in VLAN UNTAG Block 0-3 */
	mt7628_reg_read(REG_ESW_VUB0 + 4*(idx / 4), &value);

	if (idx % 4 == 0) {
		value &= 0xffffff80;
		value |= untag_mask;
	}
	else if (idx % 4 == 1) {
		value &= 0xfffc07f;
		untag_mask <<= 7;
		value += untag_mask;
	}
	else if (idx % 4 == 2) {
		value &= 0xffe03fff;
		untag_mask <<= 14;
		value += untag_mask;
	}
	else {
		value &= 0xf013ffff;
		untag_mask <<= 21;
		value += untag_mask;
	}

	mt7628_reg_write(REG_ESW_VUB0 + 4*(idx / 4), value);

	switch_fini();
}

/**
 * Configure LAN/WAN partition base on generic IPTV type.
 * @type:
 * 	0:	Default.
 * 	1:	LAN1
 * 	2:	LAN2
 * 	3:	LAN3
 * 	4:	LAN4
 * 	5:	LAN1+LAN2
 * 	6:	LAN3+LAN4
 */
static void config_mt7628_esw_LANWANPartition(int type)
{
	char portmap[16];
	int i, v, wans_lan_vid = 3, wanscap_wanlan = get_wans_dualwan() & (WANSCAP_WAN | WANSCAP_LAN);
	int wanscap_lan = get_wans_dualwan() & WANSCAP_LAN;
	int wans_lanport = nvram_get_int("wans_lanport");
	int sw_mode = nvram_get_int("sw_mode");
	unsigned int m;
	unsigned int pvid = 0, vmsc = 0;
	unsigned int untag_portmap = 0;

	if (switch_init() < 0)
		return;

	if (sw_mode == SW_MODE_AP || sw_mode == SW_MODE_REPEATER)
		wanscap_lan = 0;

	if (wanscap_lan && (wans_lanport < 0 || wans_lanport > 4)) {
		_dprintf("%s: invalid wans_lanport %d!\n", __func__, wans_lanport);
		wanscap_lan = 0;
		wanscap_wanlan &= ~WANSCAP_LAN;
	}

	if (wanscap_lan && !(get_wans_dualwan() & WANSCAP_WAN))
		wans_lan_vid = 2;

	build_wan_lan_mask(type);
	_dprintf("%s: LAN/WAN/WANS_LAN portmask %08x/%08x/%08x\n", __func__, lan_mask, wan_mask, wans_lan_mask);

	/* set priority flow control */
	if (lan_mask != 0x1f)
		mt7628_reg_write(REG_ESW_PFC1, 0x405555);
	else
		mt7628_reg_write(REG_ESW_PFC1, 0x005555);

	/* set PVID */
	/*mt7628_reg_write(REG_ESW_PVIDC0, 0x1001);
	mt7628_reg_write(REG_ESW_PVIDC1, 0x1001);
	mt7628_reg_write(REG_ESW_PVIDC2, 0x1002);*/

	for (i = 0, m = 1; i < NR_WANLAN_PORT + 1; ++i, m <<= 1) {
		if (lan_mask & m)
			v = 1;	//LAN
		else if (wanscap_lan && (wans_lan_mask & m))
			v = wans_lan_vid;	//WANSLAN
		else
			v = 2;	//WAN
		//_dprintf("%s: P%d PVID %d\n", __func__, i, v);

		if (i % 2) {
			if (i != 5)
				pvid += (v << 12);
			else
				pvid += (0x1 << 12);
			_dprintf("%s: PVIDC%d - %08x\n", __func__, i/2,  pvid);
			mt7628_reg_write(REG_ESW_PVIDC0 + ((i / 2) * 0x4), pvid);
			pvid = 0;	/* reset for next */
		}
		else
			pvid += v;
	}	

	/* set VLAN Identifier */
	mt7628_reg_write(REG_ESW_VLANI0, 0x2001);
	if (wans_lan_mask)
		mt7628_reg_write(REG_ESW_VLANI1, 0x4003);
	
	/* VLAN Member Port Configuration */
	//mt7628_reg_write(REG_ESW_VMSC0, 0xffff506f);
	vmsc = 0;
	for (i = 0, m = 1; i < NR_WANLAN_PORT; ++i, m <<= 1) {
		if (lan_mask & m)
			vmsc += (0x1 << i);
	}
	if (lan_mask)
		vmsc += (0x1 << CPU_PORT);	/* port 6 */

	for (i = 0, m = 1; i < NR_WANLAN_PORT; ++i, m <<= 1) {
		if (wan_mask & m)
			vmsc += (0x1 << (0x8 + i));
	}
	if (wan_mask)
		vmsc += (0x1 << (0x8 + CPU_PORT));	/* port 6 */

	for (i = 0, m = 1; i < NR_WANLAN_PORT; ++i, m <<= 1) {
		if (wans_lan_mask & m)
			vmsc += (0x1 << (0x10 + i));
	}
	if (wans_lan_mask)
		vmsc += (0x1 << (0x10 + CPU_PORT));	/* port 6 */
	
	vmsc += 0xff000000; // Should not modify VLAN 3 Member Port value. Set VLAN 3 Member Port to default 0xFF.
	_dprintf("%s: VMSC - %08x\n", __func__, vmsc);
	mt7628_reg_write(REG_ESW_VMSC0, vmsc);

	/* set Port Control */
	if (lan_mask != 0x1f) {
		mt7628_reg_write(REG_ESW_POC2, 0xff3f);
		set_Vlan_untag(0, lan_mask);
		set_Vlan_untag(1, wan_mask);
	}
	else {
		mt7628_reg_write(REG_ESW_POC2, 0xff7f);
		set_Vlan_untag(0, lan_mask);
		set_Vlan_untag(1, wan_mask);
	}
#if 0
	//LAN/WAN ports as security mode
	for (i = 0; i < 6; i++)
		mt7628_reg_write((REG_ESW_PORT_PCR_P0 + 0x100*i), 0xff0003);
	//LAN/WAN ports as transparent port
	for (i = 0; i < 6; i++)
		mt7628_reg_write((REG_ESW_PORT_PVC_P0 + 0x100*i), 0x810000c2);	//transparent port, admit untagged frames

	//set CPU/P7 port as user port
	mt7628_reg_write((REG_ESW_PORT_PVC_P0 + 0x100*CPU_PORT), 0x81000000);	//port6, user port, admit all frames
	mt7628_reg_write((REG_ESW_PORT_PVC_P0 + 0x100*P7_PORT), 0x81000000);	//port7, user port, admit all frames

	mt7628_reg_write((REG_ESW_PORT_PCR_P0 + 0x100*CPU_PORT), 0x20ff0003);	//port6, Egress VLAN Tag Attribution=tagged
	mt7628_reg_write((REG_ESW_PORT_PCR_P0 + 0x100*P7_PORT), 0x20ff0003);	//port7, Egress VLAN Tag Attribution=tagged

	build_wan_lan_mask(type);
	_dprintf("%s: LAN/WAN/WANS_LAN portmask %08x/%08x/%08x\n", __func__, lan_mask, wan_mask, wans_lan_mask);

	//set PVID
	for (i = 0, m = 1; i < NR_WANLAN_PORT; ++i, m <<= 1) {
		if (lan_mask & m)
			v = 1;	//LAN
		else if (wanscap_lan && (wans_lan_mask & m))
			v = wans_lan_vid;	//WANSLAN
		else
			v = 2;	//WAN
		_dprintf("%s: P%d PVID %d\n", __func__, i, v);
		mt7628_reg_write((REG_ESW_PORT_PPBV1_P0 + 0x100*i), 0x10000 | v);
	}
	mt7628_reg_write((REG_ESW_PORT_PPBV1_P0 + 0x100*P5_PORT), 0x10001);

	//VLAN member port: WAN, LAN, WANS_LAN
	//LAN: P7, P6, P5, lan_mask
	__create_port_map(0xE0 | lan_mask, portmap);
	mt7628_vlan_set(0, 1, portmap, 0);

	if (sw_mode == SW_MODE_ROUTER) {
		switch (wanscap_wanlan) {
		case WANSCAP_WAN | WANSCAP_LAN:
			//WAN: P7, P6, wan_mask
			__create_port_map(0xC0 | wan_mask, portmap);
			mt7628_vlan_set(1, 2, portmap, 0);
			//WANSLAN: P6, wans_lan_mask
			__create_port_map(0x40 | wans_lan_mask, portmap);
			mt7628_vlan_set(2, wans_lan_vid, portmap, 0);
			break;
		case WANSCAP_LAN:
			//WANSLAN: P7, P6, wans_lan_mask
			__create_port_map(0xC0 | wans_lan_mask, portmap);
			mt7628_vlan_set(1, 2, portmap, 0);
			break;
		case WANSCAP_WAN:
			//WAN: P7, P6, wan_mask
			__create_port_map(0xC0 | wan_mask, portmap);
			mt7628_vlan_set(1, 2, portmap, 0);
			break;
		default:
			_dprintf("%s: Unknown WANSCAP %x\n", __func__, wanscap_wanlan);
		}
	}
#endif
	switch_fini();
}

static void get_mt7628_esw_WAN_Speed(unsigned int *speed)
{
	int i;
	unsigned int v = 0, m;

	if (switch_init() < 0)
		return;

	m = (get_wan_port_mask(0) | get_wan_port_mask(1)) & ((1U << NR_WANLAN_PORT) - 1);
	for (i = 0; m && i < NR_WANLAN_PORT; ++i, m >>= 1) {
		if (!(m & 1))
			continue;

#if 0
		mt7628_reg_read((REG_ESW_MAC_PMSR_P0 + 4*i), (unsigned int*) &t);
		t = (t >> 2) & 0x3;
		if (t < 3 && t > v)
			v = t;
#endif
		mt7628_reg_read(REG_ESW_POA, &v);
		//printf("offset=%4x, value=%8x\n", REG_ESW_POA, v);
		v = (v >> i) & 0x1;
	}

	switch (v) {
	case 0x0:
		*speed = 10;
		break;
	case 0x1:
		*speed = 100;
		break;
	case 0x2:
		*speed = 1000;
		break;
	default:
		_dprintf("%s: invalid speed!\n", __func__);
	}
	
	switch_fini();
}

static void link_down_up_mt7628_PHY(unsigned int mask, int status, int inverse)
{
//#if defined(RTCONFIG_RALINK_MT7628)
#if 0	
	printf("%s: mask: %d\tstatus: %d\tinverse: %d\n", __func__, mask, status, inverse);
	int i;
	unsigned int v, m;

	if (switch_init() < 0)
		return;

	mt7628_reg_read(REG_ESW_POC0, &v);
	printf("READ REG_ESW_POC0: %u\n", v);
	
	for (i = 0, m = mask; m && i < NR_WANLAN_PORT; ++i, m >>= 1) {
		if (!(m & 1))
			continue;
		if (status) // power up PHY
			v &= (0 << (i + 23));
		else // power down PHY
			v |= (1 << (i + 23));
	}
	printf("WRITE REG_ESW_POC0: %u\n", v);
	mt7628_reg_write(REG_ESW_POC0, v);

	switch_fini();
		
#else
	int i;
	char idx[2];
	char value[5] = "3300";	//power up PHY
	unsigned int m;

	if (!status)		//power down PHY
		value[1] = '9';

	for (i = 0, m = mask; m && i < NR_WANLAN_PORT; ++i, m >>= 1) {
		if (!(m & 1))
			continue;
		sprintf(idx, "%d", i);
		eval("mii_mgr", "-s", "-p", idx, "-r", "0", "-v", value);
	}
#endif
}

void set_mt7628_esw_broadcast_rate(int bsr)
{
#if 0
	int i;
	unsigned int value;
#endif

	if ((bsr < 0) || (bsr > 255))
		return;

	if (switch_init() < 0)
		return;

	printf("set broadcast strom control rate as: %d\n", bsr);
#if 0
	for (i = 0; i < NR_WANLAN_PORT; i++) {
		mt7628_reg_read((REG_ESW_PORT_BSR_P0 + 0x100*i), &value);
		value |= 0x1 << 31; //STRM_MODE=Rate-based
		value |= (bsr << 16) | (bsr << 8) | bsr;
		mt7628_reg_write((REG_ESW_PORT_BSR_P0 + 0x100*i), value);
	}
#endif
	switch_fini();
}

void reset_mt7628_esw(void)
{
#if defined(RTCONFIG_RALINK_MT7628)
	if (switch_init() < 0)
		return;

	mt7628_reg_write(REG_ESW_STRT, 0xffffffff);
	usleep(3000);
	
	switch_fini();
#else
	unsigned int value;

	if (switch_init() < 0)
		return;

	//Reset ARL engine (clear vlan table)
	mt7628_reg_read(0xc, &value);
	value &= 0xfffffffe;
	mt7628_reg_write(0xc, value);
	usleep(3000);
	mt7628_reg_read(0xc, &value);
	value |= 0x1;
	mt7628_reg_write(0xc, value);

	//set to default value
	{
		int i;
		for(i = 0; i < 8; i++)
		{
			value = ((i<<1)+1) | (((i<<1)+2) << 12);
			mt7628_reg_write(REG_ESW_VLAN_ID_BASE + 4*i, value);
		}
		for(i = 0; i < 16; i++)
		{
			value = (0x80000000 | (2<<12)) + i;	//set VLAN to invalid
			write_VTCR(value);
		}
	}

	switch_fini();
#endif
}

#if defined(RTCONFIG_RALINK_MT7628)
static void set_Port_PVID(int vid, int bitmap)
{
	unsigned int value = 0;
	unsigned short i;

	if (switch_init() < 0)
		return;

	for (i = 0; i < 7; i++) {
		if (bitmap & (1<<i)) {
			/* Set PVID */
			mt7628_reg_read(REG_ESW_PVIDC0 + 4*(i/2), &value);
			if (i % 2) { /* 23:12 bits */
				value &= 0xfff;
				value |= (vid << 12);
			}
			else { /* 11:0 bits */
				value &= 0xfff000;
				value |= vid;
			}

			mt7628_reg_write(REG_ESW_PVIDC0 + 4*(i/2), value);
		}
	}

	switch_fini();
}

static void set_Port_PRIO(int prio, int bitmap)
{
	if (prio == 0) // Prio 0 is default. So skip.
		return;

	if (switch_init() < 0)
		return;
	unsigned int value = 0, value2 = 0;
	unsigned short i = 0;
	unsigned short high_prio = nvram_get_int("switch_wan1prio"); 
	unsigned short low_prio = nvram_get_int("switch_wan2prio");
	unsigned short tmp = 0, q_number = 1;

	/* Get switch_wan1prio switch_wan2prio */
	if (low_prio > high_prio) {
		tmp = low_prio;
		low_prio = high_prio;
		high_prio = tmp;
	}

	if (high_prio > 0) { // Q1 default is 0
		if (low_prio <= 0 || (high_prio == low_prio)) { // Just set Q2
			if (high_prio != 7 && high_prio != 5 && high_prio != 2) { // Q3 default is 7. Q2 default is 5. Q0 default is 2.
				/* Set Q2 */
				mt7628_reg_read(REG_ESW_PVIDC3, &value);
				value &= 0xf0ffffff;
				value += (high_prio << 24);
				mt7628_reg_write(REG_ESW_PVIDC3, value);
			}
		}
		else { // Set Q3 Q2
			if (low_prio != 5 && low_prio != 2 ) { // Q2 default is 5. Q0 default is 2.
				/* Set Q2 */
				mt7628_reg_read(REG_ESW_PVIDC3, &value);
				value &= 0xf0ffffff;
				value += (low_prio << 24);
				mt7628_reg_write(REG_ESW_PVIDC3, value);
			}
			if (high_prio != 7 && high_prio != 2) {
				/* Set Q3 */
				mt7628_reg_read(REG_ESW_PVIDC3, &value);
				value &= 0x0fffffff;
				value += (high_prio << 28);
				mt7628_reg_write(REG_ESW_PVIDC3, value);
			}
		}
	}

	/* Find prio queue number */
	mt7628_reg_read(REG_ESW_PVIDC3, &value);
	value = value >> 16;

	for (i = 0; i < 4; i++) {
		value2 = value >> (i*4);
		if ((value2 & 0xf) == prio) {
			q_number = i;
			break;
		}
	}

	/* Set default queue per port */
	mt7628_reg_read(REG_ESW_PFC1, &value);
	for (i = 0; i < 7; i++) {
		
		if (bitmap & (1<<i)) {
			value &= (0xffffffff - (0x3 << (i*2)));
			value += (q_number << (i*2));
		}
	}
	mt7628_reg_write(REG_ESW_PFC1, value);
	
	switch_fini();
}
#endif
static void set_Vlan_VID(int vid)
{
	char tmp[8];

	sprintf(tmp, "%d", vid);
	nvram_set("vlan_vid", tmp);
}

static void set_Vlan_PRIO(int prio)
{
	char tmp[2];

	sprintf(tmp, "%d", prio);
	nvram_set("vlan_prio", tmp);
}

//convert port mapping from  RT-N56U   to   RT-N14U / RT-AC52U / RT-AC51U (mt7628) /RT-N54U
static int convert_port_bitmask(int orig)
{
	int i, mask, result;
	result = 0;
	for(i = 0; i < NR_WANLAN_PORT; i++) {
		mask = (1 << i);
		if (orig & mask)
			result |= (1 << switch_port_mapping[i]);
	}
	return result;
}


/**
 * @stb_bitmask:	bitmask of STB port(s)
 * 			e.g. bit0 = P0, bit1 = P1, etc.
 */
static void initialize_Vlan(int stb_bitmask)
{
	char portmap[16];
	int wans_lan_vid = 3, wanscap_wanlan = get_wans_dualwan() & (WANSCAP_WAN | WANSCAP_LAN);
	int wanscap_lan = get_wans_dualwan() & WANSCAP_LAN;
	int wans_lanport = nvram_get_int("wans_lanport");
	int sw_mode = nvram_get_int("sw_mode");

	if (switch_init() < 0)
		return;
#if defined(RTCONFIG_RALINK_MT7628)
	unsigned int value;
	/* Enable VLAN check per port. */
	mt7628_reg_read(REG_ESW_PFC1, &value);
	value |= (0x7f << 16);
	mt7628_reg_write(REG_ESW_PFC1, value);
#endif
	if (sw_mode == SW_MODE_AP || sw_mode == SW_MODE_REPEATER)
		wanscap_lan = 0;

	if (wanscap_lan && (wans_lanport < 0 || wans_lanport > 4)) {
		_dprintf("%s: invalid wans_lanport %d!\n", __func__, wans_lanport);
		wanscap_lan = 0;
		wanscap_wanlan &= ~WANSCAP_LAN;
	}

	if (wanscap_lan && (!(get_wans_dualwan() & WANSCAP_WAN)) && !(!nvram_match("switch_wantag", "none") && !nvram_match("switch_wantag", "")))
		wans_lan_vid = 2;

	build_wan_lan_mask(0);
	stb_bitmask = convert_port_bitmask(stb_bitmask);
	lan_mask &= ~stb_bitmask;
	wan_mask |= stb_bitmask;
	_dprintf("%s: LAN/WAN/WANS_LAN portmask %08x/%08x/%08x\n", __func__, lan_mask, wan_mask, wans_lan_mask);

	//VLAN member port: LAN, WANS_LAN
	//LAN: P7, P6, P5, lan_mask
#if defined(RTCONFIG_RALINK_MT7628)
	__create_port_map(0x40 | lan_mask, portmap); // P6: CPU port
#else
	__create_port_map(0xE0 | lan_mask, portmap);
#endif
	mt7628_vlan_set(0, 1, portmap, 1);
	if (wanscap_lan) {
		switch (wanscap_wanlan) {
		case WANSCAP_WAN | WANSCAP_LAN:
			//WANSLAN: P6, wans_lan_mask
			__create_port_map(0x40 | wans_lan_mask, portmap);
			mt7628_vlan_set(3, wans_lan_vid, portmap, wans_lan_vid);
			break;
		case WANSCAP_LAN:
			if (!nvram_match("switch_wantag", "none") && !nvram_match("switch_wantag", "")) {
				//WANSLAN: P6, wans_lan_mask
				__create_port_map(0x40 | wans_lan_mask, portmap);
			} else {
				//WANSLAN: P7, P6, wans_lan_mask
				__create_port_map(0xC0 | wans_lan_mask, portmap);
			}
			mt7628_vlan_set(3, wans_lan_vid, portmap, wans_lan_vid);
			break;
		case WANSCAP_WAN:
			/* Do nothing. */
			break;
		default:
			_dprintf("%s: Unknown WANSCAP %x\n", __func__, wanscap_wanlan);
		}
	}

	switch_fini();
}

#if defined(RTN14U) || defined(RTAC52U) || defined(RTAC51U) || defined(RTN11P) || defined(RTN54U)
static void fix_up_hwnat_for_wifi(void)
{
	int i, j, m, r, v, isp_profile_hwnat_not_safe = 0;
	char portmap[10];
	char bss[] = "wl0.1_bss_enabledXXXXXX";
	char mode_x[] = "wl0_mode_xXXXXXX";
	struct wifi_if_vid_s w = {
#if defined(RTAC52U) || defined(RTAC51U) || defined(RTN54U)
		.wl_vid = { 21, 43 },		/* DP_RA0  ~ DP_RA3:  21, 22, 23, 24;	DP_RAI0  ~ DP_RAI3:  43, 44, 45, 46 */
		.wl_wds_vid = { 37, 59 },	/* DP_WDS0 ~ DP_WDS3: 37, 38, 39, 40;	DP_WDSI0 ~ DP_WDSI3: 59, 60, 61, 62 */
#elif defined(RTN14U) || defined(RTN11P)
		.wl_vid = { 21, -1 },		/* DP_RA0  ~ DP_RA3:  21, 22, 23, 24;	DP_RAI0  ~ DP_RAI3:  43, 44, 45, 46 */
		.wl_wds_vid = { 37, -1 },	/* DP_WDS0 ~ DP_WDS3: N/A;           	DP_WDSI0 ~ DP_WDSI3: N/A */
#else
#error Define VLAN ID of WiFi interface!
#endif
	};

	if (nvram_match("switch_wantag", "none") || nvram_match("switch_wantag", "")) {
		nvram_unset("isp_profile_hwnat_not_safe");
		return;
	}

	if (switch_init() < 0)
		return;

	/* Create VLANs on P6 and P7 for WiFi interfaces to make sure VLAN ID of skbs
	 * that come from WiFi interface and are injected to PPE is not swapped as zero.
	 * This is used to workaround VirIfIdx=0 problem.
	 */
	strcpy(portmap, "00000011");	/* P6, P7 */
	/* Initial state, 2G/5G interface only. */
	for (i = 0; i <= 1; ++i) {
		if ((v = w.wl_vid[i]) <= 0)
			continue;

		if ((r = mt7628_vlan_set(-1, v, portmap, v)) != 0)
			isp_profile_hwnat_not_safe = 1;
		for (j = 1; j <= 3; ++j)
			mt7628_vlan_unset(v + j);

		if ((v = w.wl_wds_vid[i]) <= 0)
			continue;
		for (j = 0; j <= 3; ++j, ++v)
			mt7628_vlan_unset(v);
	}

	/* 2G/5G guest network i/f */
	for (i = 0; !isp_profile_hwnat_not_safe && i <= 1; ++i) {
		if ((v = w.wl_vid[i]) <= 0)
			continue;

		sprintf(mode_x, "wl%d_mode_x", i);
		m = nvram_get_int(mode_x);	/* 1: WDS only */
		for (j = 1; !isp_profile_hwnat_not_safe && j <= 3; ++j) {
			sprintf(bss, "wl%d.%d_bss_enabled", i, j);
			if (m == 1 || !nvram_get_int(bss))
				continue;

			v++;
			if ((r = mt7628_vlan_set(-1, v, portmap, v)) != 0)
				isp_profile_hwnat_not_safe = 1;
		}
	}

	/* 2G/5G WDS i/f */
	for (i = 0; !isp_profile_hwnat_not_safe && i <= 1; ++i) {
		if ((v = w.wl_wds_vid[i]) <= 0)
			continue;

		sprintf(mode_x, "wl%d_mode_x", i);
		m = nvram_get_int(mode_x);
		if (m != 1 && m != 2)
			continue;
		for (j = 0; !isp_profile_hwnat_not_safe && j <= 3; ++j, ++v) {
			if ((r = mt7628_vlan_set(-1, v, portmap, v)) != 0)
				isp_profile_hwnat_not_safe = 1;
		}
	}
	nvram_set_int("isp_profile_hwnat_not_safe", isp_profile_hwnat_not_safe);

	switch_fini();
}
#else
static inline void fix_up_hwnat_for_wifi(void) { }
#endif

/**
 * Create VLAN for LAN and/or WAN in accordance with bitmask parameter.
 * @bitmask:
 *  bit15~bit0:		member port bitmask.
 * 	bit0:		RT-N56U port0, LAN4
 * 	bit1:		RT-N56U port1, LAN3
 * 	bit2:		RT-N56U port2, LAN2
 * 	bit3:		RT-N56U port3, LAN1
 * 	bit4:		RT-N56U port4, WAN
 * 	bit8:		RT-N56U port8, LAN_CPU port
 * 	bit9:		RT-N56U port9, WAN_CPU port
 *  bit31~bit16:	untag port bitmask.
 * 	bit16:		RT-N56U port0, LAN4
 * 	bit17:		RT-N56U port1, LAN3
 * 	bit18:		RT-N56U port2, LAN2
 * 	bit19:		RT-N56U port3, LAN1
 * 	bit20:		RT-N56U port4, WAN
 * 	bit24:		RT-N56U port8, LAN_CPU port
 * 	bit25:		RT-N56U port9, WAN_CPU port
 * First Ralink-based model is RT-N56U.
 * Convert RT-N56U-specific bitmask to physical port of your model,
 * base on relationship between physical port and visual WAN/LAN1~4 of that model first.
 */
static void create_Vlan(int bitmask)
{
#if defined(RTCONFIG_RALINK_MT7628)
	char untag_portmap[16];
	unsigned int value, untag_mask = 0, pvid_mask = 0;
	int idx;
#else
	unsigned int value;
#endif
	char portmap[16];
	int vid = nvram_get_int("vlan_vid") & 0xFFF;
	int prio = nvram_get_int("vlan_prio") & 0x7;
	int mbr = bitmask & 0xffff;
	int untag = (bitmask >> 16) & 0xffff;
	int i, mask;

	if (switch_init() < 0)
		return;
#if defined(RTCONFIG_RALINK_MT7628)
	/* Set PVID & VLAN member port */
	strcpy(portmap, "00000000");
	strcpy(untag_portmap, "00000000");
	/* Convert port mapping */
	for (i = 0; i < NR_WANLAN_PORT; i++) {
		mask = (1 << i);
		if (mbr & mask)
			portmap[switch_port_mapping[i]] = '1';
	}
	for (i = 0; i < NR_WANLAN_PORT; i++) {
		mask = (1 << i);
		if (untag & mask)
			untag_portmap[switch_port_mapping[i]] = '1';
	}

	if (mbr & 0x200) {
		portmap[CPU_PORT]='1';
		/* Set WAN VID */
		mt7628_vlan_set(1, vid, portmap, vid);
		/* Tag or untag */
		for (i = 0; i < 7 ; i++) {
			if (i != CPU_PORT && i != WAN_PORT && i != LAN4_PORT && i != 7) // Skip CPU/WAN
				untag_mask += (1 << i);
		}
		set_Vlan_untag(1, untag_mask);
	}
	else {

		if (mt7628_vlan_set(-1, vid, portmap, vid) < 0)
			return;
		idx = find_vlan_slot(vid, &value); // Just get idx.

		if (idx < 0)
			return; // It was not supposed to happen.

		/* Rewrite valid member bits */
		mbr = 0;
		for (i = 0; i < 8; i++)
			mbr += (*(portmap + i) - '0') * (1 << i);
		untag = 0;
		for (i = 0; i < 8; i++)
			untag += (*(untag_portmap + i) - '0') * (1 << i);
		for(i = 0; i < NR_WANLAN_PORT; i++) //LAN port only
		{
			if (i == WAN_PORT)
				continue;

			mask = (1 << i);
			if (mbr & mask) {
				if (untag & mask) { // set untag mask
					untag_mask += (1 << i);
					pvid_mask += (1 << i); // set pvid mask
				}
			}
		}
		if (pvid_mask != 0) {
			set_Port_PVID(vid, pvid_mask);
			set_Port_PRIO(prio, pvid_mask);
		}
		if (untag_mask != 0)
			set_Vlan_untag(idx, untag_mask);

		fix_up_hwnat_for_wifi();
	}

	switch_fini();
#else
	//set PVID & VLAN member port
	value = (0x1 << 16) | (prio << 13) | vid;

	strcpy(portmap, "00000000"); // init
	//convert port mapping
	for(i = 0; i < NR_WANLAN_PORT; i++) {
		mask = (1 << i);
		if (mbr & mask)
			portmap[ switch_port_mapping[i] ]='1';
	}

	if (mbr & 0x200) {	//Internet && WAN port
		portmap[CPU_PORT]='1';
		portmap[P7_PORT]='1';
		mt7628_reg_write((REG_ESW_PORT_PCR_P0 + 0x100*WAN_PORT), 0x10ff0003); //Egress VLAN Tag Attribution=swap
		mt7628_reg_write((REG_ESW_PORT_PCR_P0 + 0x100*CPU_PORT), 0x10ff0003); //port6(CPU), Egress VLAN Tag Attribution=swap
		mt7628_reg_write((REG_ESW_PORT_PVC_P0 + 0x100*WAN_PORT), 0x81000000); //user port, admit all frames
		mt7628_vlan_set(1, 2, portmap, vid);
		mt7628_vlan_set(2, vid, portmap, 2);
	}
	else {	//IPTV, VoIP port
		for(i = 0; i < 4; i++) //LAN port only
		{
			mask = (1 << i);
			if (mbr & mask) {
				if ((untag & mask) == 0) {	//need tag
					mt7628_reg_write((REG_ESW_PORT_PCR_P0 + 0x100 * switch_port_mapping[i]), 0x20ff0003); //Egress VLAN Tag Attribution=tagged
					mt7628_reg_write((REG_ESW_PORT_PVC_P0 + 0x100 * switch_port_mapping[i]), 0x81000000); //user port, admit all frames
				}
				else {
					mt7628_reg_write((REG_ESW_PORT_PPBV1_P0 + 0x100 * switch_port_mapping[i]), value);
				}
			}
		}
		mt7628_vlan_set(-1, vid, portmap, vid);

		fix_up_hwnat_for_wifi();
	}
#endif
	switch_fini();
}

static void is_singtel_mio(int is)
{
	int i;
	unsigned int value;

	if (!is)
		return;

	if (switch_init() < 0)
		return;
#if defined(RTCONFIG_RALINK_MT7628)
	/* No Support to setting admit all frames on MT7628. */
#else
	for (i = 0; i < NR_WANLAN_PORT; i++) { //WAN/LAN, admit all frames
		mt7628_reg_read((REG_ESW_PORT_PVC_P0 + 0x100*i), &value);
		value &= 0xfffffffc;
		mt7628_reg_write((REG_ESW_PORT_PVC_P0 + 0x100*i), value);
	}

#endif
	switch_fini();
}

#if defined(RTCONFIG_RALINK_MT7628) // This is just for test.
int mt7628_ioctl(int val, int val2, int val3)
#else
int mt7628_ioctl(int val, int val2)
#endif
{
//	int value = 0;
	unsigned int value2 = 0;
	int i, max_wan_unit = 0;

#if defined(RTCONFIG_DUALWAN)
	max_wan_unit = 1;
#endif

	switch (val) {
	case 0:
		value2 = rtkswitch_wanPort_phyStatus(-1);
		printf("WAN link status : %u\n", value2);
		break;
	case 3:
		value2 = rtkswitch_lanPorts_phyStatus();
		printf("LAN link status : %u\n", value2);
		break;
	case 8:
		config_mt7628_esw_LANWANPartition(val2);
		break;
	case 13:
		get_mt7628_esw_WAN_Speed(&value2);
		printf("WAN speed : %u Mbps\n", value2);
		break;
	case 14: // Link up LAN ports
		link_down_up_mt7628_PHY(get_lan_port_mask(), 1, 0);
		break;
	case 15: // Link down LAN ports
		link_down_up_mt7628_PHY(get_lan_port_mask(), 0, 0);
		break;
	case 16: // Link up ALL ports
		link_down_up_mt7628_PHY(0x1F, 1, 0);
		break;
	case 17: // Link down ALL ports
		link_down_up_mt7628_PHY(0x1F, 0, 0);
		break;
	case 21:
		break;
	case 22:
		break;
	case 23:
		break;
	case 24:
		break;
	case 25:
		set_mt7628_esw_broadcast_rate(val2);
		break;
	case 27:
		reset_mt7628_esw();
		break;
	case 36:
		set_Vlan_VID(val2);
		break;
	case 37:
		set_Vlan_PRIO(val2);
		break;
	case 38:
		initialize_Vlan(val2);
		break;
	case 39:
		create_Vlan(val2);
		break;
	case 40:
#if !defined(RTCONFIG_RALINK_MT7628) // MT7628. No support.
		is_singtel_mio(val2);
#endif
		break;
	case 50:
		fix_up_hwnat_for_wifi();
		break;
	case 114: // link up WAN ports
		for (i = WAN_UNIT_FIRST; i <= max_wan_unit; ++i)
			link_down_up_mt7628_PHY(get_wan_port_mask(i), 1, 1);
		break;
	case 115: // link down WAN ports
		for (i = WAN_UNIT_FIRST; i <= max_wan_unit; ++i)
			link_down_up_mt7628_PHY(get_wan_port_mask(i), 0, 1);
		break;
	case 200:	/* set LAN port number that is used as WAN port */
		/* Nothing to do, nvram_get_int("wans_lanport ") is enough. */
		break;
	case 300:
		mt7628_reg_read(0x80, &value2);
		printf("offset=0x80, value=%8x\n", value2);
		value2 = 0;
		mt7628_reg_read(0x84, &value2);
		printf("offset=0x84, value=%8x\n", value2);		
		break;
#if defined(RTCONFIG_RALINK_MT7628) // This is just for test.
	case 500: // for read test.
		if (switch_init() < 0)
			break;
		mt7628_reg_read(val2, &value2);
		_dprintf("offset=0x%x, value=0x%x\n", val2, value2);

		switch_fini();
		break;
	case 501: // for write test.
		if (switch_init() < 0)
			break;
		mt7628_reg_write(val2, val3);
		_dprintf("offset=0x%x, value=0x%x\n", val2, val3);

		switch_fini();
		break;
#endif
	default:
		printf("wrong ioctl cmd: %d\n", val);
	}

	return 0;
}

int config_rtkswitch(int argc, char *argv[])
{
	int val;
	int val2 = 0;
	char *cmd = NULL;
	char *cmd2 = NULL;

#if defined(RTCONFIG_RALINK_MT7628) // This is just for test.
	int val3 = 0;
	char *cmd3 = NULL;
#endif
	if (argc >= 2)
		cmd = argv[1];
	else
		return -1;
	if (argc >= 3)
		cmd2 = argv[2];
#if defined(RTCONFIG_RALINK_MT7628) // This is just for test.
	if (argc >= 4)
		cmd3 = argv[3];
	if (cmd3)
		val3 = (int) strtol(cmd3, NULL, 0);
#endif
	val = (int) strtol(cmd, NULL, 0);
	if (cmd2)
		val2 = (int) strtol(cmd2, NULL, 0);
#if defined(RTCONFIG_RALINK_MT7628) // This is just for test.
	return mt7628_ioctl(val, val2, val3);
#else
	return mt7628_ioctl(val, val2);
#endif
}

int
ralink_set_gpio_mode(unsigned int group)
{
	int fd;
 	unsigned long value;
	unsigned int req;
	
	fd = open(GPIO_DEV, O_RDONLY);
	if (fd < 0) {
		perror(GPIO_DEV);
		return -1;
	}
	req = RALINK_SET_GPIO_MODE;
	//_dprintf("set gpio group =%d  as gpio mode\n",group);
	req= req | (group<<24); 
	value=0;
	if (ioctl(fd, req, &value) < 0) {
		perror("ioctl");
		close(fd);
		return -1;
	}
	close(fd);
	return 0;
	
}

/*
typedef struct {
	unsigned int idx;
	unsigned int value;
} asus_gpio_info;
*/
int
ralink_gpio_write_bit(int idx, int value)
{
	int fd;
       unsigned int req;

	fd = open(GPIO_DEV, O_RDONLY);
	if (fd < 0) {
		perror(GPIO_DEV);
		return -1;
	}
	
	if (idx<=31)
	{
		if(value)
		   req=RALINK_GPIO3100_SET;
		else
		   req=RALINK_GPIO3100_CLEAR;
	
	}   
	else if (idx>31 && idx<=63)
	{	
  	  	idx -=32;
		if(value)
		   req=RALINK_GPIO6332_SET;
		else
		   req=RALINK_GPIO6332_CLEAR;
	}	
	else if (idx>63 && idx <=95)
	{	
  	 	idx -=64;
		if(value)
		   req=RALINK_GPIO9564_SET;
		else
		   req=RALINK_GPIO9564_CLEAR;
	}	
	else if (idx==96) 
	{              
#if defined (RTAC1200) || defined (RTN11P_B1)//wlan led
		req=RALINK_ATE_GPIO96;
		idx=value;
#else
#endif		
	}   
	else
		return -1;

	if (ioctl(fd, req, (1<<idx)) < 0) {
		perror("ioctl");
		close(fd);
		return -1;
	}
	close(fd);
	return 0;
}

int
ralink_gpio_read_bit(int idx)
{
 	unsigned long value;
	int fd;
	unsigned int req;

	value = 0;
	fd = open(GPIO_DEV, O_RDONLY);
	if (fd < 0) {
		perror(GPIO_DEV);
		return -1;
	}


	if(idx<=31)   
		 req = RALINK_GPIO3100_READ;
	else if (idx>31 && idx<=63)
	{
		idx-=32;   
	   	 req= RALINK_GPIO6332_READ;
	}	 
	else if (idx>63 && idx <=95)
	{	 
	   	idx-=64;
   	 	req= RALINK_GPIO9564_READ;
	}	
	else
		return -1;

	if (ioctl(fd, req, &value) < 0) {
		perror("ioctl");
		close(fd);
		return -1;
	}
	close(fd);

	value=(value>>idx)&1;
  	return value;
}

int
ralink_gpio_set_gpiomode(unsigned int idx, int isgpio)
{
	int fd;
	unsigned long arg;


	fd = open(GPIO_DEV, O_RDONLY);
	if (fd < 0) {
		perror(GPIO_DEV);
		return -1;
	}

	arg = ((isgpio & 0xffff) << 16) | (idx & 0xffff);

	if (ioctl(fd, RALINK_GPIO_SET_MODE, arg) < 0) {
		perror("ioctl(RALINK_GPIO_SET_MODE)");
		close(fd);
		return -1;
	}
	close(fd);
	return 0;
}

int
ralink_gpio_init(unsigned int idx, int dir)
{
	int fd, req;
	unsigned long arg;
	
#if defined(RTAC1200)
	if(idx==96) //discard gpio96
		return 0;
#endif

	ralink_gpio_set_gpiomode(idx, 1);

	fd = open(GPIO_DEV, O_RDONLY);
	if (fd < 0) {
		perror(GPIO_DEV);
		return -1;
	}
	if (dir == gpio_in) {
		if (idx <= 31)  //gpio 0~31
		{   
			req = RALINK_GPIO3100_SET_DIR_IN;
			arg=1<<idx;
		}	
		else if ((idx > 31) && (idx <= 63))  //gpio 32~63
		{	
	   	 	  req = RALINK_GPIO6332_SET_DIR_IN;
			  arg=1<<(idx-32);
		}	  
		else if ((idx > 63) && (idx <= 95))  //gpio 64~95
		{	
	   		  req = RALINK_GPIO9564_SET_DIR_IN;
			  arg=1<<(idx-64);
		}	  
		else
			return -1;
	}
	else {
		if (idx <= 31)  //gpio 0~31
		{   
			req = RALINK_GPIO3100_SET_DIR_OUT;
			arg=1<<idx;
		}	
		else if ((idx > 31) && (idx <= 63))  //gpio 32~63
		{	
	   	 	 req = RALINK_GPIO6332_SET_DIR_OUT;
			 arg=1<<(idx-32);
		}	 
		else if ((idx > 63) && (idx <= 95))  //gpio 64~95
		{	
	   	 	  req = RALINK_GPIO9564_SET_DIR_OUT;
			  arg=1<<(idx-64);
		}		
		else
			return -1;
	}

	if (ioctl(fd, req, arg) < 0) {
		perror("ioctl");
		close(fd);
		return -1;
	}
	close(fd);
	return 0;
}



// a wrapper to broadcom world
// bit 1: LED_POWER
// bit 6: BTN_RESET
// bit 8: BTN_WPS

uint32_t ralink_gpio_read(void)
{
	uint32_t r=0;
         if(ralink_gpio_read_bit(RA_BTN_RESET)) r|=(1<<6);
	 if(ralink_gpio_read_bit(RA_BTN_WPS)) r|=(1<<8);
	return r;
}

int ralink_gpio_write(uint32_t bit, int en)
{
       if(bit&(1<<1))
       {
            if(!en) ralink_gpio_write_bit(RA_LED_POWER, RA_LED_OFF);
            else ralink_gpio_write_bit(RA_LED_POWER, RA_LED_ON);
       }
	return 0;
}

unsigned int
rtkswitch_wanPort_phyStatus(int wan_unit)
{
	unsigned int status = 0;

	get_mt7628_esw_phy_linkStatus(get_wan_port_mask(wan_unit), &status);

	return status;
}

unsigned int
rtkswitch_lanPorts_phyStatus(void)
{
	unsigned int status = 0;

	get_mt7628_esw_phy_linkStatus(get_lan_port_mask(), &status);

	return status;
}

unsigned int
rtkswitch_WanPort_phySpeed(void)
{
	unsigned int speed;

	get_mt7628_esw_WAN_Speed(&speed);

	return speed;
}

int rtkswitch_WanPort_linkUp(void)
{
	eval("rtkswitch", "114");

	return 0;
}

int rtkswitch_WanPort_linkDown(void)
{
	eval("rtkswitch", "115");

	return 0;
}

int
rtkswitch_LanPort_linkUp(void)
{
	system("rtkswitch 14");

	return 0;
}

int
rtkswitch_LanPort_linkDown(void)
{
	system("rtkswitch 15");

	return 0;
}

int
rtkswitch_AllPort_linkUp(void)
{
	system("rtkswitch 16");
        
	return 0;
}

int
rtkswitch_AllPort_linkDown(void)
{
	system("rtkswitch 17");

	return 0;
}

int
rtkswitch_Reset_Storm_Control(void)
{
	system("rtkswitch 21");

	return 0;
}

void ATE_mt7628_esw_port_status(void)
{
	int i;
	unsigned int value;
	char buf[32];
	phyState pS;

	if (switch_init() < 0)
		return;

	for (i = 0; i < NR_WANLAN_PORT; i++) {
		pS.link[i] = 0;
		pS.speed[i] = 0;
		/*mt7628_reg_read((REG_ESW_MAC_PMSR_P0 + 0x100*i), &value);
		pS.link[i] = value & 0x1;
		pS.speed[i] = (value >> 2) & 0x3;*/
		mt7628_reg_read(REG_ESW_POA, &value);
		pS.link[i] = (value >> (REG_ESW_POA_LINK_STATUS_SHIFT + i)) & 0x1;
		pS.speed[i] = (value >> i) & 0x1;
	}

#if defined(PORT_W4321)
	sprintf(buf, "W0=%C;L4=%C;L3=%C;L2=%C;L1=%C;",
		(pS.link[ WAN_PORT] == 1) ? (pS.speed[ WAN_PORT] == 2) ? 'G' : 'M': 'X',
		(pS.link[LAN4_PORT] == 1) ? (pS.speed[LAN4_PORT] == 2) ? 'G' : 'M': 'X',
		(pS.link[LAN3_PORT] == 1) ? (pS.speed[LAN3_PORT] == 2) ? 'G' : 'M': 'X',
		(pS.link[LAN2_PORT] == 1) ? (pS.speed[LAN2_PORT] == 2) ? 'G' : 'M': 'X',
		(pS.link[LAN1_PORT] == 1) ? (pS.speed[LAN1_PORT] == 2) ? 'G' : 'M': 'X');
#else
	sprintf(buf, "W0=%C;L1=%C;L2=%C;L3=%C;L4=%C;",
		(pS.link[ WAN_PORT] == 1) ? (pS.speed[ WAN_PORT] == 2) ? 'G' : 'M': 'X',
		(pS.link[LAN1_PORT] == 1) ? (pS.speed[LAN1_PORT] == 2) ? 'G' : 'M': 'X',
		(pS.link[LAN2_PORT] == 1) ? (pS.speed[LAN2_PORT] == 2) ? 'G' : 'M': 'X',
		(pS.link[LAN3_PORT] == 1) ? (pS.speed[LAN3_PORT] == 2) ? 'G' : 'M': 'X',
		(pS.link[LAN4_PORT] == 1) ? (pS.speed[LAN4_PORT] == 2) ? 'G' : 'M': 'X');
#endif
	puts(buf);

	switch_fini();
}

#if 0
void usage(char *cmd)
{
	printf("Usage: %s 1 - set (SCK, SD) as (0, 1)\n", cmd);
	printf("       %s 2 - set (SCK, SD) as (1, 0)\n", cmd);
	printf("       %s 3 - set (SCK, SD) as (1, 1)\n", cmd);
	printf("       %s 0 - set (SCK, SD) as (0, 0)\n", cmd);
	printf("       %s 4 - init vlan\n", cmd);
	printf("       %s 5 - set cpu port 0 0\n", cmd);
	printf("       %s 6 - set cpu port 0 1\n", cmd);
	printf("       %s 7 - set cpu port 1 0\n", cmd);
	printf("       %s 8 - set cpu port 1 1\n", cmd);
	printf("       %s 10 - set vlan entry, no cpu port, but mbr\n", cmd);
	printf("       %s 11 - set vlan entry, no cpu port, no mbr\n", cmd);
	printf("       %s 15 - set vlan PVID, no cpu port\n", cmd);
	printf("       %s 20 - set vlan entry, with cpu port\n", cmd);
	printf("       %s 21 - set vlan entry, with cpu port and no cpu port in untag sets\n", cmd);
	printf("       %s 25 - set vlan PVID, with cpu port\n", cmd);
	printf("       %s 26 - set vlan PVID, not set cpu port\n", cmd);
	printf("       %s 90 - accept all frmaes\n", cmd);
	printf("       %s 66 - setup default vlan\n", cmd);
	printf("       %s 61 - setup vlan type1\n", cmd);
	printf("       %s 62 - setup vlan type2\n", cmd);
	printf("       %s 63 - setup vlan type3\n", cmd);
	printf("       %s 64 - setup vlan type4\n", cmd);
	printf("       %s 65 - setup vlan type34\n", cmd);
	printf("       %s 70 - disable multicast snooping\n", cmd);
	printf("       %s 81 - setRtctTesting on port x\n", cmd);
	printf("       %s 82 - getRtctResult on port x\n", cmd);
	printf("       %s 83 - setGreenEthernet x(green, powsav)\n", cmd);
	printf("       %s 84 - setAsicGreenFeature x(txGreen, rxGreen)\n", cmd);
	printf("       %s 85 - getAsicGreenFeature\n", cmd);
	printf("       %s 86 - enable GreenEthernet on port x\n", cmd);
	printf("       %s 87 - disable GreenEthernet on port x\n", cmd);
	printf("       %s 88 - getAsicPowerSaving x\n", cmd);
	printf("       %s 50 - getPortLinkStatus x\n", cmd);
	exit(0);
}
#endif
