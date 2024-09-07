#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/sysfs.h>
#include <linux/of_gpio.h>
#include <linux/stat.h>
#include <linux/firmware.h>
#include <linux/i2c.h>
#include <linux/device.h>
#include <linux/regulator/consumer.h>

#include "spr5801_ctrl.h"
#include "spr5801_util.h"
#include "spr5801_fwu.h"

#ifdef GTI_I2C_DUMP
char gti_dump_buf[128] ;
static void dump_buf(const char *fn, const char *buf, int count)
{
#define CHARS_PER_LINE (16)

	int i,j;
	int line_char = CHARS_PER_LINE ;

	int line_num = count / line_char ;
	int mod_num = count % line_char ;
	char *p_ptr, *d_ptr ;

	if(line_num > 4){
		line_num = 4 ;
		mod_num = 0 ;
	}

	d_ptr = (char *)buf;
	for(i=0;i<line_num;i++){
		p_ptr = gti_dump_buf ;
		for(j=0;j<line_char;j++){
			sprintf(p_ptr , "%02x ", *d_ptr++);
			p_ptr += 3 ;
		}
		printk("%s: %s \n", fn, gti_dump_buf) ;
	}

	if(mod_num){
		p_ptr = gti_dump_buf ;
		for(j=0;j<mod_num;j++){
			sprintf(p_ptr , "%02x ", *d_ptr++);
			p_ptr += 3 ;
		}
		printk("%s: %s \n", fn, gti_dump_buf) ;
	}
}
#endif

int gti_i2c_send(struct gti_device *dev, const char *buf, int count)
{
	int num_write ;
	num_write = i2c_master_send(dev->client, buf, count);
#ifdef GTI_I2C_DUMP
	if(num_write > 0) dump_buf(__func__, buf, num_write) ;
#endif
	return num_write;
}

int gti_i2c_recv(struct gti_device *dev, char *buf, int count)
{
	int num_read ;
	num_read = i2c_master_recv(dev->client, buf, count);
#ifdef GTI_I2C_DUMP
	if(num_read > 0) dump_buf(__func__, buf, num_read) ;
#endif
	return num_read ;
}

static int gti_memory_read(struct gti_device *dev, u16 addr, char *buf, u8 len)
{
	int num_read;
	int num_write;
	char cmd_buf[4] = { CMD_MEMR, addr >> 8, addr & 0xff, len };

	num_write = gti_i2c_send(dev, cmd_buf, 4);
	if (num_write < 0) {
		pr_err("%s: i2c error on cmd memory read\n", __func__);
		return num_write;
	}

	num_read = gti_i2c_recv(dev, buf, len);
	if (num_read < 0 || num_read != len) {
		num_read = -EIO;
		pr_err("%s: i2c error on recv\n", __func__);
		return num_read;
	}
	return 0;
}

/* one byte write only */
static int gti_memory_write(struct gti_device *dev, u16 addr, const u8 data)
{
	int num_write;
	char buf[4] = { CMD_MEMW, addr >> 8, addr & 0xff, data };

	num_write = gti_i2c_send(dev, buf, 4);
	if (num_write < 0)
		pr_err("%s: i2c error on cmd memory write\n", __func__);
	return (num_write < 0) ? num_write : 0; 
}

/* sys node */
static ssize_t sysfs_pon_store(struct device *dev,
		struct device_attribute *attr, const char *buf,
		size_t count)
{
	struct gti_device *gdev = (struct gti_device *)dev_get_drvdata(dev);
	int ret, v;

	ret = kstrtoint(buf, 10, &v);
	if(ret) {
		pr_err("%s: invalid value\n", __func__);
		return ret;
	}

	if(v){
		gti_power_on(gdev) ;
		gdev->stat = READY ;
	}
	else {
		if(gdev->i2c_hiz != 1)
			gti_clr_mark(gdev) ;
		gti_power_off(gdev) ;
		gdev->stat = PWR_OFF ;
	}

	return count;
}

static ssize_t sysfs_reset_store(struct device *dev,
		struct device_attribute *attr, const char *buf,
		size_t count)
{
	struct gti_device *gdev = (struct gti_device *)dev_get_drvdata(dev);
	int ret, v;
	pr_err("%s: reset start.\n", __func__);
	ret = kstrtoint(buf, 10, &v);
	if (ret) {
		pr_err("%s: invalid value\n", __func__);
		return ret;
	}

	if(v){
		gti_power_off(gdev) ;
		gti_power_on(gdev) ;
		gdev->stat = READY ;
	}
	return count;
}

static ssize_t sysfs_stat_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct gti_device *gdev = (struct gti_device *)dev_get_drvdata(dev);

	return snprintf(buf, PAGE_SIZE, "%s \n",stat_str[gdev->stat] );
}

static ssize_t sysfs_vid_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct gti_device *gdev = (struct gti_device *)dev_get_drvdata(dev);
	int pid, vid ;
	int ret ;

	if (gdev->stat == PWR_OFF)
		return -ENODEV;

	ret = gti_get_device_id(gdev, &vid, &pid);
	if(ret >= 0){
		gdev->vid = vid;
		gdev->pid = pid;
	}
	return snprintf(buf, PAGE_SIZE, "0x%04X \n", vid);
}


static ssize_t sysfs_pid_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct gti_device *gdev = (struct gti_device *)dev_get_drvdata(dev);
	int pid, vid ;
	int ret ;

	if (gdev->stat == PWR_OFF)
		return -ENODEV;

	ret = gti_get_device_id(gdev, &vid, &pid);
	if(ret >= 0){
		gdev->vid = vid;
		gdev->pid = pid;
	}
	return snprintf(buf, PAGE_SIZE, "0x%04X \n", pid);
}


static ssize_t sysfs_fwver_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct gti_device *gdev = (struct gti_device *)dev_get_drvdata(dev);
	int fw_ver =0;
	int ret ;

	if (gdev->stat == PWR_OFF)
		return -ENODEV;

	ret = gti_get_fw_version(gdev, &fw_ver);
	if(ret >= 0){
		gdev->fwver = fw_ver;
	}
	return snprintf(buf, PAGE_SIZE, "0x%02x\n", fw_ver);
}

static ssize_t sysfs_fw_update_store(struct device *dev,
		struct device_attribute *attr, const char *buf,
		size_t count)
{
	struct gti_device *gdev = (struct gti_device *)dev_get_drvdata(dev);
	int ret, value;

	pr_err("%s: start.\n", __func__);

	if(gdev->stat == PWR_OFF)
		return -ENODEV;

	ret = kstrtoint(buf, 10, &value);
	if (ret) {
		pr_err("%s: invalid value\n", __func__);
		return ret;
	}
	if(value){
		ret = gti_update_fw(gdev);
		if(!ret)
			gdev->stat = FW_UP ;
	}

	return count;
}

static ssize_t sysfs_pon_fwu_store(struct device *dev,
		struct device_attribute *attr, const char *buf,
		size_t count)
{
	struct gti_device *gdev = (struct gti_device *)dev_get_drvdata(dev);
	int ret, value;
	int fw_ver =0;

	pr_err("%s: pon fwu start.\n", __func__);
	ret = kstrtoint(buf, 10, &value);
	if (ret) {
		pr_err("%s: invalid value\n", __func__);
		return ret;
	}
	if(value){
		pr_err("gti: sysfs power on.\n", __func__);
		if(gdev->stat != PWR_OFF)
			gti_power_off(gdev) ;

		gti_power_on(gdev) ;
		gti_get_fw_version(gdev, &fw_ver);
		ret = gti_update_fw(gdev);
		if(!ret)
			gdev->stat = FW_UP ;
		else
			gdev->stat = READY ;
	}
	else {
		pr_err("gti: sysfs power off.\n", __func__);
		if(gdev->i2c_hiz != 1)
			gti_clr_mark(gdev) ;
		gti_power_off(gdev) ;
		gdev->stat = PWR_OFF ;
	}

	return count;
}

static ssize_t sysfs_pon_fwu_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct gti_device *gdev = (struct gti_device *)dev_get_drvdata(dev);
	int ret = 0;

	if(gdev->stat == FW_UP)
		ret = 1 ;
	return snprintf(buf, PAGE_SIZE, "%d\n",ret);
}

#ifdef GTI_DEBUG_SYSFS
static ssize_t sysfs_mem_addr_store(struct device *dev,
		struct device_attribute *attr, const char *buf,
		size_t count)
{
	struct gti_device *gdev = (struct gti_device *)dev_get_drvdata(dev);
	int ret, value;

	if(gdev->stat == PWR_OFF)
		return -ENODEV;

	ret = kstrtoint(buf, 16, &value);
	if (ret) {
		pr_err("%s: invalid value\n", __func__);
		return ret;
	}
	gdev->mem_addr = value;

	return count;
}


static ssize_t sysfs_mem_len_store(struct device *dev,
		struct device_attribute *attr, const char *buf,
		size_t count)
{
	struct gti_device *gdev = (struct gti_device *)dev_get_drvdata(dev);
	int ret, value;

	if(gdev->stat == PWR_OFF)
		return -ENODEV;

	ret = kstrtoint(buf, 16, &value);
	if (ret) {
		pr_err("%s: invalid value\n", __func__);
		return ret;
	}
	gdev->mem_len = value;

	return count;
}


static ssize_t sysfs_mem_data_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct gti_device *gdev = (struct gti_device *)dev_get_drvdata(dev);
	char data[256] = {0, };
	int ret, i, offset;

	if(gdev->stat == PWR_OFF)
		return -ENODEV;

	if(gdev->mem_len == 0)
		return -ENODEV;

	ret = gti_memory_read(gdev, gdev->mem_addr, data, gdev->mem_len);
	if (ret)
		return ret;

	for (offset = 0, i = 0; i < gdev->mem_len; i++) {
		if (!(i % 16))
			offset += sprintf(buf+offset, "\n%04X :",
					gdev->mem_addr + i);
		offset += sprintf(buf+offset, " %02X", data[i]);
	}
	offset += sprintf(buf+offset, "\n");

	return offset;
}

static ssize_t sysfs_mem_data_store(struct device *dev,
		struct device_attribute *attr, const char *buf,
		size_t count)
{
	struct gti_device *gdev = (struct gti_device *)dev_get_drvdata(dev);
	int ret, value;

	if(gdev->stat == PWR_OFF)
		return -ENODEV;

	ret = kstrtoint(buf, 16, &value);
	if (ret) {
		pr_err("%s: invalid value\n", __func__);
		return ret;
	}

	ret = gti_memory_write(gdev, gdev->mem_addr, (u8)value);

	pr_info("%s: Wrote %02X at %04X\n", __func__, gdev->mem_addr, value);
	return count;
}


static ssize_t sysfs_cmd_store(struct device *dev,
		struct device_attribute *attr, const char *buf,
		size_t count)
{
	struct gti_device *gdev = (struct gti_device *)dev_get_drvdata(dev);
	int ret, value;
	const int cmd_size = 4;
	char cmd[cmd_size] = {0x0, 0x0, 0x0, 0x0};

	if(gdev->stat == PWR_OFF)
		return -ENODEV;

	ret = kstrtoint(buf, 16, &value);
	if (ret) {
		pr_err("%s: invalid value\n", __func__);
		return ret;
	}

	cmd[0] = (u8)value ;
	ret = gti_i2c_send(gdev, cmd, 4);
	if (ret < 0) {
		pr_err("%s: i2c error on cmd cmd %d \n", __func__, value);
		return ret;
	}

	return count;
}

static ssize_t sysfs_0v9_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct gti_device *gdev = (struct gti_device *)dev_get_drvdata(dev);

	return snprintf(buf, PAGE_SIZE, "%d \n", gpio_get_value(gdev->ldo_0v9));
}

static ssize_t sysfs_0v9_store(struct device *dev,
		struct device_attribute *attr, const char *buf,
		size_t count)
{
	struct gti_device *gdev = (struct gti_device *)dev_get_drvdata(dev);
	int ret, value;

	ret = kstrtoint(buf, 16, &value);
	gpio_set_value(gdev->ldo_0v9, value) ;

	return count;
}

static ssize_t sysfs_1v8_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct gti_device *gdev = (struct gti_device *)dev_get_drvdata(dev);

	return snprintf(buf, PAGE_SIZE, "%d \n", gpio_get_value(gdev->ldo_1v8));
}

static ssize_t sysfs_1v8_store(struct device *dev,
		struct device_attribute *attr, const char *buf,
		size_t count)
{
	struct gti_device *gdev = (struct gti_device *)dev_get_drvdata(dev);
	int ret, value;

	ret = kstrtoint(buf, 16, &value);
	gpio_set_value(gdev->ldo_1v8, value) ;

	return count;
}


static ssize_t sysfs_3v3_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct gti_device *gdev = (struct gti_device *)dev_get_drvdata(dev);

	return snprintf(buf, PAGE_SIZE, "%d \n", gpio_get_value(gdev->ldo_3v3));
}

static ssize_t sysfs_3v3_store(struct device *dev,
		struct device_attribute *attr, const char *buf,
		size_t count)
{
	struct gti_device *gdev = (struct gti_device *)dev_get_drvdata(dev);
	int ret, value;

	ret = kstrtoint(buf, 16, &value);
	gpio_set_value(gdev->ldo_3v3, value) ;

	return count;
}


static ssize_t sysfs_rst_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct gti_device *gdev = (struct gti_device *)dev_get_drvdata(dev);

	return snprintf(buf, PAGE_SIZE, "%d \n", gpio_get_value(gdev->reset));
}

static ssize_t sysfs_rst_store(struct device *dev,
		struct device_attribute *attr, const char *buf,
		size_t count)
{
	struct gti_device *gdev = (struct gti_device *)dev_get_drvdata(dev);
	int ret, value;

	ret = kstrtoint(buf, 16, &value);
    gpio_set_value(gdev->reset, value) ;

	return count;
}

static ssize_t sysfs_isol_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct gti_device *gdev = (struct gti_device *)dev_get_drvdata(dev);

	return snprintf(buf, PAGE_SIZE, "%d \n", gpio_get_value(gdev->i2c_en));
}

static ssize_t sysfs_isol_store(struct device *dev,
		struct device_attribute *attr, const char *buf,
		size_t count)
{
	struct gti_device *gdev = (struct gti_device *)dev_get_drvdata(dev);
	int ret, value;

	ret = kstrtoint(buf, 16, &value);
    gpio_set_value(gdev->i2c_en, value) ;

	return count;
}

static ssize_t sysfs_prg_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct gti_device *gdev = (struct gti_device *)dev_get_drvdata(dev);

	return snprintf(buf, PAGE_SIZE, "%d \n", gpio_get_value(gdev->prog));
}

static ssize_t sysfs_prg_store(struct device *dev,
		struct device_attribute *attr, const char *buf,
		size_t count)
{
	struct gti_device *gdev = (struct gti_device *)dev_get_drvdata(dev);
	int ret, value;

	ret = kstrtoint(buf, 16, &value);
    gpio_set_value(gdev->prog, value) ;

	return count;
}

static ssize_t sysfs_hw_rev_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct gti_device *gdev = (struct gti_device *)dev_get_drvdata(dev);

	return snprintf(buf, PAGE_SIZE, "%d \n", gdev->hw_rev);
}

static ssize_t sysfs_hw_rev_store(struct device *dev,
		struct device_attribute *attr, const char *buf,
		size_t count)
{
	struct gti_device *gdev = (struct gti_device *)dev_get_drvdata(dev);
	int ret, value;

	ret = kstrtoint(buf, 16, &value);
    gdev->hw_rev = value;

	return count;
}

static ssize_t sysfs_hiz_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct gti_device *gdev = (struct gti_device *)dev_get_drvdata(dev);

	return snprintf(buf, PAGE_SIZE, "%d \n", gdev->i2c_hiz);
}

static ssize_t sysfs_hiz_store(struct device *dev,
		struct device_attribute *attr, const char *buf,
		size_t count)
{
	struct gti_device *gdev = (struct gti_device *)dev_get_drvdata(dev);
	int ret, value;

	ret = kstrtoint(buf, 16, &value);
    gdev->i2c_hiz = value;

	return count;
}

static ssize_t sysfs_fw_name_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct gti_device *gdev = (struct gti_device *)dev_get_drvdata(dev);
	pr_err("%s: firmware name %s size %d \n", __func__, gdev->fw_name, strlen(gdev->fw_name));
	return snprintf(buf, PAGE_SIZE, "%s \n", gdev->fw_name);
}

static ssize_t sysfs_fw_name_store(struct device *dev,
		struct device_attribute *attr, const char *buf,
		size_t count)
{
	struct gti_device *gdev = (struct gti_device *)dev_get_drvdata(dev);

	int crlf ;
	if(sizeof(gdev->fw_name) < strlen(buf)){
		pr_err("%s: fw_name buf size %d, fw_name %d \n", __func__,sizeof(gdev->fw_name),strlen(buf)  ) ;
		return count ;
	}

	crlf = strlen(buf) ;
	if(crlf < strlen("gti_firmware" )){
		return count ;
	}
	else {
		strcpy(gdev->fw_name, buf) ;
		gdev->fw_name[crlf - 1] = 0 ;
		gti_fw_exist(gdev->client) ;
	}
	return count;
}

static ssize_t sysfs_mark_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct gti_device *gdev = (struct gti_device *)dev_get_drvdata(dev);
	u16 mark ;
	int ret ;

	ret = gti_memory_read(gdev, 0x25F4, (char *)&mark, 2);
	if(ret < 0)
		pr_err("%s: mark read fail \n", __func__) ;

	return snprintf(buf, PAGE_SIZE, "0x%04x \n", mark);
}

static ssize_t sysfs_mark_store(struct device *dev,
		struct device_attribute *attr, const char *buf,
		size_t count)
{
	struct gti_device *gdev = (struct gti_device *)dev_get_drvdata(dev);
	int ret, value;

	ret = kstrtoint(buf, 16, &value);

	if(value == 0){
		ret = gti_memory_write(gdev, 0x25F4, 0);
		if(ret < 0)
			pr_err("%s: mark write fail \n", __func__) ;
	}

	return count;
}

#endif

#define ATTR_RD (S_IRUSR | S_IRGRP | S_IROTH)
#define ATTR_WR (S_IWUSR | S_IWGRP)

static DEVICE_ATTR(pon, ATTR_WR, NULL, sysfs_pon_store);
static DEVICE_ATTR(pon_fwu, ATTR_WR | ATTR_RD, sysfs_pon_fwu_show, sysfs_pon_fwu_store);
static DEVICE_ATTR(reset, ATTR_WR, NULL, sysfs_reset_store);
static DEVICE_ATTR(stat, ATTR_RD, sysfs_stat_show, NULL);
static DEVICE_ATTR(vid, ATTR_RD, sysfs_vid_show, NULL);
static DEVICE_ATTR(pid, ATTR_RD, sysfs_pid_show, NULL);
static DEVICE_ATTR(fwver, ATTR_RD, sysfs_fwver_show, NULL);
static DEVICE_ATTR(fw_update, ATTR_WR, NULL, sysfs_fw_update_store);

#if GTI_DEBUG_SYSFS
static DEVICE_ATTR(mem_add, ATTR_WR, NULL, sysfs_mem_addr_store);
static DEVICE_ATTR(mem_len, ATTR_WR, NULL, sysfs_mem_len_store);
static DEVICE_ATTR(mem_dat, ATTR_WR | ATTR_RD, sysfs_mem_data_show,sysfs_mem_data_store);
static DEVICE_ATTR(cmd, ATTR_WR, NULL, sysfs_cmd_store);

static DEVICE_ATTR(0v9, ATTR_WR | ATTR_RD, sysfs_0v9_show,sysfs_0v9_store);
static DEVICE_ATTR(1v8, ATTR_WR | ATTR_RD, sysfs_1v8_show,sysfs_1v8_store);
static DEVICE_ATTR(3v3, ATTR_WR | ATTR_RD, sysfs_3v3_show,sysfs_3v3_store);
static DEVICE_ATTR(rst, ATTR_WR | ATTR_RD, sysfs_rst_show,sysfs_rst_store);
static DEVICE_ATTR(isol, ATTR_WR | ATTR_RD, sysfs_isol_show,sysfs_isol_store);
static DEVICE_ATTR(prg, ATTR_WR | ATTR_RD, sysfs_prg_show,sysfs_prg_store);
static DEVICE_ATTR(hw_rev, ATTR_WR | ATTR_RD, sysfs_hw_rev_show,sysfs_hw_rev_store);
static DEVICE_ATTR(hiz, ATTR_WR | ATTR_RD, sysfs_hiz_show,sysfs_hiz_store);
static DEVICE_ATTR(fw_name, ATTR_WR | ATTR_RD, sysfs_fw_name_show,sysfs_fw_name_store);
static DEVICE_ATTR(mark, ATTR_WR | ATTR_RD, sysfs_mark_show,sysfs_mark_store);
#endif

static struct attribute *gti_dev_attrs[] = {
	&dev_attr_pon.attr,
	&dev_attr_pon_fwu.attr,
	&dev_attr_reset.attr,
	&dev_attr_stat.attr,
	&dev_attr_vid.attr,
	&dev_attr_pid.attr,
	&dev_attr_fwver.attr,
	&dev_attr_fw_update.attr,
#if GTI_DEBUG_SYSFS
	&dev_attr_mem_add.attr,
	&dev_attr_mem_len.attr,
	&dev_attr_mem_dat.attr,
	&dev_attr_cmd.attr,

	&dev_attr_0v9.attr,
	&dev_attr_1v8.attr,
	&dev_attr_3v3.attr,
	&dev_attr_rst.attr,
	&dev_attr_isol.attr,
	&dev_attr_prg.attr,
	&dev_attr_hw_rev.attr,
	&dev_attr_hiz.attr,
	&dev_attr_fw_name.attr,
	&dev_attr_mark.attr,
#endif
	NULL
};

static struct attribute_group gti_dev_attr_group = {
	.attrs = gti_dev_attrs,
};

int gti_sysfs_create(struct i2c_client *client)
{
	int ret ;
	ret = sysfs_create_group(&client->dev.kobj, &gti_dev_attr_group);

	return ret ;
}

int gti_sysfs_remove(struct i2c_client *client)
{
	sysfs_remove_group(&client->dev.kobj, &gti_dev_attr_group);
	return 0 ;
}

int gti_clr_mark(struct gti_device *gdev)
{
	int ret ;

	ret = gti_memory_write(gdev, 0x25F4, 0);
	if(ret < 0)
		pr_err("%s: mark write fail \n", __func__) ;

	return 0;
}

int gti_i2c_transfer(struct gti_device *dev, struct i2c_msg *i2c_msg, int num_msg)
{
	int i;
	char *buf ;
	int  len ;
	int ret ;

	ret = i2c_transfer(dev->client->adapter, i2c_msg, num_msg);
#ifdef GTI_I2C_DUMP
	if(ret > 0){
		for(i=0;i<num_msg;i++){
			buf = i2c_msg[i].buf ;
			len = i2c_msg[i].len ;
			dump_buf(__func__, buf, len) ;
		}
	}
#endif
	return ret ;
}

int gti_get_device_id(struct gti_device *dev, int *vid, int *pid)
{
	int ret;
	const int buf_size = 8;
	char buf[buf_size] = {0x0, 0x0, 0x0, 0x0, 0x0,  };

	buf[0] = (char)CMD_DEVID ;
	ret = gti_i2c_send(dev, buf, 4);
	if (ret < 0) {
		pr_err("%s: i2c error on cmd device id\n", __func__);
		return ret;
	}
	mdelay(2);
	memset(buf, 0, buf_size);
	ret = gti_i2c_recv(dev, buf, buf_size);
	if (ret < 0 || ret != buf_size) {
		ret = -EIO;
		pr_err("%s: i2c error on recv\n", __func__);
		return ret;
	}

	*vid = ((int)buf[0] << 24)| (buf[1] << 16) | (buf[2] << 8) | buf[3];
	*pid = ((int)buf[4] << 24)| (buf[5] << 16) | (buf[6] << 8) | buf[7];

	return 0;
}

int gti_get_fw_version(struct gti_device *dev, int *fw_ver)
{
	int ret;
	const int buf_size = 8;
	char buf[buf_size] = {0x0, 0x0, 0x0, 0x0, 0x0,};

	buf[0] = CMD_FWVER ;
	ret = gti_i2c_send(dev, buf, 4);
	if (ret < 0) {
		pr_err("%s: i2c error on cmd fw ver\n", __func__);
		return ret;
	}
	mdelay(2);
	ret = gti_i2c_recv(dev, buf, 1);
	if (ret < 0 || ret != 1) {
		ret = -EIO;
		pr_err("%s: i2c error on recv\n", __func__);
		return ret;
	}
	*fw_ver = (int)buf[0] ;
	return ret ;
}

int gti_get_status(struct gti_device *dev, int *chip_stat)
{
	int ret;
	const int buf_size = 4;
	char buf[buf_size] = {0x0, 0x0, 0x0, 0x0};

	pr_err("%s: gti get status\n", __func__);
	buf[0] = CMD_STATUS ;
	ret = gti_i2c_send(dev, buf, 4);
	if (ret < 0) {
		pr_err("%s: i2c error on cmd status\n", __func__);
		return ret;
	}
	mdelay(2);
	ret = gti_i2c_recv(dev, buf, 1);
	if (ret < 0 || ret != 1) {
		ret = -EIO;
		pr_err("%s: i2c error on recv\n", __func__);
		return ret;
	}
	*chip_stat = (int)buf[0] ;
	return ret ;
}

static int gti_get_gpio(struct device *dev,
		const char *dt_name, int *gpio,
		unsigned long gpio_flags, const char *label)
{
	struct device_node *node = dev->of_node;
	int ret;

	*gpio = of_get_named_gpio(node, dt_name, 0);
	if (!gpio_is_valid(*gpio)) {
		pr_err("%s: %s not in device tree\n", __func__, dt_name);
		return -EINVAL;
	}

	ret = devm_gpio_request_one(dev, *gpio, gpio_flags, label);
	if (ret < 0) {
		pr_err("%s: cannot request gpio %d (%s)\n", __func__,
				*gpio, label);
		return ret;
	}
	return 0;
}

int gti_parse_dt(struct i2c_client *client)
{
	struct gti_device *gdev = i2c_get_clientdata(client);
	struct device *dev = &client->dev;
	int ret ;
	const char *str =0;

	ret = gti_get_gpio(dev, "gti,reset-gpio", &gdev->reset, \
                         GPIOF_OUT_INIT_LOW, "reset");
	if (ret) goto err_parse	;

	ret = gti_get_gpio(dev, "gti,prog-gpio",  &gdev->prog,  \
                         GPIOF_OUT_INIT_LOW, "prog");
	if (ret) goto err_parse	;

	ret = gti_get_gpio(dev, "gti,ldo_0v9_en", &gdev->ldo_0v9, \
                         GPIOF_OUT_INIT_LOW, "0v9");
	if (ret) goto err_parse	;

	ret = gti_get_gpio(dev, "gti,ldo_1v8_en", &gdev->ldo_1v8, \
                         GPIOF_OUT_INIT_LOW, "1v8");
	if (ret) goto err_parse	;

	ret = gti_get_gpio(dev, "gti,ldo_3v3_en", &gdev->ldo_3v3, \
                         GPIOF_OUT_INIT_LOW, "3v3");
	if (ret) goto err_parse	;

	ret = gti_get_gpio(dev, "gti,i2c_en-gpio", &gdev->i2c_en, \
                         GPIOF_OUT_INIT_LOW, "i2c_en");
	if (ret) goto err_parse	;

	ret = of_property_read_u32(dev->of_node, "gti,hw_rev", &gdev->hw_rev);
	if (ret) goto err_parse	;

	of_property_read_u32(dev->of_node, "gti,i2c_hiz", &gdev->i2c_hiz);
	if (ret) goto err_parse	;

	ret = of_property_read_string(dev->of_node, "gti,fw_name", &str);
	if(ret) goto err_parse	;

	if(sizeof(gdev->fw_name) < strlen(str)){
		pr_err("%s: fw_name buf size %d, fw_name %d \n", __func__,sizeof(gdev->fw_name),strlen(str)) ;
		return -1 ;
	}

	strcpy(gdev->fw_name, str) ;

	return 0 ;
err_parse:
	return ret ;
}
