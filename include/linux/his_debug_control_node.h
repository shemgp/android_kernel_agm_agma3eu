#ifndef __HIS_DEBUG_CONTROL_NODE_H__
#define __HIS_DEBUG_CONTROL_NODE_H__

#ifdef CONFIG_HISENSE_DBG_CTRL_NODE

int his_register_debug_control_node(const struct attribute *attr);

#else	/* CONFIG_HISENSE_DBG_CTRL_NODE */

static inline int his_register_debug_control_node(
	const struct attribute *attr)
{
	return -1;
}

#endif	/* CONFIG_HISENSE_DBG_CTRL_NODE */

#endif	/* __HIS_DEBUG_CONTROL_NODE_H__ */
