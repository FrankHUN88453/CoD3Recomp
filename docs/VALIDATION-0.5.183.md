# 0.5.183 validation checkpoint — October 16, 2026

- Campaign and multiplayer builds: PASS using the local project toolchain.
  Existing kernel printf format warning remains.
- Weapon helper182: PASS for stale cached IDs, current ownership, secondary
  fallback, changed kits, invalid IDs and empty inventory.
- Team helper183: PASS for humans on either/both teams and twenty additions.
  All six assignment/setup/respawn call sites use the per-bot side.
- Death/render/input-isolation hook tail unchanged from182.
- Installed EXE/PDB artifacts matched build SHA256; VERSION is0.5.183.
- Startup182 reached the multiplayer menu. Startup/live183 not yet confirmed.
- User confirmed Kangaroo exits, sensitivity slider round trip, and current
  saved-position return controls in earlier testing.

Pending: paired-client team/body visibility, held/dropped gun visibility,
combat crash reproduction, unwanted movement, bot UAV markers and artillery.
No claim these are fixed. Raw runtime/build logs and dumps stay local under
`docs/build-logs`; this summary accompanies the source-only GitHub update.
