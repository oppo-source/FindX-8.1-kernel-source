#ifndef __MX6300_SPI_H
#define __MX6300_SPI_H

#include <linux/types.h>
#include <media/cam_sensor.h>
#define MX6300_CMD_SIZE             4
#define MX6300_ID_SIZE              2
#define MX6300_BUFFER_MAX           4096*512
#define MX6300_CMD_DEVICE_ID        0X6300
#define MX6300_CMD_READ_ID          0X90
#define MX6300_CMD_READ_BYTES       0X0B
#define MX6300_CMD_WRITE_ENABLE     0X06
#define MX6300_CMD_WRITE_DISABLE    0X04
#define MX6300_CMD_WRITE_BYTES      0X02
#define MX6300_CMD_CHECK_BUSY       0X05
#define MX6300_CMD_ERASE_4K         0X20
#define MX6300_CMD_ERASE_32K         0X52
#define MX6300_CMD_ERASE_64K         0XD8
#define MX6300_MAX_WAIT_COUNT      100000

#define MX6300_FLASH_ADDR  0X4000
#define MX6300_FLASH_BLOCK_SIZE 2048//256 // max write len per 1 times

#define SIZE_4K 10*1024
#define SIZE_32K 32*1024
#define SIZE_64K 64*1024

#define MX6300_SPI_SPEED  10*1000*1000
#define MX6300_SPI_WR_UDELAY  1

enum {
    MX_MSG_INIT,
    MX_MSG_START,
    MX_MSG_STOP,
    MX_MSG_CONFIG,
    MX_MSG_PROBE,
    MX_MSG_MAX
}MX_MSG_TYPE;

#ifndef OPPO_MX6300_TEE_SUPPORT
/* oujinrong@BSP.Fingerprint.Basic 2018/04/17, add to disable REE SPI of mx6300 */
struct mx6300_dev {
    bool        isPowerOn;
	int           IRAVDD_gpio;
    int           IRDVDD_gpio;
    int           IRPWDN_gpio;
	int           MX_VDDIO;
	int           MX_DVDD1;
    //int           LDMP_gpio;
    int           debug_gpio;
    int           vio1p8_gpio;
    int           vio1v8_gpio;
    int           vdd0v9_gpio;
    int           vdd1_0v9_gpio;
    int           wake_gpio;
    int           reset_gpio;
    int           device_id;
    dev_t      devt;
    struct spi_device   *spi;
    struct list_head    device_entry;
    struct mutex buf_lock;
    struct mutex rw_lock;
};
int cam_sensor_ctl_mxmodule(int cmd, int parm);
void mx6x_stop_stream(void);
void mx6x_ir_stream(void);
void mx6x_depth_stream(void);
int mx6300_parse_dts(struct mx6300_dev* mx6300_dev);
void mx6300_cleanup(struct mx6300_dev* mx6300_dev);
int mx6300_config_single_vreg(struct device *dev,struct regulator *reg_ptr, int config);
int mx6300_power_on(struct mx6300_dev* mx6300_dev);
int mx6300_power_off(struct mx6300_dev* mx6300_dev);
void mx6300_reset(struct mx6300_dev* mx6300_dev);
/*guohui.tan@Camera,2017/10/30  add for MX6300 SPI driver*/
int mx6300_erase_sector_4K(struct mx6300_dev * mx6300_dev,int addr);
int mx6300_erase_sector_32K(struct mx6300_dev * mx6300_dev,int addr);
int mx6300_erase_sector_64K(struct mx6300_dev * mx6300_dev,int addr);
int mx6300_spi_read_block(struct mx6300_dev *mx6300_dev,int addr,int read_len,unsigned char *data);
int mx6300_spi_write_block(struct mx6300_dev *mx6300_dev,int addr,int write_len,unsigned char *data);
int mx6300_spi_read(struct mx6300_dev *mx6300_dev,int addr,int read_len,unsigned char *data);
int mx6300_spi_write(struct mx6300_dev *mx6300_dev,int addr,int write_len,unsigned char *data);
int mx6300_erase_sector(struct mx6300_dev * mx6300_dev,int addr, int erase_len);
void mx6300_spi_write_enable(struct mx6300_dev *mx6300_dev, int enable);
int mx6300_spi_read_sr(struct mx6300_dev *mx6300_dev);
#endif /* OPPO_MX6300_TEE_SUPPORT */
#endif
