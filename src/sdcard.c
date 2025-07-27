#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/storage/disk_access.h>
#include <zephyr/logging/log.h>
#include <zephyr/fs/fs.h>
#include <ff.h>

LOG_MODULE_REGISTER(sdcard, LOG_LEVEL_INF);

#define DISK_DRIVE_NAME "SD"
static FATFS fat_fs;
/* mounting info */
static struct fs_mount_t mp = {
	.type = FS_FATFS,
	.fs_data = &fat_fs,
};
#define DISK_MOUNT_PT "/SD:"
static const char *disk_mount_pt = DISK_MOUNT_PT;

static int sdcard_init(void)
{
    static const char *disk_pdrv = DISK_DRIVE_NAME;
    uint64_t memory_size_mb;
    uint32_t block_count;
    uint32_t block_size;

    LOG_INF("Initializing SD card...");


    if (disk_access_ioctl(disk_pdrv, DISK_IOCTL_CTRL_INIT, NULL) != 0) {
        LOG_ERR("Storage init ERROR!");
        return -1;
    }

    if (disk_access_ioctl(disk_pdrv, DISK_IOCTL_GET_SECTOR_COUNT, &block_count)) {
        LOG_ERR("Unable to get sector count");
        return -1;
    }
    // LOG_INF("Block count %u", block_count);

    if (disk_access_ioctl(disk_pdrv, DISK_IOCTL_GET_SECTOR_SIZE, &block_size)) {
        LOG_ERR("Unable to get sector size");
        return -1;
    }
    // LOG_INF("Sector size %u", block_size);

    memory_size_mb = (uint64_t)block_count * block_size;
    LOG_INF("Memory Size(MB) %u", (uint32_t)(memory_size_mb >> 20));

    if (disk_access_ioctl(disk_pdrv, DISK_IOCTL_CTRL_DEINIT, NULL) != 0) {
        LOG_ERR("Storage deinit ERROR!");
        return -1;

    }
	mp.mnt_point = disk_mount_pt;

	int err = fs_mount(&mp);

	if (err == FR_OK) {
		LOG_INF("Disk mounted.");
    } else {
        LOG_ERR("Disk mount failed: %d", err);
    }

	return 0;
}

SYS_INIT(sdcard_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);


