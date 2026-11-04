/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
#ifndef FM1_VERSION_H
#define FM1_VERSION_H

/* DX747 is its own firmware, not a build of M-VAVE's Open V14, so it has its own release label and its own
 * update identity. The SDK fixes the formats: the label is "V" and two or three digits (V01 to V999, Build.ps1,
 * Build-Web.ps1); the identity is "FM-1_8" and six digits (Build.ps1, Package.cs and the workbench page's
 * openIdentity() all check that shape, and the page treats any such identity as an Open-compatible build, so
 * an update from or to M-VAVE's Open V14 stays a normal update). M-VAVE's own Open releases use
 * FM-1_8 followed by the release number padded to six digits (V14 is FM-1_8000014), so DX747's six digits are
 * above 999 and cannot collide with a future release. The same constant is the 27-byte identity the unit
 * answers with (fm1_update_identity) and the product code inside the update package. */
#define FM1_RELEASE_VERSION "V01"
#define FM1_UPDATE_PRODUCT "FM-1_8747747"

#endif
