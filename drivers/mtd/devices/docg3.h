#include <linux/mtd/nand_bch.h>

/*
 * Flash memory areas :
 *   - 0x0000 .. 0x07ff : IPL
 *   - 0x0800 .. 0x0fff : Data area
 *   - 0x1000 .. 0x17ff : Registers
 *   - 0x1800 .. 0x1fff : Unknown
 */
#define DOC_IOSPACE_IPL			0x0000
#define DOC_IOSPACE_DATA		0x0800
#define DOC_IOSPACE_SIZE		0x2000

/*
 * DOC G3 layout and adressing scheme
 *   A page address for the block "b", plane "P" and page "p":
 *   address = [bbbb bPpp pppp]
 */

#define DOC_ADDR_PAGE_MASK		0x3f
#define DOC_ADDR_PLANE_MASK		0x40
#define DOC_ADDR_BLOCK_SHIFT		7
#define DOC_LAYOUT_PAGES_PER_BLOCK	DOC_ADDR_PLANE_MASK
#define DOC_LAYOUT_PAGE_SIZE		512
#define DOC_LAYOUT_OOB_SIZE		16
#define DOC_LAYOUT_WEAR_SIZE		8
#define DOC_LAYOUT_PAGE_OOB_SIZE				\
	(DOC_LAYOUT_PAGE_SIZE + DOC_LAYOUT_OOB_SIZE)
#define DOC_LAYOUT_WEAR_OFFSET		(DOC_LAYOUT_PAGE_OOB_SIZE * 2)
#define DOC_LAYOUT_BLOCK_SIZE					\
	(DOC_LAYOUT_PAGES_PER_BLOCK * DOC_LAYOUT_PAGE_SIZE)
#define DOC_ECC_BCH_SIZE		7
#define DOC_ECC_BCH_COVERED_BYTES				\
	(DOC_LAYOUT_PAGE_SIZE + DOC_LAYOUT_OOB_PAGEINFO_SZ +	\
	 DOC_LAYOUT_OOB_HAMMING_SZ + DOC_LAYOUT_OOB_BCH_SZ)

/*
 * Blocks distribution
 */
#define DOC_LAYOUT_BLOCK_BBT		0
#define DOC_LAYOUT_BLOCK_OTP		0
#define DOC_LAYOUT_BLOCK_FIRST_DATA	6

#define DOC_LAYOUT_PAGE_BBT		4

/*
 * Extra page OOB (16 bytes wide) layout
 */
#define DOC_LAYOUT_OOB_PAGEINFO_OFS	0
#define DOC_LAYOUT_OOB_HAMMING_OFS	7
#define DOC_LAYOUT_OOB_BCH_OFS		8
#define DOC_LAYOUT_OOB_UNUSED_OFS	15
#define DOC_LAYOUT_OOB_PAGEINFO_SZ	7
#define DOC_LAYOUT_OOB_HAMMING_SZ	1
#define DOC_LAYOUT_OOB_BCH_SZ		7
#define DOC_LAYOUT_OOB_UNUSED_SZ	1


#define DOC_CHIPID_G3			0x200
#define DOC_ERASE_MARK			0xaa
/*
 * Flash registers
 */
#define DoC_ChipID			0x1000
#define DoC_Test			0x1004
#define DoC_BusLock			0x1006
#define DoC_EndianControl		0x1008
#define DoC_DeviceSelect		0x100a
#define DoC_AsicMode			0x100c
#define DoC_Configuration		0x100e
#define DoC_InterruptControl		0x1010
#define DoC_ReadAddress			0x101a
#define DoC_DataEnd			0x101e
#define DoC_InterruptStatus		0x1020

#define DoC_FlashSequence		0x1032
#define DoC_FlashCommand		0x1034
#define DoC_FlashAddress		0x1036
#define DoC_FlashControl		0x1038
#define DoC_NOP				0x103e

#define DoC_EccConf0			0x1040
#define DoC_EccConf1			0x1042
#define DoC_EccPreset			0x1044
#define DoC_HammingParity		0x1046
#define DoC_BCH_Syndrom(idx)		(0x1048 + (idx << 1))

#define DoC_AsicModeConfirm		0x1072
#define DoC_ChipID_Inv			0x1074

/*
 * Flash sequences
 * A sequence is preset before one or more commands are input to the chip.
 */
#define DoC_Seq_RESET			0x00
#define DoC_Seq_PAGE_SIZE_532		0x03
#define DoC_Seq_SET_MODE		0x09
#define DoC_Seq_READ			0x12
#define DoC_Seq_SET_PLANE1		0x0e
#define DoC_Seq_SET_PLANE2		0x10
#define DoC_Seq_PAGE_SETUP		0x1d

/*
 * Flash commands
 */
#define DoC_Cmd_READ_PLANE1		0x00
#define DoC_Cmd_SET_ADDR_READ		0x05
#define DoC_Cmd_READ_ALL_PLANES		0x30
#define DoC_Cmd_READ_PLANE2		0x50
#define DoC_Cmd_READ_FLASH		0xe0
#define DoC_Cmd_PAGE_SIZE_532		0x3c

#define DoC_Cmd_PROG_BLOCK_ADDR		0x60
#define DoC_Cmd_PROG_CYCLE1		0x80
#define DoC_Cmd_PROG_CYCLE2		0x10
#define DoC_Cmd_ERASECYCLE2		0xd0

#define DoC_Cmd_RELIABLE_MODE		0x22
#define DoC_Cmd_FAST_MODE		0xa2

#define DoC_Cmd_RESET			0xff

/*
 * Flash register : DoC_FlashControl
 */
#define Doc_Ctrl_Violation		0x20
#define Doc_Ctrl_CE			0x10
#define Doc_Ctrl_UNKNOWN_BITS		0x08
#define Doc_Ctrl_PROTECTION_ERROR	0x04
#define Doc_Ctrl_SEQUENCE_ERROR		0x02
#define Doc_Ctrl_FLASHREADY		0x01

/*
 * Flash register : DoC_AsicMode
 */
#define Doc_AsicMode_RESET		0x00
#define Doc_AsicMode_NORMAL		0x01
#define Doc_AsicMode_POWERDOWN		0x02
#define Doc_AsicMode_MDWREN		0x04
#define Doc_AsicMode_BDETCT_RESET	0x08
#define Doc_AsicMode_RSTIN_RESET	0x10
#define Doc_AsicMode_RAM_WE		0x20

/*
 * Flash register : DoC_EccConf0
 */
#define Doc_ECCConf0_READ_MODE		0x8000
#define Doc_ECCConf0_AUTO_ECC_ENABLE	0x4000
#define Doc_ECCConf0_HAMMING_ENABLE	0x1000
#define Doc_ECCConf0_BCH_ENABLE		0x0800
#define Doc_ECCConf0_DATA_BYTES_MASK	0x07ff

/*
 * Flash register : DoC_EccConf1
 */
#define Doc_ECCConf1_BCH_SYNDROM_ERR	0x80
#define Doc_ECCConf1_UNKOWN1		0x40
#define Doc_ECCConf1_UNKOWN2		0x20
#define Doc_ECCConf1_UNKOWN3		0x10
#define Doc_ECCConf1_HAMMING_BITS_MASK	0x0f

/*
 * Flash register : DoC_Configuration
 */
#define Doc_Conf_IF_CFG			0x80
#define Doc_Conf_MAX_ID_MASK		0x30
#define Doc_Conf_VCCQ_3V		0x01

/*
 * Flash register : DoC_ReadAddress
 */
#define Doc_ReadAddr_INC		0x8000
#define Doc_ReadAddr_ONE_BYTE		0x4000
#define Doc_ReadAddr_ADDR_MASK		0x1fff

/**
 * struct docg3 - DiskOnChip driver private data
 * @dev: the device currently under control
 * @base: mapped IO space
 * @device_id: number of the cascaded DoCG3 device (0, 1, 2 or 3)
 * @if_cfg: if true, reads are on 16bits, else reads are on 8bits
 * @bbt: bad block table cache
 */
struct docg3 {
	struct device *dev;
	void __iomem *base;
	int device_id:4;
	int if_cfg:1;
	int max_block;
	u8 *bbt;
};

#define doc_err(fmt, arg...) dev_err(docg3->dev, (fmt), ## arg)
#define doc_info(fmt, arg...) dev_info(docg3->dev, (fmt), ## arg)
#define doc_dbg(fmt, arg...) dev_dbg(docg3->dev, (fmt), ## arg)
#define doc_vdbg(fmt, arg...) dev_vdbg(docg3->dev, (fmt), ## arg)
