#ifndef _H_LGE_DP_
#define _H_LGE_DP_

struct dp_notify_dev {
	const char 		*name;
	struct device 	*dev;
	int 			state;
};

int dp_notify_register(struct dp_notify_dev *ndev);
void dp_notify_unregister(struct dp_notify_dev *ndev);
void dp_notify_set_state(struct dp_notify_dev *ndev, int state);
#endif // _H_LGE_DP_
