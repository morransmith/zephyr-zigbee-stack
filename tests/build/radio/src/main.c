/* SPDX-License-Identifier: Apache-2.0 */

#include <zzb/zzb.h>

int main(void)
{
	const struct zzb_node_config cfg = {0};
	struct zzb_node *node;

	/* Keeps the EUI-64 sources in the image, so their linking is checked */
	return zzb_node_init(&cfg, &node);
}
