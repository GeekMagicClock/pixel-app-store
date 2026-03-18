#pragma once

// Avoid and QSPI pins

#ifndef R1_PIN_DEFAULT
#define R1_PIN_DEFAULT 4
#endif
#ifndef G1_PIN_DEFAULT
#define G1_PIN_DEFAULT 5
#endif
#ifndef B1_PIN_DEFAULT
#define B1_PIN_DEFAULT 6
#endif
#ifndef R2_PIN_DEFAULT
#define R2_PIN_DEFAULT 7
#endif
#ifndef G2_PIN_DEFAULT
#define G2_PIN_DEFAULT 15
#endif
#ifndef B2_PIN_DEFAULT
#define B2_PIN_DEFAULT 16
#endif
#ifndef A_PIN_DEFAULT
#define A_PIN_DEFAULT 18
#endif
#ifndef B_PIN_DEFAULT
#define B_PIN_DEFAULT 8
#endif
#ifndef C_PIN_DEFAULT
#define C_PIN_DEFAULT 3
#endif
#ifndef D_PIN_DEFAULT
#define D_PIN_DEFAULT 42
#endif
#ifndef E_PIN_DEFAULT
#define E_PIN_DEFAULT -1 // required for 1/32 scan panels, like 64x64. Any available pin would do, i.e. IO32
#endif
#ifndef LAT_PIN_DEFAULT
#define LAT_PIN_DEFAULT 40
#endif
#ifndef OE_PIN_DEFAULT
#define OE_PIN_DEFAULT 2
#endif
#ifndef CLK_PIN_DEFAULT
#define CLK_PIN_DEFAULT 41
#endif
