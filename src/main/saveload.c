/*
 *  R : A Computer Language for Statistical Data Analysis
 *  Copyright (C) 1995, 1996  Robert Gentleman and Ross Ihaka
 *  Copyright (C) 1997--2001  Robert Gentleman, Ross Ihaka and the
 *			      R Development Core Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <Defn.h>
#include <Rmath.h>
#include <Fileio.h>

#define R_MAGIC_ASCII_V1   1001
#define R_MAGIC_BINARY_V1  1002
#define R_MAGIC_XDR_V1     1003

/* Static Globals, DIE, DIE, DIE! */

static char smbuf[512];		/* Small buffer for temp use */
static char *buf=NULL;		/* Buffer for character strings */
static char *bufp;		/* A pointer to that buffer */
static int bufsize=0;		/* Current buffer size */

static int NSymbol;		/* Number of symbols */
static int NSave;		/* Number of non-symbols */
static int NTotal;		/* NSymbol + NSave */
static int NVSize;		/* Number of vector cells */

static int *OldOffset;		/* Offsets in previous incarnation */
static SEXP NewAddress;		/* Addresses in this incarnation */

static int VersionId;
static int DLstartup;		/* Allows different error action on startup */

static SEXP DataLoad(FILE*);

static void AllocBuffer(int len)
{
    if(len >= 0 ) {
	if(len*sizeof(char) < bufsize) return;
	len = (len+1)*sizeof(char);
	if(len < MAXELTSIZE) len = MAXELTSIZE;
	buf = (char *) realloc(buf, len);
	bufsize = len;
	if(!buf) {
	    bufsize = 0;
	    error("Could not allocate memory for string save/load");
	}
    } else {
	if(bufsize == MAXELTSIZE) return;
	free(buf);
	buf = (char *) malloc(MAXELTSIZE);
	bufsize = MAXELTSIZE;
    }
}

/* ----- I / O -- F u n c t i o n -- P o i n t e r s ----- */

static void	(*OutInit)(FILE*);
static void	(*OutInteger)(FILE*, int);
static void	(*OutReal)(FILE*, double);
static void	(*OutComplex)(FILE*, Rcomplex);
static void	(*OutString)(FILE*, char*);
static void	(*OutSpace)(FILE*, int);
static void	(*OutNewline)(FILE*);
static void	(*OutTerm)(FILE*);

static void	(*InInit)(FILE*);
static int	(*InInteger)(FILE*);
static double	(*InReal)(FILE*);
static Rcomplex	(*InComplex)(FILE*);
static char*	(*InString)(FILE*);
static void	(*InTerm)(FILE*);



/* ----- D u m m y -- P l a c e h o l d e r -- R o u t i n e s ----- */

static void DummyInit(FILE *fp)
{
}

static void DummyOutSpace(FILE *fp, int nspace)
{
}

static void DummyOutNewline(FILE *fp)
{
}

static void DummyTerm(FILE *fp)
{
}

/* ----- O l d - s t y l e  (p r e 1. 0)  R e s t o r e ----- */

/* This section is only used to load old-style workspaces / objects */


/* ----- L o w l e v e l -- A s c i i -- I / O ----- */

static int AsciiInInteger(FILE *fp)
{
    int x;
    fscanf(fp, "%s", smbuf);
    if (strcmp(smbuf, "NA") == 0)
	return NA_INTEGER;
    else {
	sscanf(smbuf, "%d", &x);
	return x;
    }
}

static double AsciiInReal(FILE *fp)
{
    double x;
    fscanf(fp, "%s", smbuf);
    if (strcmp(smbuf, "NA") == 0)
	x = NA_REAL;
    else if (strcmp(smbuf, "Inf") == 0)
	x = R_PosInf;
    else if (strcmp(smbuf, "-Inf") == 0)
	x = R_NegInf;
    else
	sscanf(smbuf, "%lg", &x);
    return x;
}

static Rcomplex AsciiInComplex(FILE *fp)
{
    Rcomplex x;
    fscanf(fp, "%s", smbuf);
    if (strcmp(smbuf, "NA") == 0)
	x.r = NA_REAL;
    else if (strcmp(smbuf, "Inf") == 0)
	x.r = R_PosInf;
    else if (strcmp(smbuf, "-Inf") == 0)
	x.r = R_NegInf;
    else
	sscanf(smbuf, "%lg", &x.r);

    fscanf(fp, "%s", smbuf);
    if (strcmp(smbuf, "NA") == 0)
	x.i = NA_REAL;
    else if (strcmp(smbuf, "Inf") == 0)
	x.i = R_PosInf;
    else if (strcmp(smbuf, "-Inf") == 0)
	x.i = R_NegInf;
    else
	sscanf(smbuf, "%lg", &x.i);
    return x;
}


static char *AsciiInString(FILE *fp)
{
    int c;
    bufp = buf;
    while ((c = R_fgetc(fp)) != '"');
    while ((c = R_fgetc(fp)) != R_EOF && c != '"') {
	if (c == '\\') {
	    if ((c = R_fgetc(fp)) == R_EOF) break;
	    switch(c) {
	    case 'n':  c = '\n'; break;
	    case 't':  c = '\t'; break;
	    case 'v':  c = '\v'; break;
	    case 'b':  c = '\b'; break;
	    case 'r':  c = '\r'; break;
	    case 'f':  c = '\f'; break;
	    case 'a':  c = '\a'; break;
	    case '\\': c = '\\'; break;
	    case '\?': c = '\?'; break;
	    case '\'': c = '\''; break;
	    case '\"': c = '\"'; break;
	    default:  break;
	    }
	}
	*bufp++ = c;
    }
    *bufp = '\0';
    return buf;
}

static SEXP AsciiLoad(FILE *fp)
{
    VersionId = 0;
    InInit = DummyInit;
    InInteger = AsciiInInteger;
    InReal = AsciiInReal;
    InComplex = AsciiInComplex;
    InString = AsciiInString;
    InTerm = DummyTerm;
    return DataLoad(fp);
}

static SEXP AsciiLoadOld(FILE *fp, int version)
{
    VersionId = version;
    InInit = DummyInit;
    InInteger = AsciiInInteger;
    InReal = AsciiInReal;
    InComplex = AsciiInComplex;
    InString = AsciiInString;
    InTerm = DummyTerm;
    return DataLoad(fp);
}

/* ----- L o w l e v e l -- X D R -- I / O ----- */

#ifdef HAVE_XDR

#include <rpc/rpc.h>

XDR xdrs;

static void XdrInInit(FILE *fp)
{
    xdrstdio_create(&xdrs, fp, XDR_DECODE);
}

static void XdrInTerm(FILE *fp)
{
    xdr_destroy(&xdrs);
}

static int XdrInInteger(FILE * fp)
{
    int i;
    if (!xdr_int(&xdrs, &i)) {
	xdr_destroy(&xdrs);
	error("a Iread error occured");
    }
    return i;
}

static double XdrInReal(FILE * fp)
{
    double x;
    if (!xdr_double(&xdrs, &x)) {
	xdr_destroy(&xdrs);
	error("a R read error occured");
    }
    return x;
}

static Rcomplex XdrInComplex(FILE * fp)
{
    Rcomplex x;
    if (!xdr_double(&xdrs, &(x.r)) || !xdr_double(&xdrs, &(x.i))) {
	xdr_destroy(&xdrs);
	error("a C read error occured");
    }
    return x;
}

static char *XdrInString(FILE *fp)
{
    char *bufp = buf;
    if (!xdr_string(&xdrs, &bufp, bufsize)) {
	xdr_destroy(&xdrs);
	error("a S read error occured");
    }
    return buf;
}

static SEXP XdrLoad(FILE *fp)
{
    VersionId = 0;
    InInit = XdrInInit;
    InInteger = XdrInInteger;
    InReal = XdrInReal;
    InComplex = XdrInComplex;
    InString = XdrInString;
    InTerm = XdrInTerm;
    return DataLoad(fp);
}
#endif /* HAVE_XDR */


/* ----- L o w l e v e l -- B i n a r y -- I / O ----- */

static int BinaryInInteger(FILE * fp)
{
    int i;
    if (fread(&i, sizeof(int), 1, fp) != 1)
	error("a read error occured");
    return i;
}

static double BinaryInReal(FILE * fp)
{
    double x;
    if (fread(&x, sizeof(double), 1, fp) != 1)
	error("a read error occured");
    return x;
}

static Rcomplex BinaryInComplex(FILE * fp)
{
    Rcomplex x;
    if (fread(&x, sizeof(Rcomplex), 1, fp) != 1)
	error("a read error occured");
    return x;
}

static char *BinaryInString(FILE *fp)
{
    bufp = buf;
    do {
	*bufp = R_fgetc(fp);
    }
    while (*bufp++);
    return buf;
}

static SEXP BinaryLoad(FILE *fp)
{
    VersionId = 0;
    InInit = DummyInit;
    InInteger = BinaryInInteger;
    InReal = BinaryInReal;
    InComplex = BinaryInComplex;
    InString = BinaryInString;
    InTerm = DummyTerm;
    return DataLoad(fp);
}

static SEXP BinaryLoadOld(FILE *fp, int version)
{
    VersionId = version;
    InInit = DummyInit;
    InInteger = BinaryInInteger;
    InReal = BinaryInReal;
    InComplex = BinaryInComplex;
    InString = BinaryInString;
    InTerm = DummyTerm;
    return DataLoad(fp);
}

static SEXP OffsetToNode(int offset)
{
    int l, m, r;

    if (offset == -1) return R_NilValue;
    if (offset == -2) return R_GlobalEnv;
    if (offset == -3) return R_UnboundValue;
    if (offset == -4) return R_MissingArg;

    /* binary search for offset */

    l = 0;
    r = NTotal - 1;
    do {
	m = (l + r) / 2;
	if (offset < OldOffset[m])
	    r = m - 1;
	else
	    l = m + 1;
    }
    while (offset != OldOffset[m] && l <= r);
    if (offset == OldOffset[m]) return VECTOR_ELT(NewAddress, m);

    /* Not supposed to happen: */
    warning("unresolved node during restore");
    return R_NilValue;
}

static unsigned int FixupType(unsigned int type)
{
    if (VersionId) {
	switch(VersionId) {

	case 16:
	    /* In the version 0.16.1 -> 0.50 switch */
	    /* we really introduced complex values */
	    /* and found that numeric/complex numbers */
	    /* had to be contiguous.  Hence this switch */
	    if (type == STRSXP)
		type = CPLXSXP;
	    else if (type == CPLXSXP)
		type = STRSXP;
	    break;

	default:
	    error("restore compatibility error - no version %d compatibility",
		  VersionId);
	}
    }

    /* Map old factors to new ...  (0.61->0.62) */
    if (type == 11 || type == 12)
	type = 13;

    return type;
}

static void RemakeNextSEXP(FILE *fp)
{
    unsigned int j, idx, type;
    int len;
    SEXP s = R_NilValue;	/* -Wall */

    idx = InInteger(fp),
    type = FixupType(InInteger(fp));

    /* skip over OBJECT, LEVELS, and ATTRIB */
    /* OBJECT(s) = */ InInteger(fp);
    /* LEVELS(s) = */ InInteger(fp);
    /* ATTRIB(s) = */ InInteger(fp);
    switch (type) {
    case LISTSXP:
    case LANGSXP:
    case CLOSXP:
    case PROMSXP:
    case ENVSXP:
	s = allocSExp(type);
	/* skip over CAR, CDR, and TAG */
	/* CAR(s) = */ InInteger(fp);
	/* CDR(s) = */ InInteger(fp);
	/* TAG(s) = */ InInteger(fp);
	break;
    case SPECIALSXP:
    case BUILTINSXP:
	s = allocSExp(type);
	/* skip over length and name fields */
	/* length = */ InInteger(fp);
	AllocBuffer(MAXELTSIZE - 1);
	/* name = */ InString(fp);
	break;
    case CHARSXP:
	len = InInteger(fp);
	s = allocString(len);
	AllocBuffer(len);
	/* skip over the string */
	/* string = */ InString(fp);
	break;
    case REALSXP:
        len = InInteger(fp);
	s = allocVector(type, len);
	/* skip over the vector content */
	for (j = 0; j < len; j++)
	    /*REAL(s)[j] = */ InReal(fp);
	break;
    case CPLXSXP:
	len = InInteger(fp);
	s = allocVector(type, len);
	/* skip over the vector content */
	for (j = 0; j < len; j++)
	    /* COMPLEX(s)[j] = */ InComplex(fp);
	break;
    case INTSXP:
    case LGLSXP:
	len = InInteger(fp);;
	s = allocVector(type, len);
	/* skip over the vector content */
	for (j = 0; j < len; j++)
	    /* INTEGER(s)[j] = */ InInteger(fp);
	break;
    case STRSXP:
    case VECSXP:
    case EXPRSXP:
	len = InInteger(fp);
	s = allocVector(type, len);
	/* skip over the vector content */
	for (j = 0; j < len; j++) {
	    /* VECTOR(s)[j] = */ InInteger(fp);
	}
	break;
    default: error("bad SEXP type in data file");
    }

    /* install the new SEXP */
    SET_VECTOR_ELT(NewAddress, idx, s);
}

static void RestoreSEXP(SEXP s, FILE *fp)
{
    unsigned int j, type;
    int len;

    type = FixupType(InInteger(fp));
    if (type != TYPEOF(s))
      error("mismatch on types");

    SET_OBJECT(s, InInteger(fp));
    SETLEVELS(s, InInteger(fp));
    SET_ATTRIB(s, OffsetToNode(InInteger(fp)));
    switch (TYPEOF(s)) {
    case LISTSXP:
    case LANGSXP:
    case CLOSXP:
    case PROMSXP:
    case ENVSXP:
	SETCAR(s, OffsetToNode(InInteger(fp)));
	SETCDR(s, OffsetToNode(InInteger(fp)));
	SET_TAG(s, OffsetToNode(InInteger(fp)));
	break;
    case SPECIALSXP:
    case BUILTINSXP:
	len = InInteger(fp);
	AllocBuffer(MAXELTSIZE - 1);
	SET_PRIMOFFSET(s, StrToInternal(InString(fp)));
	break;
    case CHARSXP:
	len = InInteger(fp);
	AllocBuffer(len);
	strcpy(CHAR(s), InString(fp));
	break;
    case REALSXP:
	len = InInteger(fp);
	for (j = 0; j < len; j++)
	    REAL(s)[j] = InReal(fp);
	break;
    case CPLXSXP:
	len = InInteger(fp);
	for (j = 0; j < len; j++)
	    COMPLEX(s)[j] = InComplex(fp);
	break;
    case INTSXP:
    case LGLSXP:
	len = InInteger(fp);;
	for (j = 0; j < len; j++)
	    INTEGER(s)[j] = InInteger(fp);
	break;
    case STRSXP:
    case VECSXP:
    case EXPRSXP:
	len = InInteger(fp);
	for (j = 0; j < len; j++) {
	    SET_VECTOR_ELT(s, j, OffsetToNode(InInteger(fp)));
	}
	break;
    default: error("bad SEXP type in data file");
    }
}

static void RestoreError(char *msg)
{
    if(DLstartup)
	R_Suicide(msg);
    else
	error(msg);
}
  
static SEXP DataLoad(FILE *fp)
{
    int i, j;
    char *vmaxsave;
    fpos_t savepos;

    /* read in the size information */

    InInit(fp);

    NSymbol = InInteger(fp);
    NSave = InInteger(fp);
    NVSize = InInteger(fp);
    NTotal = NSymbol + NSave;

    /* allocate the forwarding-address tables */
    /* these are non-relocatable, so we must */
    /* save the current non-relocatable base */

    vmaxsave = vmaxget();
    OldOffset = (int*)R_alloc(NSymbol+NSave, sizeof(int));
    PROTECT(NewAddress = allocVector(VECSXP, NSymbol+NSave));
    for (i = 0 ; i < NTotal ; i++) {
	OldOffset[i] = 0;
	SET_VECTOR_ELT(NewAddress, i, R_NilValue);
    }

    /* read in the required symbols */
    /* expanding the symbol table and */
    /* computing the forwarding addresses */

    for (i = 0 ; i < NSymbol ; i++) {
	j = InInteger(fp);
	OldOffset[j] = InInteger(fp);
	AllocBuffer(MAXELTSIZE - 1);
	SET_VECTOR_ELT(NewAddress, j, install(InString(fp)));
    }

    /* build the full forwarding table */

    for (i = 0 ; i < NSave ; i++) {
	j = InInteger(fp);
	OldOffset[j] = InInteger(fp);
    }


    /* save the file position */
    if (fgetpos(fp, &savepos))
	RestoreError("can't save file position while restoring data");


    /* first pass: allocate nodes */

    for (i = 0 ; i < NSave ; i++) {
        RemakeNextSEXP(fp);
    }


    /* restore the file position */
    if (fsetpos(fp, &savepos))
	RestoreError("can't restore file position while restoring data");


    /* second pass: restore the contents of the nodes */

    for (i = 0 ; i < NSave ;  i++) {
	RestoreSEXP(VECTOR_ELT(NewAddress, InInteger(fp)), fp);
    }

    /* restore the heap */

    vmaxset(vmaxsave);
    UNPROTECT(1);

    /* clean the string buffer */
    AllocBuffer(-1);

    /* return the "top-level" object */
    /* this is usually a list */

    i = InInteger(fp);
    InTerm(fp);

    return OffsetToNode(i);
}

/* These functions convert old (pairlist) lists into new */
/* (vectorlist) lists.	The conversion can be defeated by */
/* hiding things inside closures, but it is doubtful that */
/* anyone has done this. */

static SEXP ConvertPairToVector(SEXP);

static SEXP ConvertAttributes(SEXP attrs)
{
    SEXP ap = attrs;
    while (ap != R_NilValue) {
	if (TYPEOF(CAR(ap)) == LISTSXP)
	    SETCAR(ap, ConvertPairToVector(CAR(ap)));
	ap = CDR(ap);
    }
    return attrs;
}

#ifdef NOTYET
/* It may be that there are LISTXP hiding in closures. */
/* This will convert them. */

static SEXP ConvertEnvironment(SEXP env)
{
    SEXP frame = FRAME(env);
    while (frame != R_NilValue) {
	if (TYPEOF(CAR(frame)) == LISTSXP)
	    CAR(frame) = ConvertPairToVector(CAR(frame));
	frame = CDR(frame);
    }
    return env;
}
#endif /* NOTYET */

static SEXP ConvertPairToVector(SEXP obj)
{
    int i, n;
    switch (TYPEOF(obj)) {
    case LISTSXP:
	PROTECT(obj = PairToVectorList(obj));
	n = length(obj);
	for (i = 0; i < n; i++)
	    SET_VECTOR_ELT(obj, i, ConvertPairToVector(VECTOR_ELT(obj, i)));
	UNPROTECT(1);
	break;
    case VECSXP:
	break;
    default:
	;
    }
    SET_ATTRIB(obj, ConvertAttributes(ATTRIB(obj)));
    return obj;
}


/* ----- V e r s i o n -- O n e -- S a v e / R e s t o r e ----- */

/*  Code Developed by  Chris K. Young <cky@pobox.com>
 *  and Ross Ihaka for Chris' Honours project -- 1999.
 *  Copyright Assigned to the R Project.
 */

/*  An assert function which doesn't crash the program.
 *  Something like this might be useful in an R header file
 */

#ifdef NDEBUG
#define R_assert(e) ((void) 0)
#else
/* The line below requires an ANSI C preprocessor (stringify operator) */
#define R_assert(e) ((e) ? (void) 0 : error("assertion `%s' failed: file `%s', line %d\n", #e, __FILE__, __LINE__))
#endif /* NDEBUG */


static void NewWriteItem (SEXP s, SEXP sym_list, SEXP env_list, FILE *fp);
static SEXP NewReadItem (SEXP sym_table, SEXP env_table, FILE *fp);


/*  We use special (negative) type codes to indicate the special
 *  values: R_NilValue, R_GlobalEnv, R_UnboundValue, R_MissingArg.
 *  The following routines handle these conversions (both
 *  directions). */

static int NewSaveSpecialHook (SEXP item)
{
    if (item == R_NilValue)     return -1;
    if (item == R_GlobalEnv)    return -2;
    if (item == R_UnboundValue) return -3;
    if (item == R_MissingArg)   return -4;
    return 0;
}

static SEXP NewLoadSpecialHook (SEXPTYPE type)
{
    switch (type) {
    case -1: return R_NilValue;
    case -2: return R_GlobalEnv;
    case -3: return R_UnboundValue;
    case -4: return R_MissingArg;
    }
    return (SEXP) 0;	/* not strictly legal... */
}


/*  If "item" is a special value (as defined in "NewSaveSpecialHook")
 *  then a negative value is returned.
 *
 *  If "item" is present in "list" the a positive value is returned
 *  (the 1-based offset into the list).
 *
 *   Otherwise, a value of zero is returned.  */

static int NewLookup (SEXP item, SEXP list)
{
    SEXP iterator = list;
    int count;

    if ((count = NewSaveSpecialHook(item)))	/* variable reuse :-) */
	return count;
    /* now `count' is zero */
    while (iterator != R_NilValue) {
	R_assert(TYPEOF(list) == LISTSXP);
	++count;
	if (CAR(iterator) == item)
	    return count;
	iterator = CDR(iterator);
    }
    return 0;
}

/*  This code carries out the basic inspection of an object, building
 *  the tables of symbols and environments.
 *
 *  We don't really need to build a table of symbols here, but it does
 *  prevent repeated "install"s.  On the other hand there will generally
 *  be huge delays because of disk or network latency ...
 *
 *  CKY: One thing I've found out is that you have to build all the
 *  lists together or you risk getting infinite loops.  Of course, the
 *  method used here somehow shoots functional programming in the
 *  head --- sorry.  */

static void NewMakeLists (SEXP obj, SEXP *sym_list, SEXP *env_list)
{
    int count, length;

    if (NewSaveSpecialHook(obj))
	return;
    switch (TYPEOF(obj)) {
    case SYMSXP:
	if (NewLookup(obj, *sym_list))
	    return;
	PROTECT(*env_list);
	*sym_list = CONS(obj, *sym_list);
	UNPROTECT(1);
	break;
    case ENVSXP:
	if (NewLookup(obj, *env_list))
	    return;
	PROTECT(*sym_list);
	*env_list = CONS(obj, *env_list);
	UNPROTECT(1);
	/* FALLTHROUGH */
    case LISTSXP:
    case LANGSXP:
    case CLOSXP:
    case PROMSXP:
    case DOTSXP:
	NewMakeLists(TAG(obj), sym_list, env_list);
	NewMakeLists(CAR(obj), sym_list, env_list);
	NewMakeLists(CDR(obj), sym_list, env_list);
	break;
    case EXTPTRSXP:
	NewMakeLists(EXTPTR_PROT(obj), sym_list, env_list);
	NewMakeLists(EXTPTR_TAG(obj), sym_list, env_list);
	break;
    case VECSXP:
    case EXPRSXP:
	length = LENGTH(obj);
	for (count = 0; count < length; ++count)
	    NewMakeLists(VECTOR_ELT(obj, count), sym_list, env_list);
	break;
    }
    NewMakeLists(ATTRIB(obj), sym_list, env_list);
}

/* e.g., OutVec(fp, obj, INTEGER, OutInteger) */
#define OutVec(fp, obj, accessor, outfunc)	 			\
	do { 								\
		int cnt;						\
		for (cnt = 0; cnt < LENGTH(obj); ++cnt) {		\
			OutSpace(fp, 1);			       	\
			outfunc(fp, accessor(obj, cnt));		\
                        OutNewline(fp);                                 \
		}							\
	} while (0)

#define LOGICAL_ELT(x,__i__)	LOGICAL(x)[__i__]
#define INTEGER_ELT(x,__i__)	INTEGER(x)[__i__]
#define REAL_ELT(x,__i__)	REAL(x)[__i__]
#define COMPLEX_ELT(x,__i__)	COMPLEX(x)[__i__]

/* Simply outputs the string associated with a CHARSXP, one day this
 * will handle null characters in CHARSXPs and not just blindly call
 * OutString.  */
static void OutCHARSXP (FILE *fp, SEXP s)
{
    R_assert(TYPEOF(s) == CHARSXP);
    OutString(fp, CHAR(s));
}

static void NewWriteVec (SEXP s, SEXP sym_list, SEXP env_list, FILE *fp)
{
    int count;

    /* I can assert here that `s' is one of the vector types, but
     * it'll turn out to be one big ugly statement... so I'll do it at
     * the bottom.  */

    OutInteger(fp, LENGTH(s));
    OutNewline(fp);
    switch (TYPEOF(s)) {
    case CHARSXP:
	OutSpace(fp, 1);
	OutCHARSXP(fp, s);
	break;
    case LGLSXP:
    case INTSXP:
	OutVec(fp, s, INTEGER_ELT, OutInteger);
	break;
    case REALSXP:
	OutVec(fp, s, REAL_ELT, OutReal);
	break;
    case CPLXSXP:
	OutVec(fp, s, COMPLEX_ELT, OutComplex);
	break;
    case STRSXP:
	OutVec(fp, s, STRING_ELT, OutCHARSXP);
	break;
    case VECSXP:
    case EXPRSXP:
	for (count = 0; count < LENGTH(s); ++count) {
	    /* OutSpace(fp, 1); */
	    NewWriteItem(VECTOR_ELT(s, count), sym_list, env_list, fp);
	    OutNewline(fp);
	}
	break;
    default:
	error("NewWriteVec called with non-vector type");
    }
}

static void NewWriteItem (SEXP s, SEXP sym_list, SEXP env_list, FILE *fp)
{
    int i;

    if ((i = NewSaveSpecialHook(s))) {
	OutInteger(fp, i);
	OutNewline(fp);
    }
    else {
	OutInteger(fp, TYPEOF(s));
	OutSpace(fp, 1); OutInteger(fp, LEVELS(s));
	OutSpace(fp, 1); OutInteger(fp, OBJECT(s));
	OutNewline(fp);
	switch (TYPEOF(s)) {
	    /* Note : NILSXP can't occur here */
	case SYMSXP:
	    i = NewLookup(s, sym_list);
	    R_assert(i);
	    OutInteger(fp, i); OutNewline(fp);
	    break;
	case ENVSXP:
	    i = NewLookup(s, env_list);
	    R_assert(i);
	    OutInteger(fp, i); OutNewline(fp);
	    break;
	case LISTSXP:
	case LANGSXP:
	case CLOSXP:
	case PROMSXP:
	case DOTSXP:
	    /* Dotted pair objects */
	    NewWriteItem(TAG(s), sym_list, env_list, fp);
	    NewWriteItem(CAR(s), sym_list, env_list, fp);
	    NewWriteItem(CDR(s), sym_list, env_list, fp);
	    break;
	case EXTPTRSXP:
	    NewWriteItem(EXTPTR_PROT(s), sym_list, env_list, fp);
	    NewWriteItem(EXTPTR_TAG(s), sym_list, env_list, fp);
	    break;
	case SPECIALSXP:
	case BUILTINSXP:
	    /* Builtin functions */
	    OutString(fp, PRIMNAME(s)); OutNewline(fp);
	    break;
	case CHARSXP:
	case LGLSXP:
	case INTSXP:
	case REALSXP:
	case CPLXSXP:
	case STRSXP:
	case VECSXP:
	case EXPRSXP:
	    /* Vector Objects */
	    NewWriteVec(s, sym_list, env_list, fp);
	    break;
	default:
	    error("NewWriteItem: unknown type %i", TYPEOF(s));
	}
	NewWriteItem(ATTRIB(s), sym_list, env_list, fp);
    }
}

/*  General format: the total number of symbols, then the total number
 *  of environments.  Then all the symbol names get written out,
 *  followed by the environments, then the items to be saved.  If
 *  symbols or environments are encountered, references to them are
 *  made instead of writing them out totally.  */

static void NewDataSave (SEXP s, FILE *fp)
{
    SEXP the_sym_list = R_NilValue, the_env_list = R_NilValue, iterator;
    int sym_count, env_count;

    NewMakeLists(s, &the_sym_list, &the_env_list);
    OutInit(fp);
    OutInteger(fp, sym_count = length(the_sym_list)); OutSpace(fp, 1);
    OutInteger(fp, env_count = length(the_env_list)); OutNewline(fp);
    for (iterator = the_sym_list; sym_count--; iterator = CDR(iterator)) {
	R_assert(TYPEOF(CAR(iterator)) == SYMSXP);
	OutString(fp, CHAR(PRINTNAME(CAR(iterator))));
	OutNewline(fp);
    }
    for (iterator = the_env_list; env_count--; iterator = CDR(iterator)) {
	R_assert(TYPEOF(CAR(iterator)) == ENVSXP);
	NewWriteItem(ENCLOS(CAR(iterator)), the_sym_list, the_env_list, fp);
	NewWriteItem(FRAME(CAR(iterator)), the_sym_list, the_env_list, fp);
	NewWriteItem(TAG(CAR(iterator)), the_sym_list, the_env_list, fp);
    }
    NewWriteItem(s, the_sym_list, the_env_list, fp);
    OutTerm(fp);
}

#define InVec(fp, obj, accessor, infunc, length)			\
	do {								\
		int cnt;						\
		for (cnt = 0; cnt < length; ++cnt)		\
			accessor(obj, cnt, infunc(fp));		\
	} while (0)



#define SET_LOGICAL_ELT(x,__i__,v)	(LOGICAL_ELT(x,__i__)=(v))
#define SET_INTEGER_ELT(x,__i__,v)	(INTEGER_ELT(x,__i__)=(v))
#define SET_REAL_ELT(x,__i__,v)		(REAL_ELT(x,__i__)=(v))
#define SET_COMPLEX_ELT(x,__i__,v)	(COMPLEX_ELT(x,__i__)=(v))

static SEXP InCHARSXP (FILE *fp)
{
    SEXP s;
    char *tmp;
    int len;

    /* FIXME: rather than use strlen, use actual length of string when
     * sized strings get implemented in R's save/load code.  */
    tmp = InString(fp);
    len = strlen(tmp);
    AllocBuffer(len);
    s = allocVector(CHARSXP, len);
    memcpy(CHAR(s), tmp, len+1);
    return s;
}

static SEXP NewReadVec(SEXPTYPE type, SEXP sym_table, SEXP env_table, FILE *fp)
{
    int length, count;
    SEXP my_vec;

    length = InInteger(fp);
    PROTECT(my_vec = allocVector(type, length));
    switch(type) {
    case CHARSXP:
	my_vec = InCHARSXP(fp);
	break;
    case LGLSXP:
    case INTSXP:
	InVec(fp, my_vec, SET_INTEGER_ELT, InInteger, length);
	break;
    case REALSXP:
	InVec(fp, my_vec, SET_REAL_ELT, InReal, length);
	break;
    case CPLXSXP:
	InVec(fp, my_vec, SET_COMPLEX_ELT, InComplex, length);
	break;
    case STRSXP:
	InVec(fp, my_vec, SET_STRING_ELT, InCHARSXP, length);
	break;
    case VECSXP:
    case EXPRSXP:
	for (count = 0; count < length; ++count)
	    SET_VECTOR_ELT(my_vec, count, NewReadItem(sym_table, env_table, fp));
	break;
    default:
	error("NewReadVec called with non-vector type");
    }
    UNPROTECT(1);
    return my_vec;
}

static SEXP NewReadItem (SEXP sym_table, SEXP env_table, FILE *fp)
{
    SEXPTYPE type;
    SEXP s;
    int pos, levs, objf;

    R_assert(TYPEOF(sym_table) == VECSXP && TYPEOF(env_table) == VECSXP);
    type = InInteger(fp);
    if ((s = NewLoadSpecialHook(type)))
	return s;
    levs = InInteger(fp);
    objf = InInteger(fp);
    switch (type) {
    case SYMSXP:
	pos = InInteger(fp);
	PROTECT(s = pos ? VECTOR_ELT(sym_table, pos - 1) : R_NilValue);
	break;
    case ENVSXP:
	pos = InInteger(fp);
	PROTECT(s = pos ? VECTOR_ELT(env_table, pos - 1) : R_NilValue);
	break;
    case LISTSXP:
    case LANGSXP:
    case CLOSXP:
    case PROMSXP:
    case DOTSXP:
	PROTECT(s = allocSExp(type));
	SET_TAG(s, NewReadItem(sym_table, env_table, fp));
	SETCAR(s, NewReadItem(sym_table, env_table, fp));
	SETCDR(s, NewReadItem(sym_table, env_table, fp));
	/*UNPROTECT(1);*/
	break;
    case EXTPTRSXP:
	PROTECT(s = allocSExp(type));
	R_SetExternalPtrAddr(s, NULL);
	R_SetExternalPtrProtected(s, NewReadItem(sym_table, env_table, fp));
	R_SetExternalPtrTag(s, NewReadItem(sym_table, env_table, fp));
	/*UNPROTECT(1);*/
	break;
    case SPECIALSXP:
    case BUILTINSXP:
	AllocBuffer(MAXELTSIZE - 1);
	PROTECT(s = mkPRIMSXP(StrToInternal(InString(fp)), type == BUILTINSXP));
	break;
    case CHARSXP:
    case LGLSXP:
    case INTSXP:
    case REALSXP:
    case CPLXSXP:
    case STRSXP:
    case VECSXP:
    case EXPRSXP:
	PROTECT(s = NewReadVec(type, sym_table, env_table, fp));
	break;
    default:
	error("NewReadItem: unknown type %i", type);
    }
    SETLEVELS(s, levs);
    SET_OBJECT(s, objf);
    SET_ATTRIB(s, NewReadItem(sym_table, env_table, fp));
    UNPROTECT(1); /* s */
    return s;
}

static SEXP NewDataLoad (FILE *fp)
{
    int sym_count, env_count, count;
    SEXP sym_table, env_table, obj;

    InInit(fp);

    /* Read the table sizes */
    sym_count = InInteger(fp);
    env_count = InInteger(fp);

    /* Allocate the symbol and environment tables */
    PROTECT(sym_table = allocVector(VECSXP, sym_count));
    PROTECT(env_table = allocVector(VECSXP, env_count));

    /* Read back and install symbols */
    for (count = 0; count < sym_count; ++count) {
	SET_VECTOR_ELT(sym_table, count, install(InString(fp)));
    }
    /* Allocate the environments */
    for (count = 0; count < env_count; ++count)
	SET_VECTOR_ELT(env_table, count, allocSExp(ENVSXP));

    /* Now fill them in  */
    for (count = 0; count < env_count; ++count) {
	obj = VECTOR_ELT(env_table, count);
	SET_ENCLOS(obj, NewReadItem(sym_table, env_table, fp));
	SET_FRAME(obj, NewReadItem(sym_table, env_table, fp));
	SET_TAG(obj, NewReadItem(sym_table, env_table, fp));
    }

    /* Read the actual object back */
    obj =  NewReadItem(sym_table, env_table, fp);

    /* Wrap up */
    InTerm(fp);
    UNPROTECT(2);
    return obj;
}

/* ----- L o w l e v e l -- A s c i i -- I / O ------ */

static void OutSpaceAscii(FILE *fp, int nspace)
{
    while(--nspace >= 0)
	fputc(' ', fp);
}
static void OutNewlineAscii(FILE *fp)
{
    fputc('\n', fp);
}

static void OutIntegerAscii(FILE *fp, int x)
{
    if (x == NA_INTEGER) fprintf(fp, "NA");
    else fprintf(fp, "%d", x);
}

static int InIntegerAscii(FILE *fp)
{
    char buf[128];
    int x;
    fscanf(fp, "%s", buf);
    if (strcmp(buf, "NA") == 0)
	return NA_INTEGER;
    else
	sscanf(buf, "%d", &x);
    return x;
}

static void OutStringAscii(FILE *fp, char *x)
{
    int i, nbytes;
    nbytes = strlen(x);
    fprintf(fp, "%d ", nbytes);
    for (i = 0; i < nbytes; i++) {
        if (x[i] <= 32 || x[i] > 126) {
            switch(x[i]) {
            case '\n': fprintf(fp, "\\n");  break;
            case '\t': fprintf(fp, "\\t");  break;
            case '\v': fprintf(fp, "\\v");  break;
            case '\b': fprintf(fp, "\\b");  break;
            case '\r': fprintf(fp, "\\r");  break;
            case '\f': fprintf(fp, "\\f");  break;
            case '\a': fprintf(fp, "\\a");  break;
            case '\\': fprintf(fp, "\\\\"); break;
            case '\?': fprintf(fp, "\\?");  break;
            case '\'': fprintf(fp, "\\'");  break;
            case '\"': fprintf(fp, "\\\""); break;
		/* cannot print char in octal mode -> cast to unsigned
		   char first */
            default  : fprintf(fp, "\\%03o", (unsigned char) x[i]); break;
            }
        }
        else fputc(x[i], fp);
    }
}

static char *InStringAscii(FILE *fp)
{
    static char *buf = NULL;
    static int buflen = 0;
    int c, d, i, j;
    int nbytes;
    fscanf(fp, "%d", &nbytes);
    /* FIXME : Ultimately we need to replace */
    /* this with a real string allocation. */
    /* All buffers must die! */
    if (nbytes >= buflen) {
	char *newbuf = realloc(buf, nbytes + 1);
	if (newbuf == NULL)
	    error("out of memory reading ascii string\n");
	buf = newbuf;
	buflen = nbytes + 1;
    }
    while(isspace(c = fgetc(fp)))
        ;
    ungetc(c, fp);
    for (i = 0; i < nbytes; i++) {
        if ((c =  fgetc(fp)) == '\\') {
            switch(c = fgetc(fp)) {
            case 'n' : buf[i] = '\n'; break;
            case 't' : buf[i] = '\t'; break;
            case 'v' : buf[i] = '\v'; break;
            case 'b' : buf[i] = '\b'; break;
            case 'r' : buf[i] = '\r'; break;
            case 'f' : buf[i] = '\f'; break;
            case 'a' : buf[i] = '\a'; break;
            case '\\': buf[i] = '\\'; break;
            case '?' : buf[i] = '\?'; break;
            case '\'': buf[i] = '\''; break;
            case '\"': buf[i] = '\"'; break;
            case '0': case '1': case '2': case '3':
            case '4': case '5': case '6': case '7':
                d = 0; j = 0;
                while('0' <= c && c < '8' && j < 3) {
                    d = d * 8 + (c - '0');
                    c = fgetc(fp);
                    j++;
                }
                buf[i] = d;
                ungetc(c, fp);
                break;
            default  : buf[i] = c;
            }
        }
        else buf[i] = c;
    }
    buf[i] = '\0';
    return buf;
}

static void OutDoubleAscii(FILE *fp, double x)
{
    if (!R_FINITE(x)) {
	if (ISNAN(x)) fprintf(fp, "NA");
	else if (x < 0) fprintf(fp, "-Inf");
	else fprintf(fp, "Inf");
    }
    /* 16: full precision; 17 gives 999, 000 &c */
    else fprintf(fp, "%.16g", x);
}

static double InDoubleAscii(FILE *fp)
{
    char buf[128];
    double x;
    fscanf(fp, "%s", buf);
    if (strcmp(buf, "NA") == 0)
	x = NA_REAL;
    else if (strcmp(buf, "Inf") == 0)
	x = R_PosInf;
    else if (strcmp(buf, "-Inf") == 0)
	x = R_NegInf;
    else
	sscanf(buf, "%lg", &x);
    return x;
}

static void OutComplexAscii(FILE *fp, Rcomplex x)
{
    if (ISNAN(x.r) || ISNAN(x.i))
	fprintf(fp, "NA NA");
    else {
	OutDoubleAscii(fp, x.r);
	OutSpaceAscii(fp, 1);
	OutDoubleAscii(fp, x.i);
    }
}

static Rcomplex InComplexAscii(FILE *fp)
{
    Rcomplex x;
    x.r = InDoubleAscii(fp);
    x.i = InDoubleAscii(fp);
    return x;
}

static void NewAsciiSave(SEXP s, FILE *fp)
{
    OutInit = DummyInit;
    OutInteger = OutIntegerAscii;
    OutReal = OutDoubleAscii;
    OutComplex = OutComplexAscii;
    OutString = OutStringAscii;
    OutSpace = OutSpaceAscii;
    OutNewline = OutNewlineAscii;
    OutTerm = DummyTerm;
    NewDataSave(s, fp);
}

static SEXP NewAsciiLoad(FILE *fp)
{
    InInit = DummyInit;
    InInteger = InIntegerAscii;
    InReal = InDoubleAscii;
    InComplex = InComplexAscii;
    InString = InStringAscii;
    InTerm = DummyTerm;
    return NewDataLoad(fp);
}

/* ----- L o w l e v e l -- B i n a r y -- I / O ----- */

static int InIntegerBinary(FILE * fp)
{
    int i;
    if (fread(&i, sizeof(int), 1, fp) != 1)
	error("a binary read error occured");
    return i;
}

static char *InStringBinary(FILE *fp)
{
    static char *buf = NULL;
    static int buflen = 0;
    int nbytes = InIntegerBinary(fp);
    if (nbytes >= buflen) {
	char *newbuf = realloc(buf, nbytes + 1);
	if (newbuf == NULL)
	    error("out of memory reading binary string\n");
	buf = newbuf;
	buflen = nbytes + 1;
    }
    if (fread(buf, sizeof(char), nbytes, fp) != nbytes)
	error("a binary string read error occured");
    buf[nbytes] = '\0';
    return buf;
}

static double InRealBinary(FILE * fp)
{
    double x;
    if (fread(&x, sizeof(double), 1, fp) != 1)
	error("a read error occured");
    return x;
}

static Rcomplex InComplexBinary(FILE * fp)
{
    Rcomplex x;
    if (fread(&x, sizeof(Rcomplex), 1, fp) != 1)
	error("a read error occured");
    return x;
}

static SEXP NewBinaryLoad(FILE *fp)
{
    VersionId = 0;
    InInit = DummyInit;
    InInteger = InIntegerBinary;
    InReal = InRealBinary;
    InComplex = InComplexBinary;
    InString = InStringBinary;
    InTerm = DummyTerm;
    return NewDataLoad(fp);
}

#ifndef HAVE_XDR
static void OutIntegerBinary(FILE *fp, int i)
{
    if (fwrite(&i, sizeof(int), 1, fp) != 1)
	error("a binary write error occured");
}

static void OutStringBinary(FILE *fp, char *s)
{
    int n = strlen(s);
    OutIntegerBinary(fp, n);
    if (fwrite(s, sizeof(char), n, fp) != n)
	error("a binary string write error occured");
}

static void OutRealBinary(FILE *fp, double x)
{
    if (fwrite(&x, sizeof(double), 1, fp) != 1)
	error("a write error occured");
}

static void OutComplexBinary(FILE *fp, Rcomplex x)
{
	if (fwrite(&x, sizeof(Rcomplex), 1, fp) != 1)
		error("a write error occured");
}

static void NewBinarySave(SEXP s, FILE *fp)
{
    OutInit = DummyInit;
    OutInteger = OutIntegerBinary;
    OutReal = OutRealBinary;
    OutComplex = OutComplexBinary;
    OutString = OutStringBinary;
    OutSpace = DummyOutSpace;
    OutNewline = DummyOutNewline;
    OutTerm = DummyTerm;
    NewDataSave(s, fp);
}
#endif /* not HAVE_XDR */

/* ----- L o w l e v e l -- X D R -- I / O ----- */

#ifdef HAVE_XDR

static void InInitXdr(FILE *fp)
{
    xdrstdio_create(&xdrs, fp, XDR_DECODE);
}

static void OutInitXdr(FILE *fp)
{
    xdrstdio_create(&xdrs, fp, XDR_ENCODE);
}

static void InTermXdr(FILE *fp)
{
    xdr_destroy(&xdrs);
}

static void OutTermXdr(FILE *fp)
{
    xdr_destroy(&xdrs);
}

static void OutIntegerXdr(FILE *fp, int i)
{
    if (!xdr_int(&xdrs, &i)) {
	xdr_destroy(&xdrs);
	error("an xdr integer data write error occured");
    }
}

static int InIntegerXdr(FILE *fp)
{
    int i;
    if (!xdr_int(&xdrs, &i)) {
	xdr_destroy(&xdrs);
	error("an xdr integer data read error occured");
    }
    return i;
}

static void OutStringXdr(FILE *fp, char *s)
{
    unsigned int n = strlen(s);
    OutIntegerXdr(fp, n);
    if (!xdr_bytes(&xdrs, &s, &n, n)) {
	xdr_destroy(&xdrs);
	error("an xdr string data write error occured");
    }
}

static char *InStringXdr(FILE *fp)
{
    static char *buf = NULL;
    static int buflen = 0;
    unsigned int nbytes = InIntegerXdr(fp);
    if (nbytes >= buflen) {
	char *newbuf = realloc(buf, nbytes + 1);
	if (newbuf == NULL)
	    error("out of memory reading binary string\n");
	buf = newbuf;
	buflen = nbytes + 1;
    }
    if (!xdr_bytes(&xdrs, &buf, &nbytes, nbytes)) {
	xdr_destroy(&xdrs);
	error("an xdr string data write error occured");
    }
    buf[nbytes] = '\0';
    return buf;
}

static void OutRealXdr(FILE *fp, double x)
{
    if (!xdr_double(&xdrs, &x)) {
	xdr_destroy(&xdrs);
	error("an xdr real data write error occured");
    }
}

static double InRealXdr(FILE * fp)
{
    double x;
    if (!xdr_double(&xdrs, &x)) {
	xdr_destroy(&xdrs);
	error("an xdr real data read error occured");
    }
    return x;
}

static void OutComplexXdr(FILE *fp, Rcomplex x)
{
    if (!xdr_double(&xdrs, &(x.r)) || !xdr_double(&xdrs, &(x.i))) {
	xdr_destroy(&xdrs);
	error("an xdr complex data write error occured");
    }
}

static Rcomplex InComplexXdr(FILE * fp)
{
    Rcomplex x;
    if (!xdr_double(&xdrs, &(x.r)) || !xdr_double(&xdrs, &(x.i))) {
	xdr_destroy(&xdrs);
	error("an xdr complex data read error occured");
    }
    return x;
}

static void NewXdrSave(SEXP s, FILE *fp)
{
    OutInit = OutInitXdr;
    OutInteger = OutIntegerXdr;
    OutReal = OutRealXdr;
    OutComplex = OutComplexXdr;
    OutString = OutStringXdr;
    OutSpace = DummyOutSpace;
    OutNewline = DummyOutNewline;
    OutTerm = OutTermXdr;
    NewDataSave(s, fp);
}

static SEXP NewXdrLoad(FILE *fp)
{
    InInit = InInitXdr;
    InInteger = InIntegerXdr;
    InReal = InRealXdr;
    InComplex = InComplexXdr;
    InString = InStringXdr;
    InTerm = InTermXdr;
    return NewDataLoad(fp);
}
#endif /* HAVE_XDR */


/* ----- F i l e -- M a g i c -- N u m b e r s ----- */

static void R_WriteMagic(FILE *fp, int number)
{
    unsigned char buf[5];
    number = abs(number);
    switch (number) {
    case R_MAGIC_ASCII_V1:   /* Version 1 - R Data, ASCII Format */
	strcpy((char*)buf, "RDA1");
	break;
    case R_MAGIC_BINARY_V1:  /* Version 1 - R Data, Binary Format */
	strcpy((char*)buf, "RDB1");
	break;
    case R_MAGIC_XDR_V1:     /* Version 1 - R Data, XDR Binary Format */
	strcpy((char*)buf, "RDX1");
	break;
    default:
	buf[0] = (number/1000) % 10 + '0';
	buf[1] = (number/100) % 10 + '0';
	buf[2] = (number/10) % 10 + '0';
	buf[3] = number % 10 + '0';
    }
    buf[4] = '\n';
    fwrite((char*)buf, sizeof(char), 5, fp);
}

static int R_ReadMagic(FILE *fp)
{
    unsigned char buf[6];
    int d1, d2, d3, d4;
    fread((char*)buf, sizeof(char), 5, fp);
    if (strncmp((char*)buf, "RDA1\n", 5) == 0) {
	return R_MAGIC_ASCII_V1;
    }
    else if (strncmp((char*)buf, "RDB1\n", 5) == 0) {
	return R_MAGIC_BINARY_V1;
    }
    else if (strncmp((char*)buf, "RDX1\n", 5) == 0) {
	return R_MAGIC_XDR_V1;
    }
    /* Intel gcc seems to screw up a single expression here */
    d1 = (buf[3]-'0') % 10;
    d2 = (buf[2]-'0') % 10;
    d3 = (buf[1]-'0') % 10;
    d4 = (buf[0]-'0') % 10;
    return d1 + 10 * d2 + 100 * d3 + 1000 * d4;
}

/* ----- E x t e r n a l -- I n t e r f a c e s ----- */

void R_SaveToFile(SEXP obj, FILE *fp, int ascii)
{
    if (ascii) {
	R_WriteMagic(fp, R_MAGIC_ASCII_V1);
	NewAsciiSave(obj, fp);
    } else {
#ifdef HAVE_XDR
	R_WriteMagic(fp, R_MAGIC_XDR_V1);
	NewXdrSave(obj, fp);
#else
	R_WriteMagic(fp, R_MAGIC_BINARY_V1);
	NewBinarySave(obj, fp);
#endif /* HAVE_XDR */
    }
}

SEXP R_LoadFromFile(FILE *fp, int startup)
{
    DLstartup = startup; /* different handling of errors */
    switch(R_ReadMagic(fp)) {
#ifdef HAVE_XDR
    case R_MAGIC_XDR:
	return(XdrLoad(fp));
#endif /* HAVE_XDR */
    case R_MAGIC_BINARY:
	return(BinaryLoad(fp));
    case R_MAGIC_ASCII:
	return(AsciiLoad(fp));
    case R_MAGIC_BINARY_VERSION16:
	return(BinaryLoadOld(fp, 16));
    case R_MAGIC_ASCII_VERSION16:
	return(AsciiLoadOld(fp, 16));
    case R_MAGIC_ASCII_V1:
	return(NewAsciiLoad(fp));
    case R_MAGIC_BINARY_V1:
	return(NewBinaryLoad(fp));
#ifdef HAVE_XDR
    case R_MAGIC_XDR_V1:
	return(NewXdrLoad(fp));
#endif /* HAVE_XDR */
    default:
	fclose(fp);
	error("restore file corrupted -- no data loaded");
	return(R_NilValue);/* for -Wall */
    }
}

SEXP do_save(SEXP call, SEXP op, SEXP args, SEXP env)
{
/* save(list, file, ascii, oldstyle) */

    SEXP s, t;
    int len, j;
    FILE *fp;

    checkArity(op, args);


    if (TYPEOF(CAR(args)) != STRSXP)
	errorcall(call, "first argument must be a character vector");
    if (!isValidStringF(CADR(args)))
	errorcall(call, "`file' must be non-empty string");
    if (TYPEOF(CADDR(args)) != LGLSXP)
	errorcall(call, "`ascii' must be logical");

    fp = R_fopen(R_ExpandFileName(CHAR(STRING_ELT(CADR(args), 0))), "wb");
    if (!fp)
	errorcall(call, "unable to open file");

    len = length(CAR(args));
    PROTECT(s = allocList(len));

    t = s;
    for (j = 0; j < len; j++, t = CDR(t)) {
	SET_TAG(t, install(CHAR(STRING_ELT(CAR(args), j))));
	SETCAR(t, findVar(TAG(t), R_GlobalContext->sysparent));
	if (CAR(t) == R_UnboundValue)
	    error("Object \"%s\" not found", CHAR(PRINTNAME(TAG(t))));
    }

    R_SaveToFile(s, fp, INTEGER(CADDR(args))[0]);

    UNPROTECT(1);
    fclose(fp);
    return R_NilValue;
}

static void R_LoadSavedData(FILE *fp, SEXP aenv)
{
    SEXP a, ans;
    ans = R_LoadFromFile(fp, 0);

    /* Store the components of the list in the Global Env */
    /* We either replace the existing objects in the Global */
    /* Environment or establish new bindings for them. */
    /* Note that we try to convert old "pairlist" objects */
    /* to new "pairlist" objects. */

    PROTECT(a = ans);
    while (a != R_NilValue) {
#ifdef OLD
	for (e = FRAME(aenv); e != R_NilValue ; e = CDR(e)) {
	    if (TAG(e) == TAG(a)) {
		CAR(e) = CAR(a);
		a = CDR(a);
		CAR(a) = ConvertPairToVector(CAR(a)); /* PAIRLIST conv */
		goto NextItem;
	    }
	}
	e = a;
	a = CDR(a);
	UNPROTECT(1);
	PROTECT(a);
	CDR(e) = FRAME(aenv);
	FRAME(aenv) = e;
	CAR(e) = ConvertPairToVector(CAR(e)); /* PAIRLIST conv */
    NextItem:
	;
#else
        defineVar(TAG(a), ConvertPairToVector(CAR(a)), aenv);
        a = CDR(a);
#endif /* OLD */
    }
    UNPROTECT(1);
}

SEXP do_load(SEXP call, SEXP op, SEXP args, SEXP env)
{
    SEXP fname, aenv;
    FILE *fp;

    checkArity(op, args);

    if (!isValidString(fname = CAR(args)))
	errorcall (call, "first argument must be a file name\n");

    /* GRW 1/26/99 GRW : added environment parameter so that */
    /* the loaded objects can be placed where desired  */

    aenv = CADR(args);
    if (TYPEOF(aenv) != ENVSXP && aenv != R_NilValue)
	error("invalid envir argument");

    /* Process the saved file to obtain a list of saved objects. */
    fp = R_fopen(R_ExpandFileName(CHAR(STRING_ELT(fname, 0))), "rb");
    if (!fp)
	errorcall(call, "unable to open file");
    R_LoadSavedData(fp, aenv);
    fclose(fp);
    return R_NilValue;
}
