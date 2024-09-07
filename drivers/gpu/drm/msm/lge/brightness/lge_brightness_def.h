#ifndef _LGE_BACKLIGHT_DEF_H_
#define _LGE_BACKLIGHT_DEF_H_


enum lge_blmap_type {
	LGE_BLMAP_DEFAULT = 0,
	LGE_BLMAP_TYPE_MAX
};

char *lge_get_blmapname(enum lge_blmap_type type);

#endif // _LGE_BACKLIGHT_DEF_H_
