/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/irq.h>
#include <zephyr/pm/pm.h>

void pm_state_set(enum pm_state state, uint8_t substate_id)
{
}

void pm_state_exit_post_ops(enum pm_state state, uint8_t substate_id)
{
	irq_unlock(0);
}
