#ifndef _LGE_DDIC_OPS_HELPER_H_
#define _LGE_DDIC_OPS_HELPER_H_

extern struct dsi_cmd_desc* find_cmd(struct dsi_cmd_desc *cmds, int cmds_count, int addr);
extern struct dsi_cmd_desc* find_nth_cmd(struct dsi_cmd_desc *cmds, int cmds_count, int addr, int nth);

#define WORD_UPPER_BYTE(w) (((w)>>8)&0xff)
#define WORD_LOWER_BYTE(w) ((w)&0xff)
#endif // _LGE_DDIC_OPS_HELPER_H_
