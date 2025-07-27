#include <zephyr/kernel.h>
#include <zephyr/device.h>

#include <zephyr/usb/usb_device.h>
#include <zephyr/usb/usbd.h>
#include <zephyr/usb/class/usbd_msc.h>

#include <zephyr/storage/flash_map.h>
#include <zephyr/storage/disk_access.h>
#include <zephyr/logging/log.h>
#include <zephyr/fs/fs.h>
#include <ff.h>

LOG_MODULE_REGISTER(flashdrive, LOG_LEVEL_INF);

#define STORAGE_PARTITION		    wifi_firmware
#define STORAGE_PARTITION_ID		FIXED_PARTITION_ID(STORAGE_PARTITION)

static FATFS fat_fs;
static struct fs_mount_t fs_mnt ={
    .type = FS_FATFS,
    .fs_data = &fat_fs, 
    .storage_dev = NULL, 
    .mnt_point = NULL, 
};

#define DISK_MOUNT_PT "/NAND:"
static const char *disk_mount_pt = DISK_MOUNT_PT;


// USBD_DEFINE_MSC_LUN(nand, "NAND", "Zephyr", "FlashDisk", "0.00");


static int setup_flash()
{
	int err = 0;
	unsigned int id;
	const struct flash_area *pfa;

	id = STORAGE_PARTITION_ID;

	err = flash_area_open(id, &pfa);
	LOG_INF("Area %u at 0x%x on %s for %u bytes",
	       id, (unsigned int)pfa->fa_off, pfa->fa_dev->name,
	       (unsigned int)pfa->fa_size);

	if (err < 0) {
		LOG_INF("Erasing flash area ... ");
		err = flash_area_flatten(pfa, 0, pfa->fa_size);
		LOG_INF("Erased %d", err);
	}

	if (err < 0) {
		flash_area_close(pfa);
	}
	return err;
}

static int flashdrive_init(void)
{
	int err;
	struct fs_dir_t dir;

	fs_dir_t_init(&dir);
    err = setup_flash();
    if (err < 0) {
        LOG_ERR("Failed to setup flash area");
        return -1;
    }
    fs_mnt.storage_dev = (void *)STORAGE_PARTITION_ID;
	fs_mnt.mnt_point = disk_mount_pt;
	err = fs_mount(&fs_mnt);

	return 0;
}

SYS_INIT(flashdrive_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);