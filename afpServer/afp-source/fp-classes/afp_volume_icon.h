#pragma once

#include <cstdint>

/*
 * afp_volume_icon
 *
 * A 32x32, 1-bit-per-pixel monochrome icon (128-byte bitmap + 128-byte
 * transparency mask = 256 bytes total) served in the "VolumeIconAndMask"
 * field of the FPGetSrvrInfo response, so classic Mac OS clients show
 * our icon for the server instead of the generic network-server icon.
 *
 * The artwork is the MacFile_For_HaikuOS project's own application icon
 * (BEOS:L:STD_ICON in afp_server.rsrc), thresholded to 1-bit; identical
 * to that project's ClassicMacIcon.cpp. Derivation rules:
 *
 *   bitmap bit = 1  if the source pixel is dark   (value < 0x80)
 *   bitmap bit = 0  if the source pixel is light  (value >= 0x80)
 *
 *   mask bit   = 1  if the source pixel is opaque (value != 0xff)
 *   mask bit   = 0  if the source pixel is transparent (value == 0xff)
 *
 * Bitmap: bit = 1 for black, 0 for white. Mask: bit = 1 for opaque.
 * Both planes are stored row-major, MSB-first, top row first - the
 * classic Mac 1-bit icon layout. 32 rows x 4 bytes = 128 bytes each.
 */

constexpr size_t volume_icon_bitmap_size = 128; // 32 rows x 4 bytes
constexpr size_t volume_icon_mask_size = 128;   // 32 rows x 4 bytes
constexpr size_t volume_icon_size = 256;        // bitmap + mask

extern const uint8_t volume_icon_bitmap[volume_icon_bitmap_size];
extern const uint8_t volume_icon_mask[volume_icon_mask_size];
