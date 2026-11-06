/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
#ifndef FM1_VERSION_H
#define FM1_VERSION_H

/* DX747 for the SMK-37 is its own firmware, like DX747 for the FM-1: its own update identity, in the shape smk37_open's SDK requires
 * ("SMK-37 Pro_8" or "SMK-37 Elite_8" and six digits, 27 bytes with the padding). The six digits are DX747's own (747747), so no release of
 * M-VAVE's SMK37 Open (SMK-37 Pro_8000001, ...) shares them, and the Firmware Hub's check can tell the firmwares apart. M-VAVE's own
 * Build.ps1 for this SDK also demands that the digits are 8000000 plus the release number; DX747's build (tools/docker, tools/portable)
 * does not: the Windows route of that script cannot build this firmware. The same constants are the identity the unit answers with
 * (fm1_update_identity) and the product code inside the update package. */
#define FM1_RELEASE_VERSION "V01"
#define FM1_UPDATE_PRODUCT_PRO "SMK-37 Pro_8747747"
#define FM1_UPDATE_PRODUCT_ELITE "SMK-37 Elite_8747747"

#endif
