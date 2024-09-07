#define pr_fmt(fmt)	"[Display][DP:%s:%d] " fmt, __func__, __LINE__

#include <linux/module.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/err.h>
#include "lge_dp_notifier.h"

struct class *dp_notify_class;

static ssize_t state_show(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct dp_notify_dev *ndev = (struct dp_notify_dev *)
		dev_get_drvdata(dev);

	return sprintf(buf, "%d\n", ndev->state);
}

/* this node should be used for self test purpose only
static ssize_t state_write(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	ssize_t ret = strnlen(buf, PAGE_SIZE);
	int cmd;
	struct dp_notify_dev *ndev = (struct dp_notify_dev *)
		dev_get_drvdata(dev);

	sscanf(buf, "%d", &cmd);
	dp_notify_set_state(ndev, cmd);
	pr_info("test dp notify state = %d\n", cmd);
	return ret;
}
static DEVICE_ATTR(state, S_IRUGO|S_IWUSR, state_show, state_write);
*/
static DEVICE_ATTR(state, S_IRUGO, state_show, NULL);

void dp_notify_set_state(struct dp_notify_dev *ndev, int state)
{
	char *envp[2];
	char name_buf[30];

	if (ndev->state != state) {
		ndev->state = state;
		snprintf(name_buf, sizeof(name_buf), "SWITCH_NAME=%s%c", ndev->name, ndev->state?'1':'0');
		envp[0] = name_buf;
		envp[1] = NULL;
		kobject_uevent_env(&ndev->dev->kobj, KOBJ_CHANGE, envp);
		pr_info("check_dp_notify,name_buf = %s state = %d,\n", name_buf, ndev->state);
	}
}

static int create_dp_notify_class(void)
{
	if (!dp_notify_class) {
		dp_notify_class = class_create(THIS_MODULE, "dp_notify");
		if (IS_ERR(dp_notify_class))
			return PTR_ERR(dp_notify_class);
	}

	return 0;
}

int dp_notify_register(struct dp_notify_dev *ndev)
{
	int ret;

	if (!ndev) {
		pr_err("%s : dp register failed\n", __func__);
		return -EINVAL;
	}

	ndev->dev = device_create(dp_notify_class, NULL,
			MKDEV(0, 1), NULL, ndev->name);
	if (IS_ERR(ndev->dev))
		return PTR_ERR(ndev->dev);

	ret = device_create_file(ndev->dev, &dev_attr_state);
	if (ret < 0)
		goto err1;

	dev_set_drvdata(ndev->dev, ndev);
	ndev->state = 0;
	return 0;

err1:
	device_remove_file(ndev->dev, &dev_attr_state);
	return ret;
}

void dp_notify_unregister(struct dp_notify_dev *ndev)
{
	device_remove_file(ndev->dev, &dev_attr_state);
	dev_set_drvdata(ndev->dev, NULL);
	device_destroy(dp_notify_class, MKDEV(0, 1));
}

static int __init dp_notify_class_init(void)
{
	return create_dp_notify_class();
}

static void __exit dp_notify_class_exit(void)
{
	class_destroy(dp_notify_class);
}

module_init(dp_notify_class_init);
module_exit(dp_notify_class_exit);
