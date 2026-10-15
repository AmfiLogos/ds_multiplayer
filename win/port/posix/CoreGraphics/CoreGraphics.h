// CoreGraphics stand-in (Windows build): the geometry types used by mpmenu.c's Mac-only test input.
#pragma once
typedef double CGFloat;
typedef struct { CGFloat x, y; } CGPoint;
typedef struct { CGFloat width, height; } CGSize;
typedef struct { CGPoint origin; CGSize size; } CGRect;
static const CGPoint CGPointZero = { 0, 0 };
