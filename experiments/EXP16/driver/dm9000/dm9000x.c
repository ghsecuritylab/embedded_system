/*
  dm9000x.c: Version 1.25 04/28/2004
  
        A Davicom DM9000 ISA NIC fast Ethernet driver for Linux.
	Copyright (C) 1997  Sten Wang

	This program is free software; you can redistribute it and/or
	modify it under the terms of the GNU General Public License
	as published by the Free Software Foundation; either version 2
	of the License, or (at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.


  (C)Copyright 1997-1998 DAVICOM Semiconductor,Inc. All Rights Reserved.


V0.11	06/20/2001	REG_0A bit3=1, default enable BP with DA match
	06/22/2001 	Support DM9801 progrmming	
	 	 	E3: R25 = ((R24 + NF) & 0x00ff) | 0xf000
		 	E4: R25 = ((R24 + NF) & 0x00ff) | 0xc200
		     		R17 = (R17 & 0xfff0) | NF + 3
		 	E5: R25 = ((R24 + NF - 3) & 0x00ff) | 0xc200
		     		R17 = (R17 & 0xfff0) | NF
				
v1.00               	modify by simon 2001.9.5
                        change for kernel 2.4.x    
			
v1.1   11/09/2001      	fix force mode bug             

v1.2   03/18/2003       Weilun Huang <weilun_huang@davicom.com.tw>:
			Added tx/rx 32 bit mode.
			Cleaned up for kernel merge.
			
v1.21  08/26/2003       Fixed phy reset.

v1.22  09/09/2003       Fixed power-on reset.
       09/30/2003	Spenser Tsai
			Fixed 32-bit mode bug (Excessive Collision and Late Collision)

v1.23  11/05/2003	Added Auto-MDIX function
v1.24  01/15/2004	Modify Full-Duplex mode
v1.25  04/28/2004	Default Flow control function
v1.26 06/09/2004	Performance raising & reducing amount of interrupt 
*/

#if defined(MODVERSIONS)
#include <linux/modversions.h>
#endif
				
#include <linux/module.h>
#include <linux/ioport.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/version.h>
#include <asm/dma.h>
#include <asm/io.h>
#include <linux/spinlock.h>
#include <linux/delay.h>
//#include <linux/crc32.h>

/* Board/System/Debug information/definition ---------------- */

#define DM9000_ID		0x90000A46

#define DM9000_NCR		0x00
#define DM9000_NSR		0x01
#define DM9000_REG05		0x30	/* SKIP_CRC/SKIP_LONG */
#define DM9000_REG08		0x37
#define DM9000_REG09		0x38
#define DM9000_REG0A		0x29	/* Flow Control Enable */ 
#define DM9000_GPCR		0x1e	/* General purpose control register */
#define DM9000_GPR		0x1f	/* General purpose register */
#define DM9000_REGFF		0x83	/* IMR */

#define DM9000_PHY		0x40	/* PHY address 0x01 */
#define DM9000_PKT_MAX		1536	/* Received packet max size */
#define DM9000_PKT_RDY		0x01	/* Packet ready to receive */
#define DM9000_MIN_IO		0x300
#define DM9000_MAX_IO		0x370
#define DM9000_INT_MII		0x00
#define DM9000_EXT_MII		0x80

#define DM9000_BASE_ADDR_ETH0   0x08000000  //nGCS1
#define DM9000_BASE_ADDR_ETH1   0x10000000  //nGCS2
#define DM9000_MIN_IO_ETH0      (DM9000_BASE_ADDR_ETH0 + 0x300)
#define DM9000_MIN_IO_ETH1      (DM9000_BASE_ADDR_ETH1 + 0x300)


#define DM9000_VID_L		0x28
#define DM9000_VID_H		0x29
#define DM9000_PID_L		0x2A
#define DM9000_PID_H		0x2B

#define DM9801_NOISE_FLOOR	0x08
#define DM9802_NOISE_FLOOR	0x05

#define DMFE_SUCC       	0
#define MAX_PACKET_SIZE 	1514
#define DMFE_MAX_MULTICAST 	14

#define DM9000_RX_INTR		0x01
#define DM9000_TX_INTR		0x02
#define DM9000_OVERFLOW_INTR	0x04

#define DM9000_DWORD_MODE	1
#define DM9000_BYTE_MODE	2
#define DM9000_WORD_MODE	0

#define TRUE			1
#define FALSE			0

#define DMFE_TIMER_WUT  jiffies+(HZ*2)	/* timer wakeup time : 2 second */
#define DMFE_TX_TIMEOUT (HZ*2)		/* tx packet time-out time 1.5 s" */
#if defined(AUTOMDIX)
#define DMFE_TIMER_MDIX	jiffies+(HZ*1)  /* timer wakeup time : 1 second */
#endif

#if defined(DM9000_DEBUG)
#define DMFE_DBUG(dbug_now, msg, vaule)\
if (dmfe_debug||dbug_now) printk(KERN_ERR "dmfe: %s %x\n", msg, vaule)
#else
#define DMFE_DBUG(dbug_now, msg, vaule)\
if (dbug_now) printk(KERN_ERR "dmfe: %s %x\n", msg, vaule)

#endif

#define IICNETBUFSIZE 0x20
#define BIT_IIC		(0x1 << 27)
#define rGPECON		(*(volatile unsigned long *)net_r_GPECON)
#define rGPEUP		(*(volatile unsigned long *)net_r_GPEUP)
#define rIICCON		(*(volatile unsigned long *)net_r_IICCON)
#define rIICSTAT	(*(volatile unsigned long *)net_r_IICSTAT)
#define rIICADD		(*(volatile unsigned long *)net_r_IICADD)
#define rIICDS		(*(volatile unsigned long *)net_r_IICDS)
#define WRDATA		(1)
#define POLLACK		(2)
#define RDDATA		(3)
#define SETRDADDR	(4)
#define PDEBUG printk
#define Uart_Printf	PDEBUG
#define U8		unsigned char
#define U32		unsigned int
static unsigned char net_iicData[IICNETBUFSIZE];
static volatile int net_iicDataCount;
static volatile int net_iicStatus;
static volatile int net_iicMode;
static int net_iicPt;
void net_init_iic(void);
void net_iic_read(U32 slvaddr,U32 addr,U8 *buffer,U32 length);
void net_run_iicpoll(void);
void net_iicpoll(void);
int net_address_map(void);
unsigned long net_r_GPECON,net_r_GPEUP;
unsigned long net_r_IICCON,net_r_IICSTAT,net_r_IICADD,net_r_IICDS;

void net_delay(int times)
{
        udelay(1000);
}
int net_address_map(void)
{
        net_r_GPECON        =__ioremap(0x56000040,4,0);
        net_r_GPEUP         =__ioremap(0x56000048,4,0);
        net_r_IICCON        =__ioremap(0x54000000,4,0);
        net_r_IICSTAT       =__ioremap(0x54000004,4,0);
        net_r_IICADD        =__ioremap(0x54000008,4,0);
        net_r_IICDS         =__ioremap(0x5400000c,4,0);
                                                                                
        return 0;
}

void net_run_iicpoll(void)
{
    if(rIICCON & 0x10)
       net_iicpoll();
}
//**********************[IicPoll ]**************************************
void net_iicpoll(void)
{
    U32 iicSt,i;
                                                                                
    iicSt = rIICSTAT;
    if(iicSt & 0x8){
        PDEBUG("bus arbitration is failed\n");
                        }   //When bus arbitration is failed.
    if(iicSt & 0x4){
        PDEBUG("matched\n");
        }                   //When a slave address is matched with IICADD
    if(iicSt & 0x2){
        PDEBUG("slave address 0000000b\n");
        }                   //When a slave address is 0000000b
    if(iicSt & 0x1){
        PDEBUG("Ack isn't received\n");
        }                   //When ACK isn't received
                                                                                
    switch(net_iicMode)
    {
        case POLLACK:
                PDEBUG("poll--ACK\n");
            net_iicStatus = iicSt;
            break;
                                                                                
        case RDDATA:
              PDEBUG("read--ACK\n");
            if((net_iicDataCount--)==0)
            {
                net_iicData[net_iicPt++] = rIICDS;
                                                                                
                rIICSTAT = 0x90;                //Stop MasRx condition
                rIICCON  = 0xaf;                //Resumes IIC operation.
                net_delay(1);                       //Wait until stop condtion is in effect.
                                                //Too long time...
                                                //The pending bit will not be set after issuing stop condition.
                break;
            }
            net_iicData[net_iicPt++] = rIICDS;
                        //The last data has to be read with no ack.
            if((net_iicDataCount)==0)
                rIICCON = 0x2f;                 //Resumes IIC operation with NOACK.
            else
                rIICCON = 0xaf;           //Resumes IIC operation with ACK i
	    break;                    //hhtech add by wpq ,delay of readmac
                                                                                
        case WRDATA:
                PDEBUG("write--ACK\n");
            if((net_iicDataCount--)==0)
            {
                rIICSTAT = 0xd0;                //stop MasTx condition
                rIICCON  = 0xaf;                //resumes IIC operation.
                net_delay(1);                       //wait until stop condtion is in effect.
                       //The pending bit will not be set after issuing stop condition.
                break;
            }
                PDEBUG("poll--->write\n");
            rIICDS = net_iicData[net_iicPt++];
            for(i=0;i<10;i++);                  //for setup time until rising edge of IICSCL
            rIICCON = 0xaf;                     //resumes IIC operation.
            break;
                                                                                
        case SETRDADDR:
                PDEBUG("SETRDADDR\n");
            if((net_iicDataCount--)==0)
            {
                break;                  //IIC operation is stopped because of IICCON[4]
            }
            rIICDS = net_iicData[net_iicPt++];
            for(i=0;i<10;i++);          //for setup time until rising edge of IICSCL
            rIICCON = 0xaf;             //resumes IIC operation.
            break;
                                                                                
        default:
            break;
    }
}

void net_init_iic(void)
{
        net_address_map();
                                                                                
        rGPEUP  |= 0xc000;                  //Pull-up disable
        rGPECON |= 0xa00000;                //GPE15:IICSDA , GPE14:IICSCL
                                                                                
        //Enable ACK, Prescaler IICCLK=PCLK/16, Enable interrupt, Transmit clock value Tx clock=IICCLK/16
        rIICCON  = (1<<7) | (0<<6) | (1<<5) | (0xf);
                                                                                
        rIICSTAT = 0x10;                    //IIC bus data output enable(Rx/Tx)
}

void net_iic_read(U32 slvAddr,U32 addr,U8 *buffer,U32 length)
{
     //X1227 Random read && Sequential read
     int i = 0;
     PDEBUG("read---->0\n");
    net_iicMode      = SETRDADDR;
    net_iicPt        = 0;
    net_iicData[0]   = addr/256;
    net_iicData[1]   = addr;
    net_iicDataCount = 2;
                                                                                
    rIICDS   = slvAddr&0xfe;
    rIICSTAT = 0xf0;                    //MasTx,Start
                                                                                
    //Clearing the pending bit isn't needed because the pending bit has been cleared.
    while(net_iicDataCount!=-1)
        net_run_iicpoll();
    net_iicMode      = RDDATA;
    net_iicPt        = 1;
    net_iicDataCount = length;
                                                                                
    rIICDS   = slvAddr|0x01;
    rIICSTAT = 0xb0;                    //Master Rx,Start
    rIICCON  = 0xaf;                    //Resumes IIC operation.
//########################################hhtech wpq mod
    while(net_iicDataCount!=-1)
       net_run_iicpoll();
                                                                                
  //  printk("In the Read Function\n");
    /////////////////////////
                                                                             
    for(i = 0; i<length;i++)
    {
                buffer[i] = net_iicData[i+2];//hhtech wpq mod
//              printk("The read num %d is %x\n",i,_iicData[i+2]);
//###############hhtech wpq test
    }


}

enum DM9000_PHY_mode {
	DM9000_10MHD   = 0, 
	DM9000_100MHD  = 1, 
	DM9000_10MFD   = 4,
	DM9000_100MFD  = 5, 
	DM9000_AUTO    = 8, 
	DM9000_1M_HPNA = 0x10 
};

enum DM9000_NIC_TYPE {
	FASTETHER_NIC = 0, 
	HOMERUN_NIC   = 1, 
	LONGRUN_NIC   = 2 
};

// Active operation mode  - Spenser 10/6
enum DM9000_OP_mode {
	OP_10MHD	= 1,
	OP_10MFD	= 2,
	OP_100MHD	= 4,
	OP_100MFD	= 8
};

enum DM9000_SF_mode{
	VLAN_Enable	= 1,
	FlowControl	= 2,
	TxPausePacket	= 4
};

/* Structure/enum declaration ------------------------------- */
typedef struct board_info {
	
	//struct net_device *next_dev;   /* Link to next device */
	
	u32 runt_length_counter;	/* counter: RX length < 64byte */ 
	u32 long_length_counter;	/* counter: RX length > 1514byte */ 
	u32 reset_counter;		/* counter: RESET */ 
	u32 reset_tx_timeout;		/* RESET caused by TX Timeout */ 
	u32 reset_rx_status;		/* RESET caused by RX Statsus wrong */

	//u16 ioaddr;			/* Register I/O base address */
	//u16 io_data;			/* Data I/O address */
	u32 ioaddr;			/* Register I/O base address */
	u32 io_data;			/* Data I/O address */
	u16 irq;			/* IRQ */

	u16 tx_pkt_cnt;
	u16 sent_pkt_len, queue_pkt_len;
	u16 queue_start_addr;
	u16 dbug_cnt;
	u16 Preg0;
	u16 Preg4;

	u8 reg0, reg5, reg8, reg9, rega;/* registers saved */
	u8 op_mode;			/* PHY operation mode */
	u8 active_op_mode;	// Real PHY operation mode -Spenser 10/6
	u8 io_mode;			/* 0:word, 2:byte */
	u8 phy_addr;
	u8 link_failed;			/* Ever link failed */
	u8 nsr;				/*Network Status Register */
	u8 link_status;			/* Detect link state */
	//u8 link_states;
	u8 mdix;			/* MDIX */
	u8 device_wait_reset;		/* device state */
	u8 nic_type;			/* NIC type */
	u8 ncr;

	struct timer_list timer, mdix_timer;
	struct net_device_stats stats;
	unsigned char srom[128];
	spinlock_t lock;

} board_info_t;

/* Global variable declaration ----------------------------- */
static int dmfe_debug = 0;
//static struct net_device * dmfe_dev = NULL;
static struct net_device * dmfe_eth0_dev = NULL;
static struct net_device * dmfe_eth1_dev = NULL;

/* For module input parameter */
static int debug      = 0;
static int mode       = DM9000_AUTO;
static int media_mode = DM9000_AUTO;
static u8 reg5 	      = DM9000_REG05;
static u8 reg8 	      = DM9000_REG08;
static u8 reg9 	      = DM9000_REG09;
static u8 rega 	      = DM9000_REG0A;
static u8 nfloor      = 0;
//static u8 irqline     = 3;
static u32 tintrflag  = 0;
static u32 rxintrflag = 0;
static char * eth0_mac_string = " 00 11 22 33 44 55";
static char * eth1_mac_string = " 00 11 22 33 44 56";

/* function declaration ------------------------------------- */
static int dmfe_probe(struct net_device *);
static int dmfe_open(struct net_device *);
static int dmfe_start_xmit(struct sk_buff *, struct net_device *);
static void dmfe_tx_done(struct net_device *);
#if 0
#if defined(EMU32) || (HDX32)
static int dmfe_start_xmit32(struct sk_buff *, struct net_device *);
static void dmfe_tx32_done(unsigned long);
static void dmfe_packet32_receive(unsigned long);
#endif
#endif
static int dmfe_stop(struct net_device *);
static struct net_device_stats * dmfe_get_stats(struct net_device *); 
static int dmfe_do_ioctl(struct net_device *, struct ifreq *, int);
static void dmfe_interrupt(int , void *, struct pt_regs *);
static void dmfe_timer(unsigned long);
static void dmfe_init_dm9000(struct net_device *);
static void dmfe_reset_dm9000(struct net_device *);
static unsigned long cal_CRC(unsigned char *, unsigned int, u8);
static u8 ior(board_info_t *, int);
static void iow(board_info_t *, int, u8);
static u16 phy_read(board_info_t *, int);
static void phy_write(board_info_t *, int, u16);
static u16 read_srom_word(board_info_t *, int);
static void dmfe_packet_receive(unsigned long);
static void dm9000_hash_table(struct net_device *);
#if defined(AUTOMDIX)
static void dmfe_mdix_timer(unsigned long);
#endif

static void dmfe_eth0_packet_receive(unsigned long unused)
{
	dmfe_packet_receive(0);
}

static void dmfe_eth1_packet_receive(unsigned long unused)
{
	dmfe_packet_receive(1);
}

DECLARE_TASKLET( dmfe_rx_eth0_tasklet , dmfe_eth0_packet_receive , 0);
DECLARE_TASKLET( dmfe_rx_eth1_tasklet , dmfe_eth1_packet_receive , 0);

static unsigned char eth0_mac_addr[6] = { 0, 11, 22, 33 ,44 ,55 };
static unsigned char eth1_mac_addr[6] = { 0, 11, 22, 33 ,44 ,56 };

/* DM9000 network board routine ---------------------------- */

/*
  Search DM9000 board, allocate space and register it
*/
static int dmfe_probe(struct net_device *dev)
{
	struct board_info *db;    /* Point a board information structure */
	u32 id_val , iobase = 0 ;
	u16 i, dm9000_eth0_found = FALSE, dm9000_eth1_found=FALSE, probe_cnt;
	unsigned char buff[12];

	DMFE_DBUG(0, "dmfe_probe()",0);

	/* add by HuangBo begin */
	BWSCON|=0xdc0; 
	GPFCON |= 0xa; //EINT0/1 from PORT F control register 
	EXTINT0 |= 0x44; //EINT0/1 Rising edge triggered
	INTMSK &= 0xfffc; //EINT0/1 Enable
	/* add by HuangBo end */


	/* Search All DM9000 NIC */
	/* only search 2 dm9000 chip */
	for (probe_cnt = 0 ; probe_cnt < 2 ; probe_cnt++ )
	{
		if ( probe_cnt == 0 )
			iobase = (u32)ioremap(DM9000_MIN_IO_ETH0,0x400);
		else if ( probe_cnt == 1 )
			iobase = (u32)ioremap(DM9000_MIN_IO_ETH1,0x400);
		outb(DM9000_VID_L, iobase);
		id_val = inb(iobase + 4);
		outb(DM9000_VID_H, iobase);
		id_val |= inb(iobase + 4) << 8;
		outb(DM9000_PID_L, iobase);
		id_val |= inb(iobase + 4) << 16;
		outb(DM9000_PID_H, iobase);
		id_val |= inb(iobase + 4) << 24;
		
		if (id_val == DM9000_ID) {
			
			printk("<DM9000> I/O: %x, VID: %x \n",iobase, id_val);
			if ( probe_cnt == 0 )
				dm9000_eth0_found = TRUE;
			else if ( probe_cnt == 1 )
				dm9000_eth1_found = TRUE;

			/* Init network device */
			dev = init_etherdev(NULL, 0);  // oh ,I think the driver must be compiled with module
			
			/* Allocated board information structure */
			db = (void *)(kmalloc(sizeof(*db), GFP_KERNEL));
			memset(db, 0, sizeof(*db));
			dev->priv   = db;   /* link device and board info */
			//dmfe_dev  = dev;
			if ( probe_cnt == 0 ) 
			{
				dmfe_eth0_dev = dev;
			} else if ( probe_cnt == 1 )
			{
				dmfe_eth1_dev = dev;
			}
			db->ioaddr  = iobase;
			db->io_data = iobase + 4;

			/* driver system function */
			ether_setup(dev);
				
			dev->base_addr = iobase;
			if ( probe_cnt == 0 )
				dev->irq = 0;   //eth0 irq
			else if ( probe_cnt == 1 )
				dev->irq = 1; // eth1 irq
			dev->open = &dmfe_open;
			dev->hard_start_xmit = &dmfe_start_xmit;
#if defined(EMU32) || (HDX32)
			dev->hard_start_xmit = &dmfe_start_xmit32;
#endif
			dev->stop = &dmfe_stop;
			dev->get_stats = &dmfe_get_stats;
			dev->set_multicast_list = &dm9000_hash_table;
			dev->do_ioctl = &dmfe_do_ioctl;

			SET_MODULE_OWNER(dev);

#if 0
			/* Read SROM content */
			for (i=0; i<64; i++)
 				((u16 *)db->srom)[i] = read_srom_word(db, i);
#endif
			/* Set Node Address */
        net_init_iic();
        net_iic_read(0xaf,0x20,buff,6);
for(i=0;i<6;i++) 
{
          //    printk("receive buff[%d] = %x\n",i,buff[i]);
}
/*	/////////////////////////////////////////////////////
	net_init_iic();
        net_iic_read(0xaf,0x20,buff,6);
///////////////////////////////////////////////////////////
*/

			for (i=0; i<6; i++) 
			{
				if ( probe_cnt == 0 )
					//dev->dev_addr[i] = eth0_mac_addr[i];
					dev->dev_addr[i] = buff[i];
				if ( probe_cnt == 1 )
					dev->dev_addr[i] = eth1_mac_addr[i];
			}

			/* Request IO from system */
			request_region(iobase, 2, dev->name);

		}
	} 
#if 1
	if (dm9000_eth0_found) 
		printk("dm9000 eth0 found\n");
	else
		printk("dm9000 eth0 not found\n");
//	if (dm9000_eth1_found)
//		printk("dm9000 eth1 found\n");
//	else
//		printk("dm9000 eth1 not found\n");
#endif 
	//return (dm9000_eth0_found && dm9000_eth1_found) ? 0:-ENODEV;
	return (dm9000_eth0_found) ? 0:-ENODEV;
}

/*
  Open the interface.
  The interface is opened whenever "ifconfig" actives it.
*/
static int dmfe_open(struct net_device *dev)
{
	board_info_t * db = (board_info_t *)dev->priv;
	DMFE_DBUG(0, "dmfe_open", 0);
	MOD_INC_USE_COUNT;

	if (request_irq(dev->irq,&dmfe_interrupt,SA_SHIRQ,dev->name,dev)) 
	{
		printk("requet_irq error \n");
		return -EAGAIN;
	}

	/* Initilize DM910X board */
	dmfe_init_dm9000(dev);
 
	/* Init driver variable */
	db->dbug_cnt 		= 0;
	db->runt_length_counter = 0;
	db->long_length_counter = 0;
	db->reset_counter 	= 0;

	/* set and active a timer process */
	init_timer(&db->timer);
	db->timer.expires 	= DMFE_TIMER_WUT * 2;
	db->timer.data 		= (unsigned long)dev;
	db->timer.function 	= &dmfe_timer;
	add_timer(&db->timer);	//Move to DM9000 initiallization was finished.

#if defined(AUTOMDIX)
	/* set and active a timer process for Auto-MDIX */
	init_timer(&db->mdix_timer);
	db->mdix_timer.expires	= DMFE_TIMER_MDIX;
	db->mdix_timer.data	= (unsigned long)dev;
	db->mdix_timer.function = &dmfe_mdix_timer;
	add_timer(&db->mdix_timer);	
#endif
	
	netif_start_queue(dev);

	return 0;
}

/* Set PHY operationg mode
*/
static void set_PHY_mode(board_info_t *db)
{
	//u16 phy_reg4 = 0x05e1;
	//u16 phy_reg0 = 0x3100;
	u16 phy_reg0 = 0x1000;		/* Auto-negotiation & non-duplux mode*/
	u16 phy_reg4 = 0x01e1;		/* Default non flow control */
//	u16 mreg0;

	if ( !(db->op_mode & DM9000_AUTO) ) // op_mode didn't auto sense */
	{ 
		switch(db->op_mode) {
			case DM9000_10MHD:  phy_reg4 = 0x21; 
					    //phy_reg4 = 0x0421;
                        	            phy_reg0 = 0x0000;
					    //phy_reg0 = 0x1200;
					    break;
			case DM9000_10MFD:  phy_reg4 = 0x41; 
					    //phy_reg4 = 0x0441;
					    phy_reg0 = 0x1100;
                        	            //phy_reg0 = 0x1300; 
                                	    break;
			case DM9000_100MHD: phy_reg4 = 0x81; 
					    //phy_reg4 = 0x0481;
					    phy_reg0 = 0x2000; 
					    //phy_reg0 = 0x3200;
				    	    break;
			case DM9000_100MFD: phy_reg4 = 0x101; 
					    //phy_reg4 = 0x0501;
					    phy_reg0 = 0x3100;
				    	    //phy_reg0 =0x3300; 
				   	    break;
		} // end of switch
		//db->Preg0 = phy_reg0;
		//db->Preg4 = phy_reg4;
	
	} // end of if
	phy_write(db, 0, phy_reg0);
	phy_write(db, 4, 0x0400|phy_reg4);
	db->Preg0 = phy_reg0;
	db->Preg4 = phy_reg4 + 0x0400;

#if 0
		switch(db->sf_mode)
		{
		    case VLAN_Enable:
				        break;
		    case FlowControl:
		    case TxPausePacket:
					phy_write(db, 0, phy_reg0);
					phy_write(db, 4, 0x0400|phy_reg4);
					db->Preg0 = phy_reg0;
					db->Preg4 = phy_reg4 + 0x0400;
					break;
		    default:
					phy_write(db, 0, phy_reg0);
					phy_write(db, 4, phy_reg4);
					db->Preg0 = phy_reg0;
					db->Preg4 = phy_reg4;
					break;
		}
#endif
	//}
	//else{ // op_mode is auto sense */
	//	phy_write(db, 0, phy_reg0);
	//	phy_write(db, 4, phy_reg4);
	//}
	//db->Preg0 = phy_reg0;
	//db->Preg4 = phy_reg4;

		//phy_write(db, 0, phy_reg0);	/* Tmp */
		//phy_write(db, 4, phy_reg4);	/* Set PHY media mode */

	//iow(db, 0x08, 0x37);		/* Less 3Kb, 200us */
	//udelay(250);
	//iow(db, 0x09, 0x38);		/* Less 3Kb, 200us */
	//iow(db, 0x0a, 0x21);		/* Less 3Kb, 200us */
//Marked by Spenser 10/02/2003
//	iow(db, 0x1e, 0x03);			/* Let GPIO0/GPIO1 output */
//	iow(db, 0x1f, 0x01);			/* Enable PHY */
	
}

/*
	Init HomeRun DM9801
*/
static void program_dm9801(board_info_t *db, u16 HPNA_rev)
{
	__u16 reg16, reg17, reg24, reg25;

	if ( !nfloor ) 
		nfloor = DM9801_NOISE_FLOOR;

	reg16 = phy_read(db, 16);
	reg17 = phy_read(db, 17);
	reg24 = phy_read(db, 24);
	reg25 = phy_read(db, 25);

	switch(HPNA_rev) {
		case 0xb900: /* DM9801 E3 */
			reg16 |= 0x1000;
			reg25 = ( (reg24 + nfloor) & 0x00ff) | 0xf000;
			break;
		case 0xb901: /* DM9801 E4 */
			reg25 = ( (reg24 + nfloor) & 0x00ff) | 0xc200;
			reg17 = (reg17 & 0xfff0) + nfloor + 3;
			break;
		case 0xb902: /* DM9801 E5 */
		case 0xb903: /* DM9801 E6 */
		default:
			reg16 |= 0x1000;
			reg25 = ( (reg24 + nfloor - 3) & 0x00ff) | 0xc200;
			reg17 = (reg17 & 0xfff0) + nfloor;
	}

	phy_write(db, 16, reg16);
	phy_write(db, 17, reg17);
	phy_write(db, 25, reg25);
}

/*
	Init LongRun DM9802
*/
static void program_dm9802(board_info_t *db)
{
	__u16 reg25;

	if ( !nfloor )
		nfloor = DM9802_NOISE_FLOOR;

	reg25 = phy_read(db, 25);
	reg25 = (reg25 & 0xff00) + nfloor;
	phy_write(db, 25, reg25);
}

/* 
	Identify NIC type
*/
static void identify_nic(board_info_t *db)
{
	u16 phy_reg3;
	iow(db, 0, DM9000_EXT_MII);
	phy_reg3 = phy_read(db, 3);

	switch(phy_reg3 & 0xfff0) {
		
		case 0xb900:
			
			if (phy_read(db, 31) == 0x4404) {
				db->nic_type =  HOMERUN_NIC;
				program_dm9801(db, phy_reg3);
			} else {
				db->nic_type = LONGRUN_NIC;
				program_dm9802(db);
			}
			break;

		default: db->nic_type = FASTETHER_NIC; 
			 break;

	}
	
	iow(db, 0, DM9000_INT_MII);	
}

/* 
	Initilize dm9000 board
*/
static void dmfe_init_dm9000(struct net_device *dev)
{
	board_info_t *db = (board_info_t *)dev->priv;
	DMFE_DBUG(0, "dmfe_init_dm9000()", 0);

	/* set the internal PHY power-on, GPIOs normal, and wait 2ms */
	iow(db, 0x1F, 0);	/* GPR (reg_1Fh)bit GPIO0=0 pre-activate PHY */
	udelay(20);		/* wait 2ms for PHY power-on ready */

	/* do a software reset and wait 20us */
//	iow(db, 0, 1);		/* NCR (reg_00h) bit[0] RST=1, reset on */
//	iow(db, 0, 3);		/* NCR (reg_00h) bit[0] RST=1 & Loopback=1, reset on. Added by SPenser */
	iow(db, DM9000_NCR, 3);
	udelay(20);		/* wait 20us at least for software reset ok */
	iow(db, 0, 3);		/* NCR (reg_00h) bit[0] RST=1 & Loopback=1, reset on. Added by SPenser */
	udelay(20);		/* wait 20us at least for software reset ok */

// Marked by Spenser
	/* set GPIO0=1 then GPIO0=0 to turn off and on the internal PHY */
	iow(db, 0x1F, 1);	/* GPR (reg_1Fh) bit[0] GPIO0=1 turn-off PHY */
	iow(db, 0x1F, 0);	/* GPR (reg_1Fh) bit[0] GPIO0=0 activate PHY */
	udelay(4000);		/* wait 4ms linking PHY (AUTO sense) if RX/TX */

	/* I/O mode */
	db->io_mode = ior(db, 0xfe) >> 6; /* ISR bit7:6 keeps I/O mode */

	/* NIC Type: FASTETHER, HOMERUN, LONGRUN */
	identify_nic(db);	

	/* Set PHY */
	db->op_mode = media_mode;
	set_PHY_mode(db);

	/* Init needed register value */
	db->reg0 = DM9000_NCR;
	if ( (db->nic_type != FASTETHER_NIC) && (db->op_mode & DM9000_1M_HPNA) )
		db->reg0 |= DM9000_EXT_MII;
	
	/* User passed argument */
	db->reg5 = reg5;
	db->reg8 = reg8;
	db->reg9 = reg9;
	db->rega = rega;

	/* Program operating register */
	//iow(db, 0x00, db->reg0);
	iow(db, 0x00, 0x08);
	iow(db, 0x02, 0);		/* TX Polling clear */
	iow(db, 0x2f, 0);		/* Special Mode */
	iow(db, 0x01, 0x2c);	/* clear TX status */
	iow(db, 0xfe, 0x0f); 	/* Clear interrupt status */
	iow(db, 0x08, 0x37);
	iow(db, 0x09, 0x38);	/* Flow control: High/Low water */
	iow(db, 0x0a, 0x29);	/* flow control */

	/* Set address filter table */
	dm9000_hash_table(dev);

	/* Activate DM9000 */
	iow(db, 0x05, db->reg5 | 1);	/* RX enable */
	iow(db, 0xff, DM9000_REGFF); 	/* Enable TX/RX interrupt mask */
 
	/* Init Driver variable */
	db->link_failed 	= 1;
	db->tx_pkt_cnt 		= 0;
	db->queue_pkt_len 	= 0;
	dev->trans_start 	= 0;
	
	netif_carrier_on(dev);
	spin_lock_init(&db->lock);

}

/* 
	Reset dm9000 board due to 32bits mode while excess collision or late collision.
*/

static void dmfe_reset_dm9000(struct net_device *dev)
{
	board_info_t *db = (board_info_t *)dev->priv;
	DMFE_DBUG(0, "dmfe_reset_dm9000()", 0);

	iow(db, 0, 1);		/* NCR (reg_00h) bit[0] RST=1, reset on. Added by SPenser */
	udelay(1);		/* wait 20us at least for software reset ok */
	iow(db, 0, 0);
	
	/* software reset 2 times */
	iow(db, DM9000_NCR, 3);
	udelay(20);
	iow(db, DM9000_NCR, 3);
	udelay(20);

	/* Program operating register */
	iow(db, 0x00, db->reg0);
	iow(db, 0x02, 0);		/* TX Polling clear */
	iow(db, 0x08, 0x3f);	/* Less 3Kb, 200us */
	iow(db, 0x09, 0x38);	/* Flow Control : High/Low Water */
	iow(db, 0x0a, 0x29);	/* TX Pause packet */
	iow(db, 0x2f, 0);		/* Special Mode */
	iow(db, 0x01, 0x2c);		/* clear TX status */
	iow(db, 0xfe, 0x0f); 		/* Clear interrupt status */

	/* Activate DM9000 */
	iow(db, 0x05, db->reg5 | 1);	/* RX enable */
	iow(db, 0xff, DM9000_REGFF); 	/* Enable TX/RX interrupt mask */
 
	/* Init Driver variable */
	db->link_failed 	= 1;
	db->tx_pkt_cnt 		= 0;
	db->queue_pkt_len 	= 0;
	dev->trans_start 	= 0;

	netif_carrier_on(dev);

} 

/*
  Hardware start transmission.
  Send a packet to media from the upper layer.
*/
static int dmfe_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	board_info_t *db = (board_info_t *)dev->priv;
	char * data_ptr;
	int i, tmplen;

	DMFE_DBUG(0, "dmfe_start_xmit", 0);

	if (db->tx_pkt_cnt > 1)
		return 1; 
	
	netif_stop_queue(dev); 

	/* Disable all interrupt */
	iow(db, 0xff, 0x80);
	
	/* Move data to DM9000 TX RAM */
	data_ptr = (char *)skb->data;
	outb(0xf8, db->ioaddr);
	
	db->sent_pkt_len = skb->len;
		
	if (db->io_mode == DM9000_BYTE_MODE) {
		/* Byte mode */
		for (i = 0; i < skb->len; i++)
			outb((data_ptr[i] & 0xff), db->io_data);
	} else if (db->io_mode == DM9000_WORD_MODE) {
		/* Word mode */
		tmplen = (skb->len + 1) / 2;
		for (i = 0; i < tmplen; i++)
         		outw(((u16 *)data_ptr)[i], db->io_data);
	} else {
		/* DWord mode */
		tmplen = (skb->len + 3) / 4;			
		for (i = 0; i< tmplen; i++)
			outl(((u32 *)data_ptr)[i], db->io_data);
	} 

	/* TX control: First packet immediately send, second packet queue */
	if (db->tx_pkt_cnt == 0) 
	{
		/* First Packet */
		db->tx_pkt_cnt++;

		/* Set TX length to DM9000 */
		iow(db, 0xfc, skb->len & 0xff);
		iow(db, 0xfd, (skb->len >> 8) & 0xff);

//		/* First Packet */
//		db->tx_pkt_cnt++;

		/* Issue TX polling command */
		iow(db, 0x2, 0x1);		/* Cleared after TX complete */

		/* saved the time stamp */
		dev->trans_start = jiffies;
	} 
	else {
		/* Second packet */
		db->tx_pkt_cnt++;
		db->queue_pkt_len = skb->len;
	}
 
	/* free this SKB */
	dev_kfree_skb(skb);

	/* Re-enable resource check */
	if (db->tx_pkt_cnt == 1)
              netif_wake_queue(dev); 
		
	/* Re-enable interrupt*/ 
	iow(db,0xff,0x83);
	
	return 0;
}

/*
  Stop the interface.
  The interface is stopped when it is brought.
*/
static int dmfe_stop(struct net_device *dev)
{
	board_info_t *db = (board_info_t *)dev->priv;
	DMFE_DBUG(0, "dmfe_stop", 0);

	/* deleted timer */
	del_timer(&db->timer);
	del_timer(&db->mdix_timer);

	netif_stop_queue(dev); 

	/* free interrupt */
	free_irq(dev->irq, dev);

	/* RESET devie */
	phy_write(db, 0x00, 0x8000);	/* PHY RESET */
	iow(db, 0x1f, 0x01); 		/* Power-Down PHY */
	iow(db, 0xff, 0x80);		/* Disable all interrupt */
	iow(db, 0x05, 0x00);		/* Disable RX */

	MOD_DEC_USE_COUNT;

	/* Dump Statistic counter */
#if FALSE
	printk("\nRX FIFO OVERFLOW %lx\n", db->stats.rx_fifo_errors);
	printk("RX CRC %lx\n", db->stats.rx_crc_errors);
	printk("RX LEN Err %lx\n", db->stats.rx_length_errors);
	printk("RX LEN < 64byte %x\n", db->runt_length_counter);
	printk("RX LEN > 1514byte %x\n", db->long_length_counter);
	printk("RESET %x\n", db->reset_counter);
	printk("RESET: TX Timeout %x\n", db->reset_tx_timeout);
	printk("RESET: RX Status Wrong %x\n", db->reset_rx_status);
#endif

	return 0;
}

static void dmfe_tx_done(struct net_device *dev_id)
{
	struct net_device *dev = dev_id;
	board_info_t *db = (board_info_t *)dev->priv;
	//int TxStatus;	//For Collision detect
	int tx_status = ior(db, 0x01);	/* Got TX status */
	DMFE_DBUG(0, "dmfe_tx_done()", 0);
	if (tx_status & 0xc) 
	{
		/* One packet sent complete */
		db->tx_pkt_cnt--;
		dev->trans_start = 0;
		db->stats.tx_packets++;

		/* Queue packet check & send */
		if (db->tx_pkt_cnt > 0) 
		{
			/* Set TX length to DM9000 */
			iow(db, 0xfc, db->queue_pkt_len & 0xff);
			iow(db, 0xfd, (db->queue_pkt_len >> 8) & 0xff);
		
			/* Issue TX polling command */
			iow(db, 0x2, 0x1);    /* Cleared after TX complete */
			dev->trans_start = jiffies; /* saved the time stamp */
			//netif_wake_queue(dev);
		}
		netif_wake_queue(dev);
	}

}

/*
  DM9102 insterrupt handler
  receive the packet to upper layer, free the transmitted packet
*/
static void dmfe_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	struct net_device *dev = dev_id;
	board_info_t *db;
	int int_status, tx_status;
	u8 reg_save;

	DMFE_DBUG(0, "dmfe_interrupt()", 0);

	if (!dev) {
		DMFE_DBUG(1, "dmfe_interrupt() without DEVICE arg", 0);
		return;
	}

	tintrflag++;

	/* A real interrupt coming */
	db = (board_info_t *)dev->priv;
	spin_lock(&db->lock);

	/* Save previous register address */
	reg_save = inb(db->ioaddr);

	/* Disable all interrupt */
	iow(db, 0xff, 0x80); 
	//iow(db, 0xff, 0x81);

	/* Got DM9000 interrupt status */
	int_status = ior(db, 0xfe);		/* Got ISR */
	iow(db, 0xfe, int_status);		/* Clear ISR status */ 

	/* Trnasmit Interrupt check */
//*Spenser
//	if (int_status & DM9000_TX_INTR)
//		tasklet_schedule(&dmfe_tx_tasklet);
	if (int_status & DM9000_TX_INTR)
	{
		tx_status = ior(db, 0x01);	/* Got TX status */
		if (tx_status & 0xc) 
		{
			/* One packet sent complete */
			db->tx_pkt_cnt--;
			db->stats.tx_packets++;

			dev->trans_start = 0;

			/* Queue packet check & send */
			if (db->tx_pkt_cnt > 0) 
			{
				/* Set TX length to DM9000 */
				iow(db, 0xfc, db->queue_pkt_len & 0xff);
				iow(db, 0xfd, (db->queue_pkt_len >> 8) & 0xff);
		
				/* Issue TX polling command */
				iow(db, 0x2, 0x1);    /* Cleared after TX complete */
				dev->trans_start = jiffies; /* saved the time stamp */
			}
			netif_wake_queue(dev);
		}
		
	}

	/* Received the coming packet */
	if (int_status & DM9000_RX_INTR)
	{ 
		rxintrflag++;
		if ( dev == dmfe_eth0_dev ) 
		{
			//printk("ready to &dmfe_rx_eth0_tasklet\n");
			tasklet_schedule(&dmfe_rx_eth0_tasklet);
		} else	if ( dev == dmfe_eth1_dev ) {
			//printk("ready to &dmfe_rx_eth1_tasklet\n");
			tasklet_schedule(&dmfe_rx_eth1_tasklet);
		}
	}
	else {
		/* Re-enable interrupt mask */ 
		iow(db, 0xff, 0x83);
	}

	//printk("<RX interrupt number: %d>\n", rxintrflag);
	//printk("<<Total interrupt number: %d>>\n", tintrflag);
/* Re-enable interrupt mask */ 
//		iow(db, 0xff, 0x83);	
	/* Restore previous register address */
	outb(reg_save, db->ioaddr); 

	spin_unlock(&db->lock); 
}

/*
  Get statistics from driver.
*/
static struct net_device_stats * dmfe_get_stats(struct net_device *dev)
{
	board_info_t *db = (board_info_t *)dev->priv;
	DMFE_DBUG(0, "dmfe_get_stats", 0);
	return &db->stats;
}

/*
  Process the upper socket ioctl command
*/
static int dmfe_do_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd)
{
	DMFE_DBUG(0, "dmfe_do_ioctl()", 0);
	return 0;
}

/*
  A periodic timer routine
  Dynamic media sense, allocated Rx buffer...
*/
static void dmfe_timer(unsigned long data)
{
	struct net_device *dev = (struct net_device *)data;
	board_info_t *db = (board_info_t *)dev->priv;
	u8 reg_save, tmp_reg;
	//u8 tmpreg;
	
	DMFE_DBUG(0, "dmfe_timer()", 0);

	/* Save previous register address */
	reg_save = inb(db->ioaddr);

	/* TX timeout check */
	if (dev->trans_start&&((jiffies-dev->trans_start)>DMFE_TX_TIMEOUT)) {
		//printk("TX timeout reset.\n");
		db->device_wait_reset = 1;
		db->reset_tx_timeout++;
	} 

	/* DM9000 dynamic RESET check and do */
	if (db->device_wait_reset) {
		netif_stop_queue(dev); 
		db->reset_counter++;
		db->device_wait_reset = 0;
		dev->trans_start = 0;
		dmfe_init_dm9000(dev);
		netif_wake_queue(dev);
	}

	/* Auto Sense Media mode policy:
		FastEthernet NIC: don't need to do anything.
		Media Force mode: don't need to do anything.
		HomeRun/LongRun NIC and AUTO_Mode:
			INT_MII not link, select EXT_MII
			EXT_MII not link, select INT_MII
	*/
	if ((db->nic_type != FASTETHER_NIC) & (db->op_mode == DM9000_AUTO))
	{
		tmp_reg = ior(db, 0x01);	/* Got link status */
		if ( !(tmp_reg & 0x40) ) { 	/* not link */
			db->reg0 ^= 0x80;
			iow(db, 0x00, db->reg0);
			//printk(KERN_ERR "not link\n");
		}
	}

	/* Restore previous register address */
	outb(reg_save, db->ioaddr);

	/* Set timer again */
	db->timer.expires = DMFE_TIMER_WUT;
	add_timer(&db->timer);

}

#if defined(AUTOMDIX)
static void dmfe_mdix_timer(unsigned long data)
{
	struct net_device *dev = (struct net_device *)data;
	board_info_t *db = (board_info_t *)dev->priv;

	/* support AUTO-MDIX */
	db->mdix = ior(db, DM9000_GPR); 		
	db->link_status = ior(db, DM9000_NSR); 
	db->link_status = test_bit(6, &db->link_status);
	udelay(2000);

	if (!db->link_status)
	{
		switch(db->mdix)
		{
		case 0x7c: //use parallel line change to cross line
			if(netif_carrier_ok(dev))
				netif_carrier_off(dev);
			iow(db, DM9000_GPCR, 0x03);
			iow(db, DM9000_GPR, 0x02);
			break;
		case 0x7e: // use cross line change to parallel line
			if(netif_carrier_ok(dev))
				netif_carrier_off(dev);
			iow(db, DM9000_GPCR, 0x03);
			iow(db, DM9000_GPR, 0x00);
			break;
		default:
			break;
		}
	}
	else{
		
		 if (!netif_carrier_ok(dev)) 
			netif_carrier_on(dev);
	}
	
	/* Set timer again */
	db->mdix_timer.expires = DMFE_TIMER_MDIX;
	add_timer(&db->mdix_timer);

}
#endif

/*
  Received a packet and pass to upper layer
*/
static void dmfe_packet_receive(unsigned long dev_num)
{
	struct net_device *dev = NULL;
	board_info_t *db = NULL ;
	struct sk_buff *skb;
	u8 rxbyte, *rdptr;
	u16 i, RxStatus, RxLen, GoodPacket, tmplen;
//#ifndef EMU32
	u32 tmpdata;
//#endif
 
	DMFE_DBUG(0, "dmfe_packet_receive()", 0);
	
	if ( dev_num == 0 )
		dev = dmfe_eth0_dev ;
	else if ( dev_num == 1 )
		dev = dmfe_eth1_dev ;
	     else 
		return ;			
	
	db = (board_info_t *)dev->priv;
	
	/* Check packet ready or not */
	do {
		ior(db, 0xf0);			/* Dummy read */
		rxbyte = inb(db->io_data);	/* Got most updated data */

		/* packet ready to receive check */
		if (rxbyte == DM9000_PKT_RDY) 
		{
		    /* A packet ready now  & Get status/length */
		    GoodPacket = TRUE;
		    outb(0xf2, db->ioaddr);

		    /* Selecting io mode */		
			RxStatus = (u16)0;
			RxLen    = (u16)0;
			/* modify Select io mode by jackal 10/28/2003 */
			switch (db->io_mode) 
			{
			  case DM9000_BYTE_MODE: 
 				    RxStatus = inb(db->io_data) + 
				               (inb(db->io_data) << 8);
				    RxLen = inb(db->io_data) + 
					    (inb(db->io_data) << 8);
				    break;
			  case DM9000_WORD_MODE:
				    RxStatus = inw(db->io_data);
				    RxLen    = inw(db->io_data);
				    break;
			  case DM9000_DWORD_MODE:
//#if defined(EMU32)
//				    // Emulate 32bits mode on ISA of pc
//				    RxStatus = inw(db->io_data);
//				    RxLen    = inw(0x320);
//#else
				    tmpdata  = inl(db->io_data);
				    RxStatus = tmpdata;
				    RxLen    = tmpdata >> 16;
//#endif
				    break;
			  default:
				    break;
			}

			/* Packet Status check */
			if (RxLen < 0x40) 
			{ 
			    GoodPacket = FALSE; 
			    db->runt_length_counter++; 
			} else if (RxLen > DM9000_PKT_MAX) 
			       { 
			 	    printk("<DM9000> RST: RX Len:%x\n", RxLen);
				    db->device_wait_reset = TRUE; 
				    db->long_length_counter++; 
			       }

			if (RxStatus & 0xbf00) 
			{
				GoodPacket = FALSE;
				if (RxStatus & 0x100) 
					db->stats.rx_fifo_errors++;
				if (RxStatus & 0x200) 
					db->stats.rx_crc_errors++;
				if (RxStatus & 0x8000) 
					db->stats.rx_length_errors++;
			}

			/* Move data from DM9000 */
			if (!db->device_wait_reset) {
				if ( GoodPacket && ((skb = dev_alloc_skb(RxLen + 4)) != NULL ) ) {
					skb->dev = dev;
					skb_reserve(skb, 2);
					rdptr = (u8 *) skb_put(skb, RxLen-4);
					
					/* Read received packet from RX SARM */
					if (db->io_mode == DM9000_BYTE_MODE) {
						/* Byte mode */
						for (i=0; i<RxLen; i++)
							rdptr[i]=inb(db->io_data);
					} else if (db->io_mode == DM9000_WORD_MODE) {
						/* Word mode */
						tmplen = (RxLen + 1) / 2;
						for (i = 0; i < tmplen; i++)
							((u16 *)rdptr)[i] = inw(db->io_data);
					} else {
						/* DWord mode */
//#if defined(EMU32)
//						//Emulate 32bits mode on ISA of PC
//						tmplen = (RxLen + 3) / 4;
//						for (i = 0; i < tmplen; i++)
//						{
//							((u16 *)rdptr)[i*2] = inw(db->io_data);
//							((u16 *)rdptr)[i*2+1] = inw(0x320);							
//						}
//#else
						tmplen = (RxLen + 3) / 4;
						for (i = 0; i < tmplen; i++)
							((u32 *)rdptr)[i] = inl(db->io_data);
//#endif						
					} 
					
					/* Pass to upper layer */
					skb->protocol = eth_type_trans(skb,dev);
					netif_rx(skb);
					db->stats.rx_packets++; 
					
				} else {
					/* Without buffer or error packet */
					if (db->io_mode == DM9000_BYTE_MODE) {
						/* Byte mode */
						for (i = 0; i < RxLen; i++)
							inb(db->io_data);
					} else if (db->io_mode == DM9000_WORD_MODE) {
						/* Word mode */
						tmplen = (RxLen + 1) / 2;
						for (i = 0; i < tmplen; i++)
							inw(db->io_data);
					} else {
						/* DWord mode */
//#if defined(EMU32)
//						//Emulate 32 bits mode on ISA of PC	
//						tmplen = (RxLen + 3) / 4;
//						for (i = 0; i < tmplen; i++)
//						{
//							inw(db->io_data);
//							inw(0x320);							
//						}
//#else
						tmplen = (RxLen + 3) / 4;
						for (i = 0; i < tmplen; i++)
							inl(db->io_data);
//#endif												
					} 
				}
			}
		}
		/* Status check: this byte must be 0 or 1 */
		else if (rxbyte > DM9000_PKT_RDY) {
//			printk("RX SRAM 1st byte != 01, must reset.\n");
			iow(db, 0x05, 0x00);	/* Stop Device */
			iow(db, 0xfe, 0x80);	/* Stop INT request */
			db->device_wait_reset = TRUE;
			db->reset_rx_status++;
		}
	}while(rxbyte == DM9000_PKT_RDY && !db->device_wait_reset);
//*Spenser

	if (!db->device_wait_reset)
	{
		dmfe_tx_done(dev);
		iow(db, 0xff, 0x83);
	}

}

/*
  Read a word data from SROM
*/
static u16 read_srom_word(board_info_t *db, int offset)
{
	iow(db, 0xc, offset);
	iow(db, 0xb, 0x4);
	udelay(200);
	iow(db, 0xb, 0x0);
	return (ior(db, 0xd) + (ior(db, 0xe) << 8) );
}

/*
  Set DM9000 multicast address
*/
static void dm9000_hash_table(struct net_device *dev)
{
	board_info_t *db = (board_info_t *)dev->priv;
	struct dev_mc_list *mcptr = dev->mc_list;
	int mc_cnt = dev->mc_count;
	u32 hash_val;
	u16 i, oft, hash_table[4];

	DMFE_DBUG(0, "dm9000_hash_table()", 0);

	/* Set Node address */
	for (i = 0, oft = 0x10; i < 6; i++, oft++)
		iow(db, oft, dev->dev_addr[i]);

	/* Clear Hash Table */
	for (i = 0; i < 4; i++)
		hash_table[i] = 0x0;

	/* broadcast address */
	hash_table[3] = 0x8000;

	/* the multicast address in Hash Table : 64 bits */
	for (i = 0; i < mc_cnt; i++, mcptr = mcptr->next) {
		hash_val = cal_CRC((char *)mcptr->dmi_addr, 6, 0) & 0x3f; 
		hash_table[hash_val / 16] |= (u16) 1 << (hash_val % 16);
	}

	/* Write the hash table to MAC MD table */
	for (i = 0, oft = 0x16; i < 4; i++) {
		iow(db, oft++, hash_table[i] & 0xff);
		iow(db, oft++, (hash_table[i] >> 8) & 0xff);
	}
}

/* The little-endian AUTODIN II ethernet CRC calculation.
   N.B. Do not use for bulk data, use a table-based routine instead.
   This is common code and should be moved to net/core/crc.c */
static unsigned const ethernet_polynomial_le = 0xedb88320U;
static inline unsigned ether_crc_le(int length, unsigned char *data)
{
	unsigned int crc = 0xffffffff;	/* Initial value. */
	while(--length >= 0) {
		unsigned char current_octet = *data++;
		int bit;
		for (bit = 8; --bit >= 0; current_octet >>= 1) {
			if ((crc ^ current_octet) & 1) {
				crc >>= 1;
				crc ^= ethernet_polynomial_le;
			} else
				crc >>= 1;
		}
	}
	return crc;
}

/*
  Calculate the CRC valude of the Rx packet
  flag = 1 : return the reverse CRC (for the received packet CRC)
         0 : return the normal CRC (for Hash Table index)
*/
static unsigned long cal_CRC(unsigned char * Data, unsigned int Len, u8 flag)
{
	
	u32 crc = ether_crc_le(Len, Data);

	if (flag) 
		return ~crc;
		
	return crc;
	 
}

/*
   Read a byte from I/O port
*/
static u8 ior(board_info_t *db, int reg)
{
	outb(reg, db->ioaddr);
	return inb(db->io_data);
}

/*
   Write a byte to I/O port
*/
static void iow(board_info_t *db, int reg, u8 value)
{
	outb(reg, db->ioaddr);
	outb(value, db->io_data);
}

/*
   Read a word from phyxcer
*/
static u16 phy_read(board_info_t *db, int reg)
{
	/* Fill the phyxcer register into REG_0C */
	iow(db, 0xc, DM9000_PHY | reg);

	iow(db, 0xb, 0xc); 	/* Issue phyxcer read command */
	udelay(100);		/* Wait read complete */
	iow(db, 0xb, 0x0); 	/* Clear phyxcer read command */

	/* The read data keeps on REG_0D & REG_0E */
	return ( ior(db, 0xe) << 8 ) | ior(db, 0xd);
}

/*
   Write a word to phyxcer
*/
static void phy_write(board_info_t *db, int reg, u16 value)
{
	/* Fill the phyxcer register into REG_0C */
	iow(db, 0xc, DM9000_PHY | reg);

	/* Fill the written data into REG_0D & REG_0E */
	iow(db, 0xd, (value & 0xff));
	iow(db, 0xe, ( (value >> 8) & 0xff));

	iow(db, 0xb, 0xa);		/* Issue phyxcer write command */
	udelay(500);			/* Wait write complete */
	iow(db, 0xb, 0x0);		/* Clear phyxcer write command */
}

#ifdef MODULE
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Sten Wang, sten_wang@davicom.com.tw");
MODULE_DESCRIPTION("Davicom DM9000 ISA/uP Fast Ethernet Driver");
MODULE_PARM(debug,"i");
MODULE_PARM(mode,"i");
MODULE_PARM(reg5,"i");
MODULE_PARM(reg9,"i");
MODULE_PARM(rega,"i");
MODULE_PARM(nfloor,"i");
MODULE_PARM(eth0_mac_string,"s");
//MODULE_PARM(eth1_mac_string,"s");
//MODULE_PARM(SF_mode, "i");


/*
 *  convert an ascii hex char digit to a number.
 *  param is hex digit.
 *  returns a decimal digit.
 */
static int hex_char2digit (char digit)
{
	if (digit == 0)
		return 0;

	if (digit >= '0' && digit <= '9')
		return digit - '0';
	if (digit >= 'a' && digit <= 'f')
		return digit - 'a' + 10;
	if (digit >= 'A' && digit <= 'F')
		return digit - 'A' + 10;

	/* shouldn't ever get this far */
	return -1;
}

static int hex_string2int (char *hex_ptr)
{
	int hexValue;
	int ret = 0;

	while (*hex_ptr)
	{
		hexValue = hex_char2digit(*hex_ptr);
		if (hexValue < 0)
			return -1;
		ret = (ret << 4) | hexValue;
		hex_ptr++;
	}
	return ret;
}

static char *strdup(char *str)
{ 
	int n = strlen(str)+1; 
	char *s = kmalloc(n, GFP_KERNEL); 
	if (!s) return NULL; 
	return strcpy(s, str);
}


/* Description: 
   when user used insmod to add module, system invoked init_module()
   to initilize and register.
*/
int init_module(void)
{
	int cnt;
	char *ptr;
	
	DMFE_DBUG(0, "init_module() ", debug);

	if (debug) 
		dmfe_debug = debug;   /* set debug flag */

	switch(mode) {
		case DM9000_10MHD:
		case DM9000_100MHD:
		case DM9000_10MFD:
		case DM9000_100MFD:
		case DM9000_1M_HPNA:
			media_mode = mode;
			break;
		default:
			media_mode = DM9000_AUTO;
	}
#if 0
	/* added by jackal for SF_mode */
	switch(SF_mode)
	{
		case VLAN_Enable:
		case FlowControl:
		case TxPausePacket:
			sf_mode = SF_mode;
			break;
		default:
			sf_mode = 0;
			break;
	}
#endif 
	nfloor = (nfloor > 15) ? 0:nfloor;

	ptr = strdup(eth0_mac_string);
	cnt = 0 ;
	while (cnt < 6 )
	{
		char *tail;
		
		if ( *ptr == '\0' )
			break;
		if ( *ptr != ' ' )
		{
			tail = ptr;
			while ( *tail != ' ' && *tail != '\0' )
				tail++;
			*tail = '\0' ;
			eth0_mac_addr[cnt] = hex_string2int(ptr);
			cnt++;
			ptr = tail+1;
		} else 
			ptr++;
	}
	kfree(ptr);
	
	ptr = strdup(eth1_mac_string);
	cnt = 0 ;
	while (cnt < 6 )
	{
		char *tail;
		
		if ( *ptr == '\0')
			break;
		if ( *ptr != ' ')
		{
			tail = ptr;
			while ( *tail != ' ' && *tail != '\0' )
				tail++;
			*tail = '\0' ;
			eth1_mac_addr[cnt] = hex_string2int(ptr);
			cnt++;
			ptr = tail+1;
		} else 
			ptr++;
	}
	kfree(ptr);

	return dmfe_probe(0);              /* search board and register */
}

/* Description: 
   when user used rmmod to delete module, system invoked clean_module()
   to  un-register DEVICE.
*/
void cleanup_module(void)
{
	board_info_t * db;
	DMFE_DBUG(0, "clean_module()", 0);

	if ( dmfe_eth0_dev ) 
	{
		db = (board_info_t *)dmfe_eth0_dev->priv;
		iounmap((void*)db->ioaddr);
		release_region(dmfe_eth0_dev->base_addr, 2);
		kfree(db);		/* free board information */
		kfree(dmfe_eth0_dev);	/* free device structure */
		unregister_netdev(dmfe_eth0_dev);
	}
	if ( dmfe_eth1_dev )
	{
		db = (board_info_t *)dmfe_eth1_dev->priv;
		iounmap((void*)db->ioaddr);
		release_region(dmfe_eth1_dev->base_addr, 2);
		kfree(db);		/* free board information */
		kfree(dmfe_eth1_dev);	/* free device structure */
		unregister_netdev(dmfe_eth1_dev);
	}
	DMFE_DBUG(0, "clean_module() exit", 0);
}

#endif  /* MODULE */

