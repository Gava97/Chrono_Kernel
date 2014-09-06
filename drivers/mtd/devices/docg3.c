/*
 * Handles the M-Systems DiskOnChip G3 chip
 *
 * Copyright (C) 2011 Robert Jarzmik
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

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/platform_device.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>

#include "docg3.h"

/*
 * This driver handles the DiskOnChip G3 flash memory.
 *
 * As no specification is available from M-Systems/Sandisk, this drivers lacks
 * several functions available on the chip, as :
 *  - block erase
 *  - page write
 *  - IPL write
 *  - ECC fixing (lack of BCH algorith understanding)
 *  - powerdown / powerup
 *
 * The bus data width (8bits versus 16bits) is not handled (if_cfg flag), and
 * the driver assumes a 16bits data bus.
 *
 * DocG3 relies on 2 ECC algorithms, which are handled in hardware :
 *  - a 1 byte Hamming code stored in the OOB for each page
 *  - a 7 bytes BCH code stored in the OOB for each page
 * The BCH part is only used for check purpose, no correction is available as
 * some information is missing. What is known is that :
 *  - BCH is in GF(2^14)
 *  - BCH is over data of 520 bytes (512 page + 7 page_info bytes
 *                                   + 1 hamming byte)
 *  - BCH can correct up to 4 bits (t = 4)
 *  - BCH syndroms are calculated in hardware, and checked in hardware as well
 *
 */

#define doc_readb(reg)	\
	__raw_readb(docg3->base + (reg));
#define doc_writeb(value, reg)						\
do {									\
	doc_vdbg("Write %02x to register %04x\n", (value), (reg));	\
	__raw_writeb((value), docg3->base + (reg));			\
} while (0)
#define doc_readw(reg)	\
	__raw_readw(docg3->base + (reg));
#define doc_writew(value, reg)						\
do {									\
	doc_vdbg("Write %04x to register %04x\n", (value), (reg));	\
	__raw_writew((value), docg3->base + (reg));			\
} while (0)

#define doc_flashCommand(cmd)						\
do {									\
	doc_dbg("doc_flashCommand:  %02x " #cmd "\n", DoC_Cmd_##cmd);	\
	doc_writeb(DoC_Cmd_##cmd, DoC_FlashCommand);			\
} while (0)

#define doc_flashSequence(seq)						\
do {									\
	doc_dbg("doc_flashSequence: %02x " #seq "\n", DoC_Seq_##seq);	\
	doc_writeb(DoC_Seq_##seq, DoC_FlashSequence);			\
} while (0)

#define doc_flashAddress(addr)						\
do {									\
	doc_dbg("doc_flashAddress:  %02x\n", (addr));			\
	doc_writeb((addr), DoC_FlashAddress);				\
} while (0)

static const char const *part_probes[] = { "cmdlinepart", "saftlpart", NULL };

static int doc_register_readb(struct docg3 *docg3, int reg)
{
	u8 val;

	doc_writew(reg, DoC_ReadAddress);
	val = doc_readb(reg);
	doc_vdbg("Read register %04x : %02x\n", reg, val);
	return val;
}

static int doc_register_readw(struct docg3 *docg3, int reg)
{
	u16 val;

	doc_writew(reg, DoC_ReadAddress);
	val = doc_readw(reg);
	doc_vdbg("Read register %04x : %04x\n", reg, val);
	return val;
}

static void doc_delay(struct docg3 *docg3, int nbNOPs)
{
	int i;

	doc_dbg("NOP x %d\n", nbNOPs);
	for (i = 0; i < nbNOPs; i++)
		doc_writeb(0, DoC_NOP);
}

static int is_prot_seq_error(struct docg3 *docg3)
{
	int ctrl;

	ctrl = doc_register_readb(docg3, DoC_FlashControl);
	return ctrl & (Doc_Ctrl_PROTECTION_ERROR | Doc_Ctrl_SEQUENCE_ERROR);
}

static int doc_is_ready(struct docg3 *docg3)
{
	int ctrl;

	ctrl = doc_register_readb(docg3, DoC_FlashControl);
	return ctrl & Doc_Ctrl_FLASHREADY;
}

static int doc_wait_ready(struct docg3 *docg3)
{
	int maxWaitCycles = 100;

	do {
		doc_delay(docg3, 4);
	} while (!doc_is_ready(docg3) && maxWaitCycles--);
	doc_delay(docg3, 2);
	if (maxWaitCycles > 0)
		return 0;
	else
		return -EIO;
}

static int doc_reset_seq(struct docg3 *docg3)
{
	int ret;

	doc_writeb(0x10, DoC_FlashControl);
	doc_flashSequence(RESET);
	doc_flashCommand(RESET);
	doc_delay(docg3, 2);
	ret = doc_wait_ready(docg3);

	doc_dbg("doc_reset_seq() -> isReady=%s\n", ret ? "false" : "true");
	return ret;
}

/**
 * doc_read_data_area - Read data from data area
 * @docg3: the device
 * @buf: the buffer to fill in
 * @len: the lenght to read
 * @first: first time read, DoC_ReadAddress should be set
 *
 * Reads bytes from flash data. Handles the single byte / even bytes reads.
 */
static void doc_read_data_area(struct docg3 *docg3, void *buf, int len,
			       int first)
{
	int i, cdr, len4;
	u16 data16, *dst16;
	u8 data8, *dst8;

	doc_dbg("doc_read_data_area(buf=%p, len=%d)\n", buf, len);
	cdr = len & 0x3;
	len4 = len - cdr;

	if (first)
		doc_writew(DOC_IOSPACE_DATA, DoC_ReadAddress);
	dst16 = buf;
	for (i = 0; i < len4; i += 2) {
		data16 = doc_readw(DOC_IOSPACE_DATA);
		*dst16 = data16;
		dst16++;
	}

	if (cdr) {
		doc_writew(DOC_IOSPACE_DATA | Doc_ReadAddr_ONE_BYTE,
			   DoC_ReadAddress);
		doc_delay(docg3, 1);
		dst8 = (u8 *)dst16;
		for (i = 0; i < cdr; i++) {
			data8 = doc_readb(DOC_IOSPACE_DATA);
			*dst8 = data8;
			dst8++;
		}
	}
}

/**
 * doc_set_data_mode - Sets the flash to reliable data mode
 * @docg3: the device
 *
 * The reliable data mode is a bit slower than the fast mode, but less errors
 * occur.  Entering the reliable mode cannot be done without entering the fast
 * mode first.
 */
static void doc_set_reliable_mode(struct docg3 *docg3)
{
	doc_dbg("doc_set_reliable_mode()\n");
	doc_flashSequence(SET_MODE);
	doc_flashCommand(FAST_MODE);
	doc_flashCommand(RELIABLE_MODE);
	doc_delay(docg3, 2);
}

/**
 * doc_set_asic_mode - Set the ASIC mode
 * @docg3: the device
 * @mode: the mode
 *
 * The ASIC can work in 3 modes :
 *  - RESET: all registers are zeroed
 *  - NORMAL: receives and handles commands
 *  - POWERDOWN: minimal poweruse, flash parts shut off
 */
static void doc_set_asic_mode(struct docg3 *docg3, u8 mode)
{
	int i;

	for (i = 0; i < 12; i++)
		doc_readb(DOC_IOSPACE_IPL);

	mode |= Doc_AsicMode_MDWREN;
	doc_dbg("doc_set_asic_mode(%02x)\n", mode);
	doc_writeb(mode, DoC_AsicMode);
	doc_writeb(~mode, DoC_AsicModeConfirm);
	doc_delay(docg3, 1);
}

/**
 * doc_set_device_id - Sets the devices id for cascaded G3 chips
 * @docg3: the device
 * @id: the chip to select (amongst 0, 1, 2, 3)
 *
 * There can be 4 cascaded G3 chips. This function selects the one which will
 * should be the active one.
 */
static void doc_set_device_id(struct docg3 *docg3, int id)
{
	u8 ctrl;

	doc_dbg("doc_set_device_id(%d)\n", id);
	doc_writeb(id, DoC_DeviceSelect);
	ctrl = doc_register_readb(docg3, DoC_FlashControl);

	ctrl &= ~Doc_Ctrl_Violation;
	ctrl |= Doc_Ctrl_CE;
	doc_writeb(ctrl, DoC_FlashControl);
}

/**
 * doc_set_extra_page_mode - Change flash page layout
 * @docg3: the device
 *
 * Normally, the flash page is split into the data (512 bytes) and the out of
 * band data (16 bytes). For each, 4 more bytes can be accessed, where the wear
 * leveling counters are stored.  To access this last area of 4 bytes, a special
 * mode must be input to the flash ASIC.
 *
 * Returns 0 if no error occured, -EIO else.
 */
static int doc_set_extra_page_mode(struct docg3 *docg3)
{
	int fctrl;

	doc_dbg("doc_set_extra_page_mode()\n");
	doc_flashSequence(PAGE_SIZE_532);
	doc_flashCommand(PAGE_SIZE_532);
	doc_delay(docg3, 2);

	fctrl = doc_register_readb(docg3, DoC_FlashControl);
	if (fctrl & (Doc_Ctrl_PROTECTION_ERROR | Doc_Ctrl_SEQUENCE_ERROR))
		return -EIO;
	else
		return 0;
}

/**
 * doc_seek - Set both flash planes to the specified block, page for reading
 * @docg3: the device
 * @block: the block index
 * @page: the page index within the block
 * @wear: if true, read will occur on the 4 extra bytes of the wear area
 * @ofs: offset in page to read
 *
 * Programs the flash even and odd planes to the specific block and page.
 * Alternatively, programs the flash to the wear area of the specified page.
 */
static int doc_read_seek(struct docg3 *docg3, int block, int page, int wear,
	int ofs)
{
	int sector, ret = 0;

	sector = (block << DOC_ADDR_BLOCK_SHIFT) + (page & DOC_ADDR_PAGE_MASK);
	doc_dbg("doc_seek(block=%d, page=%d, wear=%d) => sector=%d\n",
		block, page, wear, sector);

	if (!wear && (ofs < 2 * DOC_LAYOUT_PAGE_SIZE)) {
		doc_flashSequence(SET_PLANE1);
		doc_flashCommand(READ_PLANE1);
		doc_delay(docg3, 2);
	} else {
		doc_flashSequence(SET_PLANE2);
		doc_flashCommand(READ_PLANE2);
		doc_delay(docg3, 2);
	}

	doc_set_reliable_mode(docg3);
	if (wear)
		ret = doc_set_extra_page_mode(docg3);
	if (ret)
		goto out;

	doc_flashSequence(READ);
	doc_flashCommand(PROG_BLOCK_ADDR);
	doc_delay(docg3, 1);
	doc_flashAddress(sector & 0xff);
	doc_flashAddress((sector >> 8) & 0xff);
	doc_flashAddress((sector >> 16) & 0xff);
	doc_delay(docg3, 1);

	sector |= DOC_ADDR_PLANE_MASK;
	doc_flashCommand(PROG_BLOCK_ADDR);
	doc_delay(docg3, 1);
	doc_flashAddress(sector & 0xff);
	doc_flashAddress((sector >> 8) & 0xff);
	doc_flashAddress((sector >> 16) & 0xff);
	doc_delay(docg3, 2);

out:
	return ret;
}

/**
 * doc_read_page_ecc_init - Initialize hardware ECC engine
 * @docg3: the device
 * @len: the number of bytes covered by the ECC (BCH covered)
 *
 * The function does initialize the hardware ECC engine to compute the Hamming
 * ECC (on 1 byte) and the BCH Syndroms (on 7 bytes).
 *
 * Return 0 if succeeded, -EIO on error
 */
static int doc_read_page_ecc_init(struct docg3 *docg3, int len)
{
	doc_writew(Doc_ECCConf0_READ_MODE
		   | Doc_ECCConf0_BCH_ENABLE | Doc_ECCConf0_HAMMING_ENABLE
		   | (len & Doc_ECCConf0_DATA_BYTES_MASK),
		   DoC_EccConf0);
	doc_delay(docg3, 4);
	doc_register_readb(docg3, DoC_FlashControl);
	return doc_wait_ready(docg3);
}

/**
 * doc_read_page_prepare - Prepares reading data from a flash page
 * @docg3: the device
 * @block: the block index on flash memory
 * @page: the page index in the block
 * @offset: the offset in the page (must be a multiple of 4)
 *
 * Prepares the page to be read in the flash memory :
 *   - tell ASIC to map the flash pages
 *   - tell ASIC to be in read mode
 *
 * After a call to this method, a call to doc_read_page_finish is mandatory,
 * to end the read cycle of the flash.
 *
 * Read data from a flash page. The length to be read must be between 0 and
 * (page_size + oob_size + wear_size), ie. 532, and a multiple of 4 (because
 * the extra bytes reading is not implemented).
 *
 * As pages are grouped by 2 (in 2 planes), reading from odd pages requires to
 * input the preceeding even page number, and an offset of 512+16 to shift to
 * the next page.
 *
 * Returns 0 if successful, -EIO if a read error occured.
 */
static int doc_read_page_prepare(struct docg3 *docg3, int block, int page,
				 int offset)
{
	int wear_area = 0, ret = 0;

	doc_dbg("doc_read_page_prepare(block=%d, page=%d, offsetInPage=%d)\n",
		block, page, offset);
	if (offset >= DOC_LAYOUT_WEAR_OFFSET)
		wear_area = 1;
	if (!wear_area && offset > DOC_LAYOUT_PAGE_OOB_SIZE)
		return -EINVAL;

	if (page & 0x01) {
		page--;
		offset += DOC_LAYOUT_PAGE_OOB_SIZE;
	}

	doc_set_device_id(docg3, docg3->device_id);
	ret = doc_reset_seq(docg3);
	if (ret)
		goto err;

	/* Program the flash address block and page */
	ret = doc_read_seek(docg3, block, page, wear_area, offset);
	if (ret)
		goto err;

	doc_flashCommand(READ_ALL_PLANES);
	doc_delay(docg3, 2);
	doc_wait_ready(docg3);

	doc_flashCommand(SET_ADDR_READ);
	doc_delay(docg3, 1);
	if (offset >= DOC_LAYOUT_PAGE_SIZE * 2)
		offset -= 2 * DOC_LAYOUT_PAGE_SIZE;
	doc_flashAddress(offset >> 2);
	doc_delay(docg3, 1);
	doc_wait_ready(docg3);

	doc_flashCommand(READ_FLASH);

	return 0;
err:
	doc_writeb(0, DoC_DataEnd);
	doc_delay(docg3, 2);
	return -EIO;
}

/**
 * doc_read_page_getbytes - Reads bytes from a prepared page
 * @docg3: the device
 * @len: the number of bytes to be read (must be a multiple of 4)
 * @buf: the buffer to be filled in
 * @first: 1 if first time read, DoC_ReadAddress should be set
 *
 */
static int doc_read_page_getbytes(struct docg3 *docg3, int len, u_char *buf,
				  int first)
{
	doc_read_data_area(docg3, buf, len, first);
	doc_delay(docg3, 2);
	return len;
}

/**
 * doc_get_hw_bch_syndroms - Get hardware calculated BCH syndroms
 * @docg3: the device
 * @syns:  the array of 7 integers where the syndroms will be stored
 */
static void doc_get_hw_bch_syndroms(struct docg3 *docg3, int *syns)
{
	int i;

	for (i = 0; i < DOC_ECC_BCH_SIZE; i++)
		syns[i] = doc_register_readb(docg3, DoC_BCH_Syndrom(i));
}

/**
 * doc_read_page_finish - Ends reading of a flash page
 * @docg3: the device
 */
static void doc_read_page_finish(struct docg3 *docg3)
{
	doc_writeb(0, DoC_DataEnd);
	doc_delay(docg3, 2);
}

/**
 * doc_read - Read bytes from flash
 * @mtd: the device
 * @from: the offset from first block and first page, in bytes, aligned on page
 *        size
 * @len: the number of bytes to read (must be a multiple of 4)
 * @retlen: the number of bytes actually read
 * @buf: the filled in buffer
 *
 * Reads flash memory pages. This function does not read the OOB chunk, but only
 * the page data.
 *
 * Returns 0 if read successfull, of -EIO, -EINVAL if an error occured
 */
int doc_read(struct mtd_info *mtd, loff_t from, size_t len,
	     size_t *retlen, u_char *buf)
{
	struct docg3 *docg3 = mtd->priv;
	int block, page, sector, readlen, ret;
	int syn[DOC_ECC_BCH_SIZE], eccconf1;
	u8 oob[DOC_LAYOUT_OOB_SIZE];

	ret = -EINVAL;
	doc_dbg("doc_read(from=%lld, len=%u, buf=%p)\n", from, len, buf);
	if (from % DOC_LAYOUT_PAGE_SIZE)
		goto err;
	if (len % 4)
		goto err;
	sector = from / DOC_LAYOUT_PAGE_SIZE;
	block = sector / DOC_LAYOUT_PAGES_PER_BLOCK;
	page = sector & DOC_ADDR_PAGE_MASK;
	if (block > docg3->max_block)
		goto err;

	*retlen = 0;
	ret = 0;
	readlen = min(len, (size_t)DOC_LAYOUT_PAGE_SIZE);
	while (!ret && len > 0) {
		readlen = min(len, (size_t)DOC_LAYOUT_PAGE_SIZE);
		ret = doc_read_page_prepare(docg3, block, page, 0);
		if (ret < 0)
			goto err;
		ret = doc_read_page_ecc_init(docg3, DOC_ECC_BCH_COVERED_BYTES);
		if (ret < 0)
			goto err_in_read;
		ret = doc_read_page_getbytes(docg3, readlen, buf, 1);
		if (ret < readlen)
			goto err_in_read;
		ret = doc_read_page_getbytes(docg3, DOC_LAYOUT_OOB_SIZE,
					     oob, 0);
		if (ret < DOC_LAYOUT_OOB_SIZE)
			goto err_in_read;

		*retlen += readlen;
		buf += readlen;
		len -= readlen;
		page++;
		if (page > DOC_ADDR_PAGE_MASK) {
			page = 0;
			block++;
		}

		/*
		 * There should be a BCH bitstream fixing algorithm here ...
		 * By now, a page read failure is triggered by BCH error
		 */
		doc_get_hw_bch_syndroms(docg3, syn);
		eccconf1 = doc_register_readb(docg3, DoC_EccConf1);

		doc_dbg("OOB - INFO: %02x:%02x:%02x:%02x:%02x:%02x:%02x\n",
			 oob[0], oob[1], oob[2], oob[3], oob[4],
			 oob[5], oob[6]);
		doc_dbg("OOB - HAMMING: %02x\n", oob[7]);
		doc_dbg("OOB - BCH_ECC: %02x:%02x:%02x:%02x:%02x:%02x:%02x\n",
			 oob[8], oob[9], oob[10], oob[11], oob[12],
			 oob[13], oob[14]);
		doc_dbg("OOB - UNUSED: %02x\n", oob[15]);
		doc_dbg("ECC checks: ECCConf1=%x\n", eccconf1);
		doc_dbg("ECC BCH syndrom: %02x:%02x:%02x:%02x:%02x:%02x:%02x\n",
			syn[0], syn[1], syn[2], syn[3], syn[4], syn[5], syn[6]);

		ret = -EBADMSG;
		if (block >= DOC_LAYOUT_BLOCK_FIRST_DATA) {
			if (eccconf1 & Doc_ECCConf1_BCH_SYNDROM_ERR)
				goto err_in_read;
			if (is_prot_seq_error(docg3))
				goto err_in_read;
		}
		doc_read_page_finish(docg3);
	}

	return 0;
err_in_read:
	doc_read_page_finish(docg3);
err:
	return ret;
}

/**
 * doc_read_oob - Read out of band bytes from flash
 * @mtd: the device
 * @from: the offset from first block and first page, in bytes, aligned on page
 *        size
 * @ops: the mtd oob structure
 *
 * Reads flash memory OOB area of pages.
 *
 * Returns 0 if read successfull, of -EIO, -EINVAL if an error occured
 */
int doc_read_oob(struct mtd_info *mtd, loff_t from, struct mtd_oob_ops *ops)
{
	struct docg3 *docg3 = mtd->priv;
	int block, page, sector, ret;
	u8 *buf = ops->oobbuf;
	size_t len = ops->ooblen;

	doc_dbg("doc_read_oob(from=%lld, buf=%p, len=%d)\n", from, buf, len);
	if (len != DOC_LAYOUT_OOB_SIZE)
		return -EINVAL;

	switch (ops->mode) {
	case MTD_OOB_PLACE:
		buf += ops->ooboffs;
		break;
	default:
		break;
	}

	sector = from / DOC_LAYOUT_PAGE_SIZE;
	block = sector / DOC_LAYOUT_PAGES_PER_BLOCK;
	page = sector & DOC_ADDR_PAGE_MASK;
	if (block > docg3->max_block)
		return -EINVAL;

	ret = doc_read_page_prepare(docg3, block, page, DOC_LAYOUT_PAGE_SIZE);
	if (!ret)
		ret = doc_read_page_ecc_init(docg3, DOC_LAYOUT_OOB_SIZE);
	if (!ret)
		ret = doc_read_page_getbytes(docg3, DOC_LAYOUT_OOB_SIZE,
					     buf, 1);
	doc_read_page_finish(docg3);

	if (ret > 0)
		ops->oobretlen = ret;
	else
		ops->oobretlen = 0;
	return (ret > 0) ? 0 : ret;
}

static int doc_reload_bbt(struct docg3 *docg3)
{
	int block = DOC_LAYOUT_BLOCK_BBT;
	int ret = 0, nbpages, page;
	u_char *buf = docg3->bbt;

	nbpages = (docg3->max_block + 1) / (8 * DOC_LAYOUT_PAGE_SIZE);
	for (page = 0; !ret && (page < nbpages); page++) {
		ret = doc_read_page_prepare(docg3, block,
					    page + DOC_LAYOUT_PAGE_BBT, 0);
		if (!ret)
			ret = doc_read_page_ecc_init(docg3,
						     DOC_LAYOUT_PAGE_SIZE);
		if (!ret)
			doc_read_page_getbytes(docg3, DOC_LAYOUT_PAGE_SIZE,
					       buf, 1);
		buf += DOC_LAYOUT_PAGE_SIZE;
	}
	doc_read_page_finish(docg3);
	return ret;
}

/**
 * doc_block_isbad - Checks whether a block is good or not
 * @mtd: the device
 * @ofs: the offset to find the correct block
 *
 * Returns 1 if block is bad, 0 if block is good
 */
int doc_block_isbad(struct mtd_info *mtd, loff_t ofs)
{
	struct docg3 *docg3 = mtd->priv;
	int block, sector, is_good;

	doc_dbg("doc_block_isbad(from=%lld)\n", ofs);

	sector = ofs / DOC_LAYOUT_PAGE_SIZE;
	block = sector / DOC_LAYOUT_PAGES_PER_BLOCK;
	if (block < DOC_LAYOUT_BLOCK_FIRST_DATA)
		return 0;
	if (block > docg3->max_block)
		return -EINVAL;

	is_good = docg3->bbt[block >> 3] & (1 << (block & 0x7));
	return !is_good;
}

/**
 * doc_get_erase_count - Get block erase count
 * @docg3: the device
 * @from: the offset in which the block is.
 *
 * Get the number of times a block was erased. The number is the maximum of
 * erase times between first and second plane (which should be equal normally).
 *
 * Returns The number of erases, or -EINVAL or -EIO on error.
 */
int doc_get_erase_count(struct docg3 *docg3, loff_t from)
{
	u8 buf[DOC_LAYOUT_WEAR_SIZE];
	int ret, plane1_erase_count, plane2_erase_count;
	int block, page, sector;

	doc_dbg("doc_get_erase_count(from=%lld, buf=%p)\n", from, buf);
	if (from % DOC_LAYOUT_PAGE_SIZE)
		return -EINVAL;
	sector = from / DOC_LAYOUT_PAGE_SIZE;
	block = sector / DOC_LAYOUT_PAGES_PER_BLOCK;
	page = sector & DOC_ADDR_PAGE_MASK;
	if (block > docg3->max_block)
		return -EINVAL;

	ret = doc_reset_seq(docg3);
	if (!ret)
		ret = doc_read_page_prepare(docg3, block, page,
					    DOC_LAYOUT_WEAR_OFFSET);
	if (!ret)
		ret = doc_read_page_getbytes(docg3, DOC_LAYOUT_WEAR_SIZE,
					     buf, 1);
	doc_read_page_finish(docg3);

	if (ret || (buf[0] != DOC_ERASE_MARK) || (buf[2] != DOC_ERASE_MARK))
		return -EIO;
	plane1_erase_count = (u8)(~buf[1]) | ((u8)(~buf[4]) << 8)
		| ((u8)(~buf[5]) << 16);
	plane2_erase_count = (u8)(~buf[3]) | ((u8)(~buf[6]) << 8)
		| ((u8)(~buf[7]) << 16);

	return max(plane1_erase_count, plane2_erase_count);
}

/*
 * Debug sysfs entries
 */
#ifdef DEBUG
static ssize_t dbg_flashctrl_read(struct device *dev,
				      struct device_attribute *attr,
				      char *buf)
{
	struct mtd_info *mtd = dev_get_drvdata(dev);
	struct docg3 *docg3 = mtd->priv;

	int pos = 0;
	u8 fctrl = doc_register_readb(docg3, DoC_FlashControl);

	pos += scnprintf(buf + pos, PAGE_SIZE - pos,
		 "FlashControl : 0x%02x (%s,CE# %s,%s,%s,flash %s)\n",
		 fctrl,
		 fctrl & Doc_Ctrl_Violation ? "protocol violation" : "-",
		 fctrl & Doc_Ctrl_CE ? "active" : "inactive",
		 fctrl & Doc_Ctrl_PROTECTION_ERROR ? "protection error" : "-",
		 fctrl & Doc_Ctrl_SEQUENCE_ERROR ? "sequence error" : "-",
		 fctrl & Doc_Ctrl_FLASHREADY ? "ready" : "not ready");
	return pos;
}
static ssize_t dbg_flashctrl_write(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t count)
{
	struct mtd_info *mtd = dev_get_drvdata(dev);
	struct docg3 *docg3 = mtd->priv;
	int i, val;

	i = sscanf(buf, "0x%x", &val);
	if (i == 0)
		i = sscanf(buf, "%x", &val);
	if (i == 0)
		return -EINVAL;
	doc_writeb(val, DoC_FlashControl);
	return count;
}
DEVICE_ATTR(flashcontrol, S_IRWXU, dbg_flashctrl_read, dbg_flashctrl_write);

static ssize_t dbg_reset(struct device *dev,
			     struct device_attribute *attr,
			     const char *buf, size_t count)
{
	struct mtd_info *mtd = dev_get_drvdata(dev);
	struct docg3 *docg3 = mtd->priv;

	doc_reset_seq(docg3);
	return count;
}
DEVICE_ATTR(reset, S_IWUSR, NULL, dbg_reset);

static ssize_t dbg_asicmode_read(struct device *dev,
				 struct device_attribute *attr,
				 char *buf)
{
	struct mtd_info *mtd = dev_get_drvdata(dev);
	struct docg3 *docg3 = mtd->priv;

	int pos = 0;
	int pctrl = doc_register_readb(docg3, DoC_AsicMode);
	int mode = pctrl & 0x03;

	pos += scnprintf(buf + pos, PAGE_SIZE - pos,
			 "%04x : RAM_WE=%d,RSTIN_RESET=%d,BDETCT_RESET=%d,WRITE_ENABLE=%d,POWERDOWN=%d,MODE=%d%d (",
			 pctrl,
			 pctrl & Doc_AsicMode_RAM_WE ? 1 : 0,
			 pctrl & Doc_AsicMode_RSTIN_RESET ? 1 : 0,
			 pctrl & Doc_AsicMode_BDETCT_RESET ? 1 : 0,
			 pctrl & Doc_AsicMode_MDWREN ? 1 : 0,
			 pctrl & Doc_AsicMode_POWERDOWN ? 1 : 0,
			 mode >> 1, mode & 0x1);

	switch (mode) {
	case Doc_AsicMode_RESET:
		pos += scnprintf(buf + pos, PAGE_SIZE - pos, "reset");
		break;
	case Doc_AsicMode_NORMAL:
		pos += scnprintf(buf + pos, PAGE_SIZE - pos, "normal");
		break;
	case Doc_AsicMode_POWERDOWN:
		pos += scnprintf(buf + pos, PAGE_SIZE - pos, "powerdown");
		break;
	}
	pos += scnprintf(buf + pos, PAGE_SIZE - pos, ")\n");
	return pos;
}
static ssize_t dbg_asicmode_write(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	struct mtd_info *mtd = dev_get_drvdata(dev);
	struct docg3 *docg3 = mtd->priv;
	int i, val;

	i = sscanf(buf, "0x%x", &val);
	if (i == 1)
		doc_set_asic_mode(docg3, val);
	i = sscanf(buf, "%x", &val);
	if (i == 1)
		doc_set_asic_mode(docg3, val);

	return count;
}
DEVICE_ATTR(asic_mode, S_IRWXU, dbg_asicmode_read, dbg_asicmode_write);

static ssize_t dbg_deviceid_read(struct device *dev,
				 struct device_attribute *attr,
				 char *buf)
{
	struct mtd_info *mtd = dev_get_drvdata(dev);
	struct docg3 *docg3 = mtd->priv;
	int pos = 0;
	int id = doc_register_readb(docg3, DoC_DeviceSelect);

	pos += scnprintf(buf + pos, PAGE_SIZE - pos, "DeviceId = %d\n", id);
	return pos;
}
DEVICE_ATTR(device_id, S_IRUGO, dbg_deviceid_read, NULL);

int doc_dbg_register(struct device *dev)
{
	int ret = 0;

	ret = device_create_file(dev, &dev_attr_flashcontrol);
	if (!ret)
		ret = device_create_file(dev, &dev_attr_reset);
	if (!ret)
		ret = device_create_file(dev, &dev_attr_asic_mode);
	if (!ret)
		ret = device_create_file(dev, &dev_attr_device_id);
	return ret;
}

void doc_dbg_unregister(struct device *dev)
{
	device_remove_file(dev, &dev_attr_device_id);
	device_remove_file(dev, &dev_attr_asic_mode);
	device_remove_file(dev, &dev_attr_reset);
	device_remove_file(dev, &dev_attr_flashcontrol);
}
#endif

/**
 * doc_set_driver_info - Fill the mtd_info structure and docg3 structure
 * @chip_id: The chip ID of the supported chip
 * @mtd: The structure to fill
 */
static void __init doc_set_driver_info(int chip_id, struct mtd_info *mtd)
{
	struct docg3 *docg3 = mtd->priv;
	int cfg;

	cfg = doc_register_readb(docg3, DoC_Configuration);
	docg3->if_cfg = (cfg & Doc_Conf_IF_CFG ? 1 : 0);

	switch (chip_id) {
	case DOC_CHIPID_G3:
		mtd->name = "DiskOnChip G3";
		docg3->max_block = 8191;
		break;
	}
	mtd->type = MTD_NANDFLASH;
	/*
	 * Once write methods are added, the correct flags will be set.
	 * mtd->flags = MTD_CAP_NANDFLASH;
	 */
	mtd->flags = MTD_CAP_ROM;
	mtd->size = (docg3->max_block + 1) * DOC_LAYOUT_BLOCK_SIZE;
	mtd->erasesize = DOC_LAYOUT_BLOCK_SIZE;
	mtd->writesize = DOC_LAYOUT_PAGE_SIZE;
	mtd->oobsize = DOC_LAYOUT_OOB_SIZE;
	mtd->owner = THIS_MODULE;
	mtd->erase = NULL;
	mtd->point = NULL;
	mtd->unpoint = NULL;
	mtd->read = doc_read;
	mtd->write = NULL;
	mtd->read_oob = doc_read_oob;
	mtd->write_oob = NULL;
	mtd->sync = NULL;
	mtd->block_isbad = doc_block_isbad;
}

/**
 * doc_probe - Probe the IO space for a DiskOnChip G3 chip
 * @pdev: platform device
 *
 * Probes for a G3 chip at the specified IO space in the platform data
 * ressources.
 *
 * Returns 0 on success, -ENOMEM, -ENXIO on error
 */
static int __init docg3_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct docg3 *docg3;
	struct mtd_info *mtd;
	struct resource *ress;
	struct mtd_partition *parts = NULL;
	int ret, bbt_nbpages;
	u16 chip_id, chip_id_inv;

	ret = -ENOMEM;
	docg3 = kzalloc(sizeof(struct docg3), GFP_KERNEL);
	if (!docg3)
		goto nomem1;
	mtd = kzalloc(sizeof(struct mtd_info), GFP_KERNEL);
	if (!mtd)
		goto nomem2;
	mtd->priv = docg3;

	ret = -ENXIO;
	ress = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!ress) {
		dev_err(dev, "No I/O memory resource defined\n");
		goto noress;
	}
	docg3->base = ioremap(ress->start, DOC_IOSPACE_SIZE);

	docg3->dev = &pdev->dev;
	docg3->device_id = 0;
	doc_set_device_id(docg3, docg3->device_id);
	doc_set_asic_mode(docg3, Doc_AsicMode_RESET);
	doc_set_asic_mode(docg3, Doc_AsicMode_NORMAL);

	chip_id = doc_register_readw(docg3, DoC_ChipID);
	chip_id_inv = doc_register_readw(docg3, DoC_ChipID_Inv);

	ret = -ENODEV;
	if (chip_id != (u16)(~chip_id_inv)) {
		doc_info("No device found at IO addr %x\n", ress->start);
		goto nochipfound;
	}

	switch (chip_id) {
	case DOC_CHIPID_G3:
		doc_info("Found a G3 DiskOnChip at addr %x\n", ress->start);
		break;
	default:
		doc_err("Chip id %04x is not a DiskOnChip G3 chip\n", chip_id);
		goto nochipfound;
	}

	doc_set_driver_info(chip_id, mtd);
	platform_set_drvdata(pdev, mtd);

	bbt_nbpages = (docg3->max_block + 1) / (8 * DOC_LAYOUT_PAGE_SIZE);
	docg3->bbt = kzalloc(bbt_nbpages * DOC_LAYOUT_PAGE_SIZE, GFP_KERNEL);
	if (!docg3->bbt)
		goto nochipfound;
	doc_reload_bbt(docg3);

	ret = add_mtd_device(mtd);
	if (!ret && mtd_has_partitions())
		ret = parse_mtd_partitions(mtd, part_probes, &parts, 0);
	if (ret > 0)
		ret = add_mtd_partitions(mtd, parts, ret);

#ifdef DEBUG
	doc_dbg_register(docg3->dev);
#endif
	return 0;

nochipfound:
	iounmap(docg3->base);
noress:
	kfree(mtd);
nomem2:
	kfree(docg3);
nomem1:
	return ret;
}

/**
 * docg3_release - Release the driver
 * @pdev: the platform device
 *
 * Returns 0
 */
static int __exit docg3_release(struct platform_device *pdev)
{
	struct mtd_info *mtd = platform_get_drvdata(pdev);
	struct docg3 *docg3 = mtd->priv;
#ifdef DEBUG
	doc_dbg_unregister(docg3->dev);
#endif
	if (mtd_has_partitions())
		del_mtd_partitions(mtd);
	iounmap(docg3->base);
	kfree(docg3->bbt);
	kfree(docg3);
	del_mtd_device(mtd);
	kfree(mtd);
	return 0;
}

static struct platform_driver g3_driver = {
	.driver		= {
		.name	= "docg3",
		.owner	= THIS_MODULE,
	},
	.remove		= __exit_p(docg3_release),
};

static int __init docg3_init(void)
{
	return platform_driver_probe(&g3_driver, docg3_probe);
}
module_init(docg3_init);


static void __exit docg3_exit(void)
{
	platform_driver_unregister(&g3_driver);
}
module_exit(docg3_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Robert Jarzmik <robert.jarzmik at free.fr>");
MODULE_DESCRIPTION("MTD driver for DiskOnChip G3");
