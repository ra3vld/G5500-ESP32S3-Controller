// G5500 Fork; original by UA1CFM, modifications by RA3VLD. GPL-3.0.
#pragma once

// Arduino IDE: set this to 0 for a controller without TFT and touch.
// Build flags may override this default with -DTFT_DISPLAY_ENABLED=0.
#ifndef TFT_DISPLAY_ENABLED
#define TFT_DISPLAY_ENABLED 0
#endif

#if TFT_DISPLAY_ENABLED != 0 && TFT_DISPLAY_ENABLED != 1
#error "TFT_DISPLAY_ENABLED must be 0 or 1"
#endif
