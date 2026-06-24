/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * <math.h> — CaraOS userland libm (Phase T.4.3). Own implementations of the
 * transcendentals a real V36+ C app needs (the amiCalc port references
 * sin/cos/tan/asin/acos/atan/exp/log/pow/sqrt). No third-party libm is
 * linked (docs/PRINCIPLES.md §2). Implemented in src/userland/libc/math.c,
 * built as the cara_libm static library. docs/PORTS.md §3 (T.4.3).
 */

#ifndef _MATH_H
#define _MATH_H

#define M_PI 3.14159265358979323846
#define M_E 2.71828182845904523536

double sin(double x);
double cos(double x);
double tan(double x);
double asin(double x);
double acos(double x);
double atan(double x);
double exp(double x);
double log(double x);
double pow(double x, double y);
double sqrt(double x);
double fabs(double x);

#endif /* _MATH_H */
