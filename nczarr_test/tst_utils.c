#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <netcdf.h>
#include <ncpathmgr.h>
#include <nclist.h>
#include <ncuri.h>

#ifdef HAVE_GETOPT_H
#include <getopt.h>
#endif

#ifdef _WIN32
#include "XGetopt.h"
#endif

#ifdef HAVE_HDF5_H
#include <hdf5.h>
#include <H5DSpublic.h>
#endif

#include "tst_utils.h"

Options* options = NULL;
Metadata* meta = NULL;

NClist* capture = NULL;

static void
CHECKRANK(int r)
{
    if(options->rank == 0)
        options->rank = r;
    else if(r != options->rank) {
        fprintf(stderr,"FAIL: options->rank mismatch\n");
	exit(1);
    }
}

int
getoptions(int* argcp, char*** argvp)
{
    int ret = NC_NOERR;
    int i,c;
    const char* p;

    /* initialize */
    if(options == NULL) {
        if((options = calloc(1,sizeof(Options))) == NULL)
	    {ret = NC_ENOMEM; goto done;}
    }
    /* Set defaults */
    options->mode = 0; /* classic netcdf-3 */

    while ((c = getopt(*argcp, *argvp, "34c:d:e:f:n:m:p:s:D:X:O:")) != EOF) {
	switch(c) {
	case '3':
	    options->mode = 0;
	    break;
	case '4':
	    options->mode = NC_NETCDF4;
	    break;
	case 'c':
	    CHECKRANK(parsevector(optarg,options->chunks));
	    options->flags |= HAS_CHUNKS;
	    break;
	case 'd':
	    CHECKRANK(parsevector(optarg,options->dimlens));
	    options->flags |= HAS_DIMLENS;
	    break;
	case 'e':
	    CHECKRANK(parsevector(optarg,options->count));
	    options->flags |= HAS_COUNT;
	    break;
	case 'f':
	    CHECKRANK(parsevector(optarg,options->start));
	    options->flags |= HAS_START;
	    break;
	case 'p':
	    CHECKRANK(parsevector(optarg,options->stop));
	    options->flags |= HAS_STOP;
	    break;
	case 'm':
	    CHECKRANK(parsevector(optarg,options->max));
	    options->flags |= HAS_MAX;
	    break;
	case 'n':
	    CHECKRANK(atoi(optarg));
	    break;
	case 's':
	    CHECKRANK(parsevector(optarg,options->stride));
	    options->flags |= HAS_STRIDE;
	    break;
	case 'D': 
	    options->debug = (unsigned)atoi(optarg);
	    break;
	case 'O': 
	    for(p=optarg;*p;p++) {
	        switch (*p) {
	        case 'r': options->op = Read; break;
    	        case 'w': options->op = Write; break;
	        case 'W': options->wholevar = 1; break;
    	        case 'o': options->op = Odom; break;
		default: fprintf(stderr,"Unknown operation '%c'\n",*p); exit(1);
	        }
	    } break;
	case 'X': 
	    if(strcmp(optarg,"opt")==0) {
	        options->optimize = 1;
            } else if(strncmp(optarg,"wd",2)==0) {
		options->wdebug = atoi(optarg+2);
	    }
	    break;
	case '?':
	   fprintf(stderr,"unknown option\n");
	   exit(1);
	}
    }

    /* get file argument */
    *argcp -= optind;
    *argvp += optind;

    if(*argcp > 0) {
	char* p = NCdeescape((*argvp)[0]);
        strcpy(options->file,filenamefor(p));
	nullfree(p);
    }

    /* Figure out the FORMATX for this file */
    if(options->file) {
	NCURI* uri = NULL;
	ncuriparse(options->file,&uri);
	if(uri == NULL) { /* not a url */
	    switch (options->mode) {
	    default: /* fall thru to default */
	    case 0: options->formatx = NC_FORMATX_NC3; break;
    	    case NC_NETCDF4: options->formatx = NC_FORMATX_NC4; break;
	    }
	} else {
	    options->formatx = NC_FORMATX_NCZARR; /* assume */
	    ncurifree(uri);
	}
    }

#ifndef _WIN32
    if(options->wdebug) {
	char s[64];
	snprintf(s,sizeof(s),"%u",options->wdebug);
        setenv("NCZ_WDEBUG",s,1);
    }
    if(options->optimize) {
	unsetenv("NCZ_NOOPTIMIZE");
    } else {
        setenv("NCZ_NOOPTIMIZE","1",1);
    }
#endif

    /* Default some vectors */
    if(!(options->flags & HAS_DIMLENS)) {for(i=0;i<NC_MAX_VAR_DIMS;i++) {options->dimlens[i] = 4;}}
    if(!(options->flags & HAS_CHUNKS)) {for(i=0;i<NC_MAX_VAR_DIMS;i++) {options->chunks[i] = 2;}}
    if(!(options->flags & HAS_STRIDE)) {for(i=0;i<NC_MAX_VAR_DIMS;i++) {options->stride[i] = 1;}}

    /* Computed Defaults */
    if((options->flags & HAS_COUNT) && (options->flags & HAS_STOP)) {
        fprintf(stderr,"cannot specify both count and stop\n");
	ERR(NC_EINVAL);
    }
    if(!(options->flags & HAS_COUNT) && !(options->flags & HAS_STOP)) {
        for(i=0;i<options->rank;i++)
            options->count[i] = (options->dimlens[i]+options->stride[i]-1)/options->stride[i];
    }
    if((options->flags & HAS_COUNT) && !(options->flags & HAS_STOP)) {
        for(i=0;i<options->rank;i++)
            options->stop[i] = (options->count[i] * options->stride[i]);
    }
    if(!(options->flags & HAS_COUNT) && (options->flags & HAS_STOP)) {
        for(i=0;i<options->rank;i++)
            options->count[i] = ((options->stop[i]+options->stride[i]-1) / options->stride[i]);
    }	
    
    if(!(options->flags & HAS_MAX)) {for(i=0;i<NC_MAX_VAR_DIMS;i++) {options->max[i] = options->stop[i];}}

    if(options->debug) {
#ifdef USE_HDF5
        H5Eset_auto2(H5E_DEFAULT,(H5E_auto2_t)H5Eprint,stderr);
#endif
    }

done:
    return ret;
}

int
getmetadata(int create)
{
    int ret = NC_NOERR;
    char dname[NC_MAX_NAME];
    int i;

    if(meta == NULL) {
	if((meta = calloc(1,sizeof(Metadata)))==NULL)
	    {ret = NC_ENOMEM; goto done;}
	/* Non-zero defaults */
	meta->fill = -1;
    }

    if(create) {
        if((ret = nc_create(options->file,options->mode,&meta->ncid))) goto done;
        for(i=0;i<options->rank;i++) {
            snprintf(dname,sizeof(dname),"d%d",i);
            if((ret = nc_def_dim(meta->ncid,dname,options->dimlens[i],&meta->dimids[i]))) goto done;
        }
        if((ret = nc_def_var(meta->ncid,"v",NC_INT,options->rank,meta->dimids,&meta->varid))) goto done;
        if((ret = nc_def_var_fill(meta->ncid,meta->varid,0,&meta->fill))) goto done;
        if(options->formatx == NC_FORMATX_NC4 || options->formatx == NC_FORMATX_NCZARR) {
            if((ret = nc_def_var_chunking(meta->ncid,meta->varid,NC_CHUNKED,options->chunks))) goto done;
        }
        if((ret = nc_enddef(meta->ncid))) goto done;
    } else {/*Open*/
        if((ret = nc_open(options->file,options->mode,&meta->ncid))) goto done;
        for(i=0;i<options->rank;i++) {
            snprintf(dname,sizeof(dname),"d%d",i);
            if((ret = nc_inq_dimid(meta->ncid,dname,&meta->dimids[i]))) goto done;
            if((ret = nc_inq_dimlen(meta->ncid,meta->dimids[i],&options->dimlens[i]))) goto done;
        }
        if((ret = nc_inq_varid(meta->ncid,"v",&meta->varid))) goto done;
    }

done:
    return ret;
}

void
cleanup(void)
{
    if(meta) {
        if(meta->ncid) nc_close(meta->ncid);
    }
    nclistfreeall(capture);
    nullfree(meta);
    nullfree(options);
}

int
parsevector(const char* s0, size_t* vec)
{
    char* s = strdup(s0);
    char* p = NULL;
    int i, done;

    if(s0 == NULL || vec == NULL) abort();

    for(done=0,p=s,i=0;!done;) {
	char* q;
	q = p;
        p = strchr(q,',');
        if(p == NULL) {p = q+strlen(q); done=1;}
        *p++ = '\0';
        vec[i++] = (size_t)atol(q);
    }
    if(s) free(s);
    return i;
}

const char*
printvector(int rank, const size_t* vec)
{
    char s[NC_MAX_VAR_DIMS*3+1];
    int i;
    char* ss = NULL;

    s[0] = '\0';
    for(i=0;i<rank;i++) {
        char e[16];
	snprintf(e,sizeof(e),"%02u",(unsigned)vec[i]);
	if(i > 0) strcat(s,",");
	strcat(s,e);
    }
    if(capture == NULL) capture = nclistnew();
    ss = strdup(s);
    nclistpush(capture,ss);
    return ss;
}

Odometer*
odom_new(size_t rank, const size_t* start, const size_t* stop, const size_t* stride, const size_t* max)
{
     int i;
     Odometer* odom = NULL;
     if((odom = calloc(1,sizeof(Odometer))) == NULL)
	 return NULL;
     odom->rank = rank;
     for(i=0;i<rank;i++) {
	 odom->start[i] = start[i];
	 odom->stop[i] = stop[i];
 	 odom->stride[i] = stride[i];
	 odom->max[i] = (max?max[i]:stop[i]);
         odom->count[i] = (odom->stop[i]+odom->stride[i]-1)/odom->stride[i];
	 odom->index[i] = 0;
     }
     return odom;
}

void
odom_free(Odometer* odom)
{
     if(odom) free(odom);
}

int
odom_more(Odometer* odom)
{
     return (odom->index[0] < odom->stop[0]);
}

int
odom_next(Odometer* odom)
{
     size_t i;
     for(i=odom->rank-1;i>=0;i--) {
	 odom->index[i] += odom->stride[i];
	 if(odom->index[i] < odom->stop[i]) break;
	 if(i == 0) return 0; /* leave the 0th entry if it overflows */
	 odom->index[i] = odom->start[i]; /* reset this position */
     }
     return 1;
}

/* Get the value of the odometer */
size_t*
odom_indices(Odometer* odom)
{
     return odom->index;
}

size_t
odom_offset(Odometer* odom)
{
     size_t offset;
     int i;

     offset = 0;
     for(i=0;i<odom->rank;i++) {
	 offset *= odom->max[i];
	 offset += odom->index[i];
     }
     return offset;
}

const char*
odom_print1(Odometer* odom, int isshort)
{
    static char s[4096];
    static char tmp[4096];
    const char* sv;
    
    s[0] = '\0';
    strcat(s,"{");
    if(!isshort) {
        snprintf(tmp,sizeof(tmp),"rank=%u",(unsigned)odom->rank); strcat(s,tmp);    
        strcat(s," start=("); sv = printvector(odom->rank,odom->start); strcat(s,sv); strcat(s,")");
        strcat(s," stop=("); sv = printvector(odom->rank,odom->stop); strcat(s,sv); strcat(s,")");
        strcat(s," stride=("); sv = printvector(odom->rank,odom->stride); strcat(s,sv); strcat(s,")");
        strcat(s," max=("); sv = printvector(odom->rank,odom->max); strcat(s,sv); strcat(s,")");
        strcat(s," count=("); sv = printvector(odom->rank,odom->count); strcat(s,sv); strcat(s,")");
    }
    snprintf(tmp,sizeof(tmp)," offset=%u",(unsigned)odom_offset(odom)); strcat(s,tmp);
    strcat(s," indices=("); sv = printvector(odom->rank,odom->index); strcat(s,sv); strcat(s,")");
    strcat(s,"}");
    return s;
}

const char*
odom_print(Odometer* odom)
{
    return odom_print1(odom,0);
}

const char*
odom_printshort(Odometer* odom)
{
    return odom_print1(odom,1);
}

static const char* urlexts[] = {"nzf", "nz4", NULL};

const char*
filenamefor(const char* f0)
{
    static char result[4096];
    const char** extp;
    char* p;

    strcpy(result,f0); /* default */
    if(nc__testurl(f0,NULL)) goto done;
    /* Not a URL */
    p = strrchr(f0,'.'); /* look at the extension, if any */
    if(p == NULL) goto done; /* No extension */
    p++;
    for(extp=urlexts;*extp;extp++) {
        if(strcmp(p,*extp)==0) break;
    }
    if(*extp == NULL) goto done; /* not found */
    /* Assemble the url */
    strcpy(result,"file://");
    strcat(result,f0); /* core path */
    strcat(result,"#mode=nczarr,");
    strcat(result,*extp);
done:
    return result;
}
