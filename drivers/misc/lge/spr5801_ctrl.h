#ifndef SPR5801_CTRL_H
#define SPR5801_CTRL_H

#define GTI_DEBUG_SYSFS 1
#define GTI_I2C_DUMP    0
#define DMA_BUF_SIZE  (16 * 1024 + 2)

struct gti_device {
	struct i2c_client *client ;
	struct work_struct fw_work ;
	struct completion	complete;
	dma_addr_t dma_addr;
	struct regulator *smps_1v8 ;
	struct regulator *smps_0v9 ;
	char *fw_buf ;
	int reset ;
	int prog ;
	int ldo_0v9 ;
	int ldo_1v8 ;
	int ldo_3v3 ;
	int i2c_en ;

	int vid ;
	int pid ;
	int fwver ;
	int dlver ;
	int fwstat;
	int stat ;
	int hw_rev ;
	int i2c_hiz ;
#ifdef GTI_DEBUG_SYSFS
	u16 mem_addr;
	u8 mem_len;
#endif
	char fw_name[64];
} ;

enum {
	PWR_OFF,
	INIT,
	CHECK,
	READY,
	FW_UP,
	UNKNOWN,
};

static const char *stat_str[] = {
	"PWR_OFF",
	"INIT",
	"CHECK",
	"READY",
	"FW_UP",
	"UNKNOWN",
};

enum {
	RUN_MODE, 
	PROG_MODE,
};

#define SPR5801_VID 0x0000300C
#define SPR5801_PID 0x58010000

#define SPR5801_FW_VER 0x0F
#define SPR5801_INIT_REV 1
#define SPR5801_I2C_PSEUDO 1

#define FWU_TIMEOUT msecs_to_jiffies(3000)

int gti_power_on(struct gti_device *gdev);
int gti_power_off(struct gti_device *gdev);
int gti_pon_fwdmode(struct gti_device *gdev);
int gti_reset_runmode(struct gti_device *gdev);
int gti_reset_prgmode(struct gti_device *gdev);
int gti_fwu(struct i2c_client *client);
#endif
