/*
 * debug.h
 *
 * Provides a set of macros that write debugging data to the Serial port iff the
 * DEBUG macro is defined.
 */

#ifndef DEBUG_H

#define DEBUG_H

#ifdef DEBUG

#define DBGbegin(baud) Serial.begin((baud))
#define DBGprint(s) Serial.print((s))
#define DBGprintfmt(s, fmt)  Serial.print((s), (fmt))
#define DBGprintln(s) Serial.println((s))
#define DBGprintlnfmt(s, fmt)  Serial.println((s), (fmt))
#define DBGblankln() Serial.println()

#else

#define DBGbegin(baud)
#define DBGprint(s)
#define DBGprintfmt(s, p)
#define DBGprintln(s)
#define DBGprintlnfmt(s, p)
#define DBGblankln()

#endif

#endif
