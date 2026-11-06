/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
#ifndef DX747_TARGET_H
#define DX747_TARGET_H
/* The device DX747 is built for. The FM-1 is the default; the SMK-37 targets (targets/smk37, build.sh --target smk37-pro or
 * smk37-elite) define SMK37_MODEL_PRO or SMK37_MODEL_ELITE on the compiler line, as M-VAVE's smk37_open does. Code that exists for the
 * SMK-37 only (keys with velocity, pads, faders, wheels) is under DX_SMK37, so the FM-1 build is byte for byte what it was. */
#if defined(SMK37_MODEL_PRO) || defined(SMK37_MODEL_ELITE)
#define DX_SMK37 1
#endif
#endif
