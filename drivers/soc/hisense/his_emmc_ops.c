#include <linux/buffer_head.h>
#include <linux/slab.h>

#define HIS_READ_LENGTH						(1*PAGE_SIZE)
#define BLOCK_PATH							"/dev/block/bootdevice/by-name/"

#define HIS_MMC_OPS_STR						"<HIS-EMMC-OPS> "
#define HIS_EMMC_OPS_ERROR(fmt,arg...)		pr_err(HIS_MMC_OPS_STR fmt,##arg)
#define HIS_EMMC_OPS_INFO(fmt,arg...)		pr_info(HIS_MMC_OPS_STR fmt,##arg)
#define HIS_EMMC_OPS_DBG(fmt,arg...)        pr_debug(HIS_MMC_OPS_STR fmt,##arg)

static struct block_device* emmc_partition_open(const char *name)
{
	struct block_device *bdev;
	char path[512];

	sprintf(path, "%s%s", BLOCK_PATH, name);
	bdev = blkdev_get_by_path(path, FMODE_WRITE|FMODE_READ, NULL);
	if (IS_ERR(bdev)) {
		HIS_EMMC_OPS_ERROR("open block device %s failed!\n", path);
		return NULL;
	}

	HIS_EMMC_OPS_DBG("%s bdev %p\n", path, bdev);
	HIS_EMMC_OPS_DBG("open patrition succeed.\n");

	return bdev;
}

static void emmc_partition_close( struct block_device *bdev)
{
	blkdev_put(bdev, FMODE_READ|FMODE_WRITE);
}

static int emmc_partition_read(struct block_device *bdev, 
	unsigned int where, char *pBuffer, unsigned int size)
{
	struct buffer_head *bh = NULL;
	unsigned int bytesOfRead = 0;
	unsigned int numOfPages = 0;

	if (where%PAGE_SIZE != 0) {
		HIS_EMMC_OPS_ERROR("%s(): where is not right!\n", __func__);
		return -1;
	}

	numOfPages = (size/PAGE_SIZE) + ((size%PAGE_SIZE)? 1 : 0);

	while (bytesOfRead < size) {
		bh = __bread(bdev, (where+bytesOfRead)/PAGE_SIZE, PAGE_SIZE);
		memcpy(pBuffer+bytesOfRead, bh->b_data, PAGE_SIZE);
		bytesOfRead += PAGE_SIZE;
	}

	brelse(bh);

	return bytesOfRead;
}

static int emmc_partition_write(struct block_device *bdev, unsigned int where, char *pBuffer, unsigned int size)
{
	struct buffer_head *bh;
	unsigned int pagesOfWritten = 0;
	unsigned int numOfPages = 0;
	unsigned int pageOfStart = 0;

	if (where%PAGE_SIZE != 0) {
		HIS_EMMC_OPS_ERROR("%s(): where is not right!\n", __func__);
		return -1;
	}

	numOfPages = (size/PAGE_SIZE) + ((size%PAGE_SIZE)? 1 : 0);

	pageOfStart = where/PAGE_SIZE;
	while (pagesOfWritten < numOfPages) {
		bh = __getblk(bdev, pageOfStart+pagesOfWritten, PAGE_SIZE);
		set_buffer_uptodate(bh);
		memcpy(bh->b_data, pBuffer+pagesOfWritten*PAGE_SIZE, PAGE_SIZE);
		mark_buffer_dirty(bh);
		ll_rw_block(WRITE, 1, &bh);
		brelse(bh);
		pagesOfWritten++;
	}

	return ((pagesOfWritten+1)*PAGE_SIZE);
}

/* his_emmc_read	: read data from emmc
 * @name	: [IN] emmc partition name, eg flag, aboot
 * @offset	: [IN] offset in partition
 * @buf		: [OUT] read data buffer
 * @size	: [IN] read data size
 */
int his_emmc_read(const char *name, unsigned int offset, 
	void *buf, unsigned int size)
{
	char *pBuffer;
	struct block_device *bdev;
	int result = -1;
	unsigned int start = 0;
	unsigned int align = 0;
	unsigned int len = 0;
	unsigned int copied = 0;

	HIS_EMMC_OPS_DBG("enter\n");
	bdev = emmc_partition_open(name);
	if (!bdev) {   
		HIS_EMMC_OPS_ERROR("open aboot partition failed!\n");
		return (-1);
	}

	if (size > HIS_READ_LENGTH) {
		HIS_EMMC_OPS_ERROR("size %d too large!\n", size);
		return -1;
	}

	pBuffer = kmalloc(HIS_READ_LENGTH, GFP_KERNEL);
	memset(pBuffer, 0, HIS_READ_LENGTH);

	start = offset & (~(PAGE_SIZE - 1));
	align = offset % PAGE_SIZE;

	while (copied < size) {
		result = emmc_partition_read(bdev, start, pBuffer, HIS_READ_LENGTH);
		if (result < 0) {
			HIS_EMMC_OPS_ERROR("read aboot partition failed!\n");
			kfree(pBuffer);
			emmc_partition_close(bdev);
			return (-1);
		}

		len = ((HIS_READ_LENGTH - align) >= (size - copied)) ? 
			(size - copied) : (HIS_READ_LENGTH - align);
		memcpy(buf + copied, pBuffer + align, len);
		copied += len;
		start += HIS_READ_LENGTH;
		align = 0;
	}

	emmc_partition_close(bdev);
	kfree(pBuffer);

	return size;
}
EXPORT_SYMBOL(his_emmc_read);

/* his_emmc_write	: write data to emmc
 * @name	: [IN] emmc partition name, eg flag, aboot
 * @offset	: [IN] offset in partition
 * @buf		: [IN] write data buffer
 * @size	: [IN] write data size
 */
int his_emmc_write(const char *name, unsigned int offset, 
	unsigned int size, void *buf)
{
	int result = -1;
	char *pBuffer;
	struct block_device *bdev;
	unsigned int start = 0;
	unsigned int align = 0;
	unsigned int len = 0;
	unsigned int copied = 0;

	bdev = emmc_partition_open(name);
	if (!bdev) {	
		HIS_EMMC_OPS_ERROR("open %s partition failed!\n", name);
		return (-1);
	}  

	pBuffer = kmalloc(HIS_READ_LENGTH, GFP_KERNEL);
	if (NULL == pBuffer) {
		HIS_EMMC_OPS_ERROR("kmalloc failed!\n");
		return -1;
	}

	memset(pBuffer, 0, HIS_READ_LENGTH);

	start = offset & (~(PAGE_SIZE - 1));
	align = offset % PAGE_SIZE;

	while (copied < size) {
		result = emmc_partition_read(bdev, start, pBuffer, HIS_READ_LENGTH);
		if (result < 0) {
			HIS_EMMC_OPS_ERROR("read %s partition1 failed!\n", name);
			kfree(pBuffer);
			emmc_partition_close(bdev);
			return (-1);
		}

		len = ((HIS_READ_LENGTH - align) >= (size - copied)) ? 
			(size - copied) : (HIS_READ_LENGTH - align);
		memcpy(pBuffer + align, buf + copied, len);
		result = emmc_partition_write(bdev, start, pBuffer, HIS_READ_LENGTH);
		if(result < 0) {
			HIS_EMMC_OPS_ERROR("write aboot partition1 failed\n");
		}

		copied += len;
		start += HIS_READ_LENGTH;
		align = 0;
	}

	emmc_partition_close(bdev);
	kfree(pBuffer);

	return result;
}
EXPORT_SYMBOL(his_emmc_write);

