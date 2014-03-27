/******
 * rescale.c SC 20091123 
 *  Receive array of floats in [0..65535] and Nvalues
 *  Return offset and scale to convert array to [0..15]
 * gcc rescale.c testrsc.c -o testrsc -lm
 ******/

static char rcsid[] = "$Id: rescale.c,v 1.5 2009/11/25 05:40:49 shami Exp $";

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <time.h>

#define USIGMA 3.5
#define LSIGMA 2.5

#define VERBOSE 0
#define MINNSAMP 1024

int shortintcmp(const void *a, const void *b)
{
  /*
  short int *da = (const short int *) a;
  short int *db = (const short int *) b;
  return (*da > *db) - (*da < *db);
  */

  if (*((short int*)a) > *((short int*)b))
    return 1;
  else
    return -1;
}

void minmax(short int* a, int n, short int* amin, short int* amax)
{
  int ii;
  *amin = a[0];
  *amax = a[0];

  for(ii=1; ii<n; ii++) {
    if(a[ii] > *amax)
      *amax = a[ii];
    else if(a[ii] < *amin)
      *amin = a[ii];
  }
}

int rescale2(short int *datav, int ndata, int nbits, float *offset, float *scale)
{
    short int* datacopy, datamin, datamax, datacopymin, datacopymax;
    float qlow, qhigh, median,s1lo,s1hi;

    int ii, dofull=0;
 
    //qsort takes a long time if there are a lot of samples per row, so use
    //a random subset to find the median and cutoff percentiles instead
    
    if(ndata > MINNSAMP) { 
      
      minmax(datav,ndata,&datamin,&datamax);
      //fprintf(stderr,"Full set: min = %d max = %d\n",datamin,datamax);

      datacopy = (short int *) malloc(MINNSAMP * sizeof(short int));
      if (!datacopy) {
        printf("Error! malloc failed?\n");
        return (-1);
      }
      for(ii=0; ii<MINNSAMP; ii++) 
	datacopy[ii] = datav[(int)((float)rand()/(float)RAND_MAX*ndata)];
      
      minmax(datacopy,MINNSAMP,&datacopymin,&datacopymax);
      //fprintf(stderr,"Subset: min = %d max = %d\n",datacopymin,datacopymax);
      
      if (datamax-datamin > 2*(datacopymax-datacopymin)) {
	//printf("Defaulting to full sort.\n");
	dofull = 1;
	free(datacopy);
      }
    }

    if(ndata <= MINNSAMP || dofull == 1) {
      datacopy = (short int *) malloc(ndata * sizeof(short int));
      if (!datacopy) {
            printf("Error! malloc failed?\n");
        return (-1);
      }
      memcpy(datacopy, datav, ndata * sizeof(short int));
    }
    
    if(ndata > MINNSAMP && dofull == 0)
      ndata = MINNSAMP;

    //printf("Before qsort, ndata: %d\n",ndata);
    qsort(datacopy, ndata, sizeof(datav[0]), shortintcmp);

    /* Now calculate median and percentiles */
    median = (float) datacopy[(int) (0.5 * ndata)];     // Ignore odd-even issue
    s1lo = (float) datacopy[(int) (0.1587 * ndata)];    // since we're approximating
    s1hi = (float) datacopy[(int) (0.8413 * ndata)];    // percentiles anyway.

    qlow = median - LSIGMA * (median - s1lo);
    qlow = (qlow < (float)datacopy[0]) ? (float)datacopy[0] : qlow;

    qhigh = median + USIGMA * (s1hi - median);
    qhigh = (qhigh > (float)datacopy[ndata - 1]) ? (float)datacopy[ndata - 1] : qhigh;

#if(VERBOSE)
    fprintf(stderr, "# Median = %.1f, 1 sigma = %.1f  %.1f, Clip at %.1f  %.1f\n",
            median, s1lo, s1hi, qlow, qhigh);
#endif

    // X(qlow..qhigh) -> Y(0..15);   for 4-bit
    // X = scale*Y + offset; 
    // Y = (X-offset)/scale = (X-qlow)/(qhigh-qlow) * 16.0.

    if (nbits == 4)
        *scale = (qhigh - qlow) / 16.0;
    else                        // nbits = 8
        *scale = (qhigh - qlow) / 256.0;
    *offset = qlow;

    free(datacopy);
    return 0;
}

/******************************/
int floatcmp(const void *a, const void *b)
{
  
  const float *da = (const float *) a;
  const float *db = (const float *) b;
  return (*da > *db) - (*da < *db);
  
  /*
    if (*((float*)a) > *((float*)b))
      return 1;
    else
      return -1;
  */
}

/******************************/
int basicscale(float *datav, int ndata, float *offset, float *scale)
{
    /* No-op. Return default offset and scale values */
    *scale = 4096.0;
    *offset = 2048.0;
    return (0);
}

/******************************/
int rescale(float *datav, int ndata, int nbits, float *offset, float *scale)
{

    float *datacopy;
    float median, s1lo, s1hi, qlow, qhigh;

    datacopy = (float *) malloc(ndata * sizeof(float));
    if (!datacopy) {
        /* malloc apparently failed */
        printf("Error! malloc failed?\n");
        return (-1);
    }

    memcpy(datacopy, datav, ndata * sizeof(float));
    qsort(datacopy, ndata, sizeof(datav[0]), floatcmp);

    /* Now calculate median and percentiles */
    median = datacopy[(int) (0.5 * ndata)];     // Ignore odd-even issue
    s1lo = datacopy[(int) (0.1587 * ndata)];    // since we're approximating
    s1hi = datacopy[(int) (0.8413 * ndata)];    // percentiles anyway.

    qlow = median - LSIGMA * (median - s1lo);
    qlow = (qlow < datacopy[0]) ? datacopy[0] : qlow;

    qhigh = median + USIGMA * (s1hi - median);
    qhigh = (qhigh > datacopy[ndata - 1]) ? datacopy[ndata - 1] : qhigh;

#if(VERBOSE)
    fprintf(stderr, "# Median = %.1f, 1 sigma = %.1f  %.1f, Clip at %.1f  %.1f\n",
            median, s1lo, s1hi, qlow, qhigh);
#endif

    // X(qlow..qhigh) -> Y(0..15);   for 4-bit
    // X = scale*Y + offset; 
    // Y = (X-offset)/scale = (X-qlow)/(qhigh-qlow) * 16.0.

    if (nbits == 4)
        *scale = (qhigh - qlow) / 16.0;
    else                        // nbits = 8
        *scale = (qhigh - qlow) / 256.0;
    *offset = qlow;

    free(datacopy);
    return 0;
}

/******************************/
int medianrescale(float *datav, int ndata, float *offset, float *scale)
{

    float *datacopy;
    float median, s1lo, s1hi, qlow, qhigh;

    datacopy = (float *) malloc(ndata * sizeof(float));
    if (!datacopy) {
        /* malloc apparently failed */
        printf("Error! malloc failed?\n");
        return (-1);
    }

    memcpy(datacopy, datav, ndata * sizeof(float));
    qsort(datacopy, ndata, sizeof(datav[0]), floatcmp);

    /* Now calculate median and percentiles */
    median = datacopy[(int) (0.5 * ndata)];     // Ignore odd-even issue
    s1lo = datacopy[(int) (0.1587 * ndata)];    // since we're approximating
    s1hi = datacopy[(int) (0.8413 * ndata)];    // percentiles anyway.

    qlow = median - LSIGMA * (median - s1lo);
    if (qlow < 0) {
        qlow = 0.0;
    }
    qhigh = median + USIGMA * (s1hi - median);
    if (qhigh > 65536) {
        qhigh = 65536.0;
    }
#if(VERBOSE)
    printf("# Median = %.1f, 1 sigma = %.1f  %.1f, Clip at %.1f  %.1f\n",
           median, s1lo, s1hi, qlow, qhigh);
#endif

    // X(0..65536) -> Y(0..15); X = scale*Y + offset.
    // scale converts clip range to 16 units
    *scale = (qhigh - qlow) / 16.0;
    // offset removes median and adds back scale*16/2
    *offset = median - (*scale) * 8.0;

    free(datacopy);
    return (0);
}
