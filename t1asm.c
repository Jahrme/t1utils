/* t1asm
 *
 * This program `assembles' Adobe Type-1 font programs in pseudo-PostScript
 * form into either PFB or PFA format.  The human readable/editable input is
 * charstring- and eexec-encrypted as specified in the `Adobe Type 1 Font
 * Format' version 1.1 (the `black book').  There is a companion program,
 * t1disasm, which `disassembles' PFB and PFA files into a pseudo-PostScript
 * file.
 *
 * Copyright (c) 1992 by I. Lee Hetherington, all rights reserved.
 *
 * Permission is hereby granted to use, modify, and distribute this program
 * for any purpose provided this copyright notice and the one below remain
 * intact.
 *
 * I. Lee Hetherington (ilh@lcs.mit.edu)
 *
 * The 1.5 versions are maintained by eddietwo@lcs.mit.edu.
 *
 * $Log: t1asm.c,v $
 * Revision 1.1  1998/03/05 16:28:46  eddietwo
 * Initial revision
 *
 * Revision 1.5  eddietwo
 * These changes by Eddie Kohler (eddietwo@lcs.mit.edu) not sanctioned
 * by I. Lee Hetherington.
 *  * Added support for Type 2 commands like `blend' and `add'.
 *  * Added unknown command parsing (translates UNKNOWN_%d to %d).
 *  * Changed default output to PFB.
 *  * Removed banner, replaced getopt with CLP.
 *
 * Revision 1.4  92/07/10  10:53:09  ilh
 * Added support for additional PostScript after the closefile command
 * (ie., some fonts have {restore}if after the cleartomark).
 * 
 * Revision 1.3  92/06/23  10:58:25  ilh
 * MSDOS porting by Kai-Uwe Herbing (herbing@netmbx.netmbx.de)
 * incoporated.
 * 
 * Revision 1.2  92/05/22  11:54:45  ilh
 * Fixed bug where integers larger than 32000 could not be encoded in
 * charstrings.  Now integer range is correct for four-byte
 * twos-complement integers: -(1<<31) <= i <= (1<<31)-1.  Bug detected by
 * Piet Tutelaers (rcpt@urc.tue.nl).
 *
 * Revision 1.1  92/05/22  11:48:46  ilh
 * initial version
 *
 * Ported to Microsoft C/C++ Compiler and MS-DOS operating system by
 * Kai-Uwe Herbing (herbing@netmbx.netmbx.de) on June 12, 1992. Code
 * specific to the MS-DOS version is encapsulated with #ifdef _MSDOS
 * ... #endif, where _MSDOS is an identifier, which is automatically
 * defined, if you compile with the Microsoft C/C++ Compiler.
 *
 */

#ifndef lint
static char rcsid[] =
  "@(#) $Id: t1asm.c,v 1.1 1998/03/05 16:28:46 eddietwo Exp $";
static char copyright[] =
  "@(#) Copyright (c) 1992 by I. Lee Hetherington, all rights reserved.";
#ifdef _MSDOS
static char portnotice[] =
  "@(#) Ported to MS-DOS by Kai-Uwe Herbing (herbing@netmbx.netmbx.de).";
#endif
#endif

/* Note: this is ANSI C. */

#ifdef _MSDOS
  #include <fcntl.h>
  #include <io.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>
#include <stdarg.h>
#include "clp.h"

/* int32 must be at least 32-bit and uint16 must be at least 16-bit */
#if INT_MAX >= 0x7FFFFFFFUL
typedef int int32;
#else
typedef long int32;
#endif
#if USHRT_MAX >= 0xFFFFUL
typedef unsigned short uint16;
#else
typedef unsigned int uint16;
#endif

#define LINESIZE 512

#define MAXBLOCKLEN ((1L<<17)-6)
#define MINBLOCKLEN ((1L<<8)-6)

#define MARKER   128
#define ASCII    1
#define BINARY   2
#define DONE     3

typedef unsigned char byte;

static FILE *ifp = stdin;
static FILE *ofp = stdout;

/* flags */
static int pfb = 1;
static int active = 0;
static int start_charstring = 0;
static int in_eexec = 0;

/* need to add 1 as space for \0 */
static char line[LINESIZE + 1];

/* lenIV and charstring start command */
static int lenIV = 4;
static char cs_start[10];

/* for charstring buffering */
static byte charstring_buf[65535];
static byte *charstring_bp;

/* for PFB block buffering */
static byte blockbuf[MAXBLOCKLEN];
static int32 blocklen = MAXBLOCKLEN;
static int32 blockpos = -1;
static int blocktyp = ASCII;

/* decryption stuff */
static uint16 er, cr;
static uint16 c1 = 52845, c2 = 22719;

/* table of charstring commands */
static struct command {
  char *name;
  int one, two;
} command_table[] = {
  { "abs", 12, 9 },
  { "add", 12, 10 },
  { "and", 12, 3 },
  { "blend", 16, -1 },
  { "callgsubr", 29, -1 },
  { "callothersubr", 12, 16 },
  { "callsubr", 10, -1 },
  { "closepath", 9, -1 },
  { "cntrmask", 18, -1 },
  { "div", 12, 12 },
  { "dotsection", 12, 0 },
  { "drop", 12, 18 },
  { "dup", 12, 27 },
  { "endchar", 14, -1 },
  { "eq", 12, 15 },
  { "error", 0, -1 },
  { "exch", 12, 28 },
  { "get", 12, 21 },
  { "hhcurveto", 27, -1 },
  { "hintmask", 19, -1 },
  { "hlineto", 6, -1 },
  { "hmoveto", 22, -1 },
  { "hsbw", 13, -1 },
  { "hstem", 1, -1 },
  { "hstem3", 12, 2 },
  { "hstemhm", 18, -1 },
  { "hvcurveto", 31, -1 },
  { "ifelse", 12, 22 },
  { "index", 12, 29 },
  { "load", 12, 13 },
  { "mul", 12, 24 },
  { "neg", 12, 14 },
  { "not", 12, 5 },
  { "or", 12, 4 },
  { "pop", 12, 17 },
  { "put", 12, 20 },
  { "random", 12, 23 },
  { "rcurveline", 24, -1 },
  { "return", 11, -1 },
  { "rlinecurve", 25, -1 },
  { "rlineto", 5, -1 },
  { "rmoveto", 21, -1 },
  { "roll", 12, 30 },
  { "rrcurveto", 8, -1 },
  { "sbw", 12, 7 },
  { "seac", 12, 6 },
  { "setcurrentpoint", 12, 33 },
  { "sqrt", 12, 26 },
  { "store", 12, 8 },
  { "sub", 12, 11 },
  { "vhcurveto", 30, -1 },
  { "vlineto", 7, -1 },
  { "vmoveto", 4, -1 },
  { "vstem", 3, -1 },
  { "vstem3", 12, 1 },
  { "vstemhm", 23, -1 },
  { "vvcurveto", 26, -1 },
};                                                /* alphabetical */

void fatal_error(char *message, ...);
void error(char *message, ...);

/* Two separate encryption functions because eexec and charstring encryption
   must proceed in parallel. */

static byte eencrypt(byte plain)
{
  byte cipher;

  cipher = (byte)(plain ^ (er >> 8));
  er = (uint16)((cipher + er) * c1 + c2);
  return cipher;
}

static byte cencrypt(byte plain)
{
  byte cipher;

  cipher = (byte)(plain ^ (cr >> 8));
  cr = (uint16)((cipher + cr) * c1 + c2);
  return cipher;
}

/* This function flushes a buffered PFB block. */

static void output_block()
{
  int32 i;

  /* output four-byte block length */
  fputc((int)(blockpos & 0xff), ofp);
  fputc((int)((blockpos >> 8) & 0xff), ofp);
  fputc((int)((blockpos >> 16) & 0xff), ofp);
  fputc((int)((blockpos >> 24) & 0xff), ofp);

  /* output block data */
  for (i = 0; i < blockpos; i++)
    fputc(blockbuf[i], ofp);

  /* mark block buffer empty and uninitialized */
  blockpos =  -1;
}

/* This function outputs a single byte.  If output is in PFB format then output
   is buffered through blockbuf[].  If output is in PFA format, then output
   will be hexadecimal if in_eexec is set, ASCII otherwise. */

static void output_byte(byte b)
{
  static char *hexchar = "0123456789ABCDEF";
  static int hexcol = 0;

  if (pfb) {
    /* PFB */
    if (blockpos < 0) {
      fputc(MARKER, ofp);
      fputc(blocktyp, ofp);
      blockpos = 0;
    }
    blockbuf[blockpos++] = b;
    if (blockpos == blocklen)
      output_block();
  } else {
    /* PFA */
    if (in_eexec) {
      /* trim hexadecimal lines to 64 columns */
      if (hexcol >= 64) {
	fputc('\n', ofp);
	hexcol = 0;
      }
      fputc(hexchar[(b >> 4) & 0xf], ofp);
      fputc(hexchar[b & 0xf], ofp);
      hexcol += 2;
    } else {
      fputc(b, ofp);
    }
  }
}

/* This function outputs a byte through possible eexec encryption. */

static void eexec_byte(byte b)
{
  if (in_eexec)
    output_byte(eencrypt(b));
  else
    output_byte(b);
}

/* This function outputs a null-terminated string through possible eexec
   encryption. */

static void eexec_string(char *string)
{
  while (*string)
    eexec_byte((byte) *string++);
}

/* This function gets ready for the eexec-encrypted data.  If output is in
   PFB format then flush current ASCII block and get ready for binary block.
   We start encryption with four random (zero) bytes. */

static void eexec_start()
{
  eexec_string(line);
  if (pfb) {
    output_block();
    blocktyp = BINARY;
  }

  in_eexec = 1;
  er = 55665;
  eexec_byte(0);
  eexec_byte(0);
  eexec_byte(0);
  eexec_byte(0);
}

/* This function returns an input line of characters.  A line is terminated by
   length (including terminating null) greater than LINESIZE, a newline \n, or
   when active (looking for charstrings) by '{'.  When terminated by a newline
   the newline is put into line[].  When terminated by '{', the '{' is not put
   into line[], and the flag start_charstring is set to 1. */

static void getline()
{
  int c;
  char *p = line;
  int comment = 0;

  start_charstring = 0;
  while (p < line + LINESIZE) {
    c = fgetc(ifp);
    if (c == EOF)
      break;
    if (c == '%')
      comment = 1;
    if (active && !comment && c == '{') {
      start_charstring = 1;
      break;
    }
    *p++ = (char) c;
    if (c == '\n')
      break;
  }
  /* change CR/LF to LF */
  if (p[-2] == '\r') {
    p[-2] = '\n';
    --p;
  }
  *p = '\0';
}

/* This function wraps-up the eexec-encrypted data and writes ASCII trailer.
   If output is in PFB format then this entails flushing binary block and
   starting an ASCII block. */

static void eexec_end()
{
  int i, j;

  if (pfb) {
    output_block();
    blocktyp = ASCII;
  } else {
    fputc('\n', ofp);
  }

  in_eexec = active = 0;

  for (i = 0; i < 8; i++) {
    for (j = 0; j < 64; j++)
      eexec_byte('0');
    eexec_byte('\n');
  }
  eexec_string("cleartomark\n");

  /* There may be additional code. */
  while (!feof(ifp) && !ferror(ifp)) {
    getline();
    eexec_string(line);
  }

  if (pfb) {
    output_block();
    fputc(MARKER, ofp);
    fputc(DONE, ofp);
  }
}

/* This function is used by the binary search, bsearch(), for command names in
   the command table. */

static int command_compare(const void *key, const void *item)
{
  return strcmp((char *) key, ((struct command *) item)->name);
}

/* This function returns 1 if the string is an integer and 0 otherwise. */

static int is_integer(char *string)
{
  if (isdigit(string[0]) || string[0] == '-' || string[0] == '+') {
    while (*++string && isdigit(*string))
      ;                                           /* deliberately empty */
    if (!*string)
      return 1;
  }
  return 0;
}

/* This function initializes charstring encryption.  Note that this is called
   at the beginning of every charstring. */

static void charstring_start()
{
  int i;

  charstring_bp = charstring_buf;
  cr = 4330;
  for (i = 0; i < lenIV; i++)
    *charstring_bp++ = cencrypt((byte) 0);
}

/* This function encrypts and buffers a single byte of charstring data. */

static void charstring_byte(int v)
{
  byte b = (byte)(v & 0xff);
  if (charstring_bp - charstring_buf > sizeof(charstring_buf))
    fatal_error("charstring buffer overflow");
  *charstring_bp++ = cencrypt(b);
}

/* This function outputs buffered, encrypted charstring data through possible
   eexec encryption. */

static void charstring_end()
{
  byte *bp;

  sprintf(line, "%d ", charstring_bp - charstring_buf);
  eexec_string(line);
  sprintf(line, "%s ", cs_start);
  eexec_string(line);
  for (bp = charstring_buf; bp < charstring_bp; bp++)
    eexec_byte(*bp);
}

/* This function generates the charstring representation of an integer. */

static void charstring_int(int num)
{
  int x;

  if (num >= -107 && num <= 107) {
    charstring_byte(num + 139);
  } else if (num >= 108 && num <= 1131) {
    x = num - 108;
    charstring_byte(x / 256 + 247);
    charstring_byte(x % 256);
  } else if (num >= -1131 && num <= -108) {
    x = abs(num) - 108;
    charstring_byte(x / 256 + 251);
    charstring_byte(x % 256);
  } else if (num >= (-2147483647-1) && num <= 2147483647) {
    charstring_byte(255);
    charstring_byte(num >> 24);
    charstring_byte(num >> 16);
    charstring_byte(num >> 8);
    charstring_byte(num);
  } else {
    error("can't format huge number `%d'", num);
    /* output 0 instead */
    charstring_byte(139);
  }
}

/* This function returns one charstring token. It ignores comments. */

static void get_charstring_token()
{
  int c = fgetc(ifp);
  while (isspace(c))
    c = fgetc(ifp);
  
  if (c == '%') {
    while (c != EOF && c != '\r' && c != '\n')
      c = fgetc(ifp);
    get_charstring_token();
    
  } else if (c == '}') {
    line[0] = '}';
    line[1] = 0;
    
  } else {
    char *p = line;
    while (p < line + LINESIZE) {
      *p++ = c;
      c = fgetc(ifp);
      if (c == EOF || isspace(c) || c == '%' || c == '}') {
	ungetc(c, ifp);
	break;
      }
    }
    *p = 0;
  }
}


/* This function parses an entire charstring into integers and commands,
   outputting bytes through the charstring buffer. */

static void parse_charstring()
{
  struct command *cp;

  charstring_start();
  while (!feof(ifp)) {
    get_charstring_token();
    if (line[0] == '}')
      break;
    if (is_integer(line)) {
      charstring_int(atoi(line));
    } else {
      cp = (struct command *)
	bsearch((void *) line, (void *) command_table,
		sizeof(command_table) / sizeof(struct command),
		sizeof(struct command),
		command_compare);
      if (cp) {
	charstring_byte(cp->one);
	if (cp->two >= 0)
	  charstring_byte(cp->two);
	
      } else if (strncmp(line, "UNKNOWN_", 8) == 0) {
	/* Allow unanticipated UNKNOWN commands. */
	int cmd;
	if (sscanf(line + 8, "12_%d", &cmd) == 1)
	  cmd += 32;
	else if (sscanf(line + 8, "%d", &cmd) == 1)
	  /* nada */;
	else {
	  error("unknown charstring command `%s'", line);
	  cmd = 0;
	}
	if (cmd < 0 || cmd > 255 + 32) {
	  error("bad charstring command number `%d'", cmd);
	  cmd = 0;
	}
	if (cmd < 32)
	  charstring_byte(cmd);
	else {
	  charstring_byte(12);
	  charstring_byte(cmd - 32);
	}
	
      } else {
	error("unknown charstring command `%s'", line);
	/* append a 0 byte to mark this command */
	charstring_byte(0);
      }
    }
  }
  charstring_end();
}


/*****
 * Command line
 **/

#define BLOCK_LEN_OPT	300
#define OUTPUT_OPT	301
#define VERSION_OPT	302
#define HELP_OPT	303
#define PFB_OPT		304
#define PFA_OPT		305

static Clp_Option options[] = {
  { "block-length", 'l', BLOCK_LEN_OPT, Clp_ArgInt, 0 },
  { "help", 0, HELP_OPT, 0, 0 },
  { "length", 0, BLOCK_LEN_OPT, Clp_ArgInt, 0 },
  { "output", 'o', OUTPUT_OPT, Clp_ArgString, 0 },
  { "pfa", 'a', PFA_OPT, 0, 0 },
  { "pfb", 'b', PFB_OPT, 0, 0 },
  { "version", 0, VERSION_OPT, 0, 0 },
};
static char *program_name;


void
fatal_error(char *message, ...)
{
  va_list val;
  va_start(val, message);
  fprintf(stderr, "%s: ", program_name);
  vfprintf(stderr, message, val);
  fputc('\n', stderr);
  exit(1);
}


void
error(char *message, ...)
{
  va_list val;
  va_start(val, message);
  fprintf(stderr, "%s: ", program_name);
  vfprintf(stderr, message, val);
  fputc('\n', stderr);
}


void
short_usage(void)
{
  fprintf(stderr, "Usage: %s [-abl] [input [output]]\n\
Type %s --help for more information.\n",
	  program_name, program_name);
}


void
usage(void)
{
  fprintf(stderr, "Usage: %s [options] [input [output]]\n\
Options:\n\
  --pfa, -a                     Output font in ASCII (PFA) format.\n\
  --pfb, -b                     Output font in binary (PFB) format. This is\n\
                                the default.\n\
  --block-length=NUM, -l NUM    Output PFB blocks will have size NUM.\n\
  --output FILE, -o FILE        Write output to FILE.\n\
  --help, -h                    Print this message and exit.\n\
  --version                     Print version number and warranty and exit.\n\
",
	  program_name);
}


int main(int argc, char **argv)
{
  char *p, *q, *r;
  int input_given = 0;
  int output_given = 0;
  
  Clp_Parser *clp =
    Clp_NewParser(argc, argv, sizeof(options) / sizeof(options[0]), options);
  program_name = (char *)Clp_ProgramName(clp);
  
  /* interpret command line arguments using CLP */
  while (1) {
    int opt = Clp_Next(clp);
    switch (opt) {
      
     case BLOCK_LEN_OPT:
      blocklen = clp->val.i;
      pfb = 1;
      if (blocklen < MINBLOCKLEN) {
	blocklen = MINBLOCKLEN;
	error("warning: block length raised to %d", blocklen);
      } else if (blocklen > MAXBLOCKLEN) {
	blocklen = MAXBLOCKLEN;
	error("warning: block length lowered to %d", blocklen);
      }
      break;
      
     output_file:
     case OUTPUT_OPT:
      if (output_given)
	fatal_error("output file already specified");
      output_given = 1;
      if (strcmp(clp->arg, "-") == 0)
	ofp = stdout;
      else {
	ofp = fopen(clp->arg, "w");
	if (!ofp) fatal_error("can't open %s for writing", clp->arg);
      }
      break;

     case PFB_OPT:
      pfb = 1;
      break;

     case PFA_OPT:
      pfb = 0;
      break;
      
     case HELP_OPT:
      usage();
      exit(0);
      break;
      
     case VERSION_OPT:
      printf("t1asm version %s\n", VERSION);
      printf("Copyright (C) 1992-8 I. Lee Hetherington et al.\n\
This is free software; see the source for copying conditions.\n\
There is NO warranty, not even for merchantability or fitness for a\n\
particular purpose. That's right: you're on your own!\n");
      exit(0);
      break;
      
     case Clp_NotOption:
      if (input_given && output_given)
	fatal_error("too many arguments");
      else if (input_given)
	goto output_file;
      input_given = 1;
      if (strcmp(clp->arg, "-") == 0)
	ifp = stdin;
      else {
	ifp = fopen(clp->arg, "r");
	if (!ifp) fatal_error("can't open %s for reading", clp->arg);
      }
      break;
      
     case Clp_Done:
      goto done;
      
     case Clp_BadOption:
      short_usage();
      exit(1);
      break;
      
    }
  }
  
 done:
  #ifdef _MSDOS
    /* If we are processing a PFB (binary) output */
    /* file, we must set its file mode to binary. */
    if (pfb)
      _setmode(_fileno(ofp), _O_BINARY);
  #endif

  /* Finally, we loop until no more input.  Some special things to look for
     are the `currentfile eexec' line, the beginning of the `/Subrs'
     definition, the definition of `/lenIV', and the definition of the
     charstring start command which has `...string currentfile...' in it. */

  while (!feof(ifp) && !ferror(ifp)) {
    getline();
    if (strcmp(line, "currentfile eexec\n") == 0) {
      eexec_start();
      continue;
    } else if (strstr(line, "/Subrs") && isspace(line[6])) {
      active = 1;
    } else if ((p = strstr(line, "/lenIV"))) {
      sscanf(p, "%*s %d", &lenIV);
    } else if ((p = strstr(line, "string currentfile"))) {
      /* locate the name of the charstring start command */
      *p = '\0';                                  /* damage line[] */
      q = strrchr(line, '/');
      if (q) {
	r = cs_start;
	++q;
	while (!isspace(*q) && *q != '{')
	  *r++ = *q++;
	*r = '\0';
      }
      *p = 's';                                   /* repair line[] */
    } else if (strstr(line, "mark currentfile closefile")) {
      eexec_string(line);
      break;
    }
    /* output line data */
    eexec_string(line);
    if (start_charstring) {
      if (!cs_start[0])
	fatal_error("couldn't find charstring start command");
      parse_charstring();
    }
  }
  eexec_end();
  
  fclose(ifp);
  fclose(ofp);

  return 0;
}