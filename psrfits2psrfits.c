 #include <stdio.h>
 #include <stdlib.h>
 #include <string.h>
 #include <math.h>
 #include <ctype.h>
 #include <time.h>
 #include "vectors.h"
 #include "psrfits2psrfits_cmd.h"
 #include "psrfits.h"
 #include "rescale.h"

 //If this flag is 1, various parts of the code will get timed
 #define DOTIME 1

 // If this flag is 1, data will be scaled to fit in 4 bits, but will be
 // written out as 8-bit (useful if you want to compare 16-bit and 4-bit data
 // by running it through the same pipeline, e.g. psrfits2fil can handle 
 // 16-bit and 8-bit psrfits, but not 4-bit (and use Sigproc downstream).
 #define DEBUG 0

 #define GB 1073741824
 #define KB 1024

int ndefaults = 0;

 int main(int argc, char *argv[])
 {
     int numfiles, ii, jj, numrows, rownum, ichan, itsamp, datidx;
     int spec_per_row, status, maxrows, tmp;
     unsigned long int maxfilesize;
     float datum, packdatum, maxval, fulltsubint;
     float *offsets, *scales;
     short int **datachunks;
     FILE **infiles;
     struct psrfits pfin, pfout;
     Cmdline *cmd;
     fitsfile *infits, *outfits;
     char outfilename[128], templatename[128], tformstring[16], tdim[16], tunit[16], firstfilenum[16];
     char *pc1, *pc2, *ibeam;
     int first = 1, dummy = 0; //, nclipped;
     short int *inrowdata16;
     time_t t0, t1, t2;
     unsigned char* outrowdata, *inrowdata8;

     if (DOTIME) {
       time(&t0);
     }


     if (argc == 1) {
	 Program = argv[0];
	 usage();
	 exit(1);
     }
     // Parse the command line using the excellent program Clig
     cmd = parseCmdline(argc, argv);
     numfiles = cmd->argc;
     infiles = (FILE **) malloc(numfiles * sizeof(FILE *));

     //Set the max. total size (in bytes) of all rows in an outut file,
     //leaving some room for PSRFITS header
     maxfilesize = (unsigned long int)(cmd->numgb * GB);
     maxfilesize = maxfilesize - 1000*KB;
     //fprintf(stderr,"cmd->numgb: %f maxfilesize: %ld\n",cmd->numgb,maxfilesize);

 #ifdef DEBUG
     showOptionValues();
 #endif

     printf("\n         PSRFITS 16-bit to 4-bit Conversion Code\n");
     printf("         by J. Deneva, S. Ransom, & S. Chatterjee\n\n");

     // Open the input files
     status = 0;  //fits_close segfaults if this is not initialized
     printf("Reading input data from:\n");
     for (ii = 0; ii < numfiles; ii++) {
	 printf("  '%s'\n", cmd->argv[ii]);

	 //Get the file basename and number from command-line argument
	 //(code taken from psrfits2fil)
	 pc2 = strrchr(cmd->argv[ii], '.');      // at .fits
	 *pc2 = 0;               // terminate string
	 pc1 = pc2 - 1;
	 while ((pc1 >= cmd->argv[ii]) && isdigit(*pc1))
	     pc1--;
	 if (pc1 <= cmd->argv[ii]) {     // need at least 1 char before filenum
	     puts("Illegal input filename. must have chars before the filenumber");
	     exit(1);
	 }
	 pc1++;                  // we were sitting on "." move to first digit
	 pfin.filenum = atoi(pc1);
	 pfin.fnamedigits = pc2 - pc1;   // how many digits in filenumbering scheme.
	 //If 1st file, save the number for naming the template 
	 if (ii == 0) {
	   strncpy(firstfilenum,pc1,pfin.fnamedigits);
	   sprintf(templatename, "%s.%s.template.fits",cmd->outfile,firstfilenum);
	 }

	 *pc1 = 0;               // null terminate the basefilename
	 strcpy(pfin.basefilename, cmd->argv[ii]);
	 pfin.initialized = 0;   // set to 1 in  psrfits_open()
	 pfin.status = 0;
	 //(end of code taken from psrfits2fil)

	 //Open the existing psrfits file
	 if (psrfits_open(&pfin, READONLY) != 0) {
	     fprintf(stderr, "error opening file\n");
	     fits_report_error(stderr, pfin.status);
	     exit(1);
	 }

	 fprintf(stderr,"Input file is from %s\n",pfin.hdr.backend);

	 if(pfin.hdr.nbits == cmd->numbits) {
	   fprintf(stderr,"Input data: %d bits. Output requested: %d bits. Nothing to do.\n",pfin.hdr.nbits, cmd->numbits);
	   exit(1);
	 }

	 if(strcmp(pfin.hdr.backend,"pdev") == 0) {
	   //Get the beam number from the file name
	   ibeam = strrchr(cmd->argv[ii], 'b');
	   ibeam = ibeam+1;
	   *(ibeam+1) = 0;  //terminate string
	   //fprintf(stderr,"ibeam: %s\n", ibeam);
	 }

	 // Create the subint arrays
	 if (first) {
	     pfin.sub.dat_freqs = (float *) malloc(sizeof(float) * pfin.hdr.nchan);
	     pfin.sub.dat_weights = (float *) malloc(sizeof(float) * pfin.hdr.nchan);
	     pfin.sub.dat_offsets =
		 (float *) malloc(sizeof(float) * pfin.hdr.nchan * pfin.hdr.npol);
	     pfin.sub.dat_scales =
		 (float *) malloc(sizeof(float) * pfin.hdr.nchan * pfin.hdr.npol);
	     //first is set to 0 after data buffer allocation further below

	     if (DOTIME) {
	       time(&t1);
	       fprintf(stderr, "Initialization took: %.0f s\n",difftime(t1,t0));
	     }
	 }

	 infits = pfin.fptr;
	 spec_per_row = pfin.hdr.nsblk;
	 //fprintf(stderr,"spec_per_row: %d\n",spec_per_row);
	 fits_read_key(infits, TINT, "NAXIS2", &dummy, NULL, &status);
	 pfin.tot_rows = dummy;
	 numrows = dummy;


	 //If dealing with 1st input file, check if a template exists,
	 //and create one if it doesn't. 
	 if (ii == 0) {
	   fits_open_file(&outfits, templatename, READONLY, &status);

	   if(status == 0) {
	     fits_movnam_hdu(outfits, BINARY_TBL, "SUBINT", 0, &status);
	     fits_read_key(outfits, TINT, "NAXIS1", &dummy, NULL, &status);
	     fits_close_file(outfits, &status);  
	   } else {
	     status = 0; //fits_create_file fails if this is not set to zero
	     fits_create_file(&outfits, templatename, &status);

	     //Instead of copying HDUs one by one, move to the SUBINT HDU
	     //and copy all the HDUs preceding it
	     fits_movnam_hdu(infits, BINARY_TBL, "SUBINT", 0, &status);
	     fits_copy_file(infits, outfits, 1, 0, 0, &status);

	     //Copy the SUBINT table header
	     fits_copy_header(infits, outfits, &status);
	     //fprintf(stderr,"After fits_copy_header, status: %d\n", status);
	     fits_flush_buffer(outfits, 0, &status);

	     //Set NAXIS2 in the output SUBINT table to 0 b/c we haven't 
	     //written any rows yet
	     dummy = 0;
	     fits_update_key(outfits, TINT, "NAXIS2", &dummy, NULL, &status);

	     //Edit the NBITS key
	     if (cmd->numbits == 8 || DEBUG) {
	       dummy = 8;
	       fits_update_key(outfits, TINT, "NBITS", &dummy, NULL, &status);
	     } else {
	       fits_update_key(outfits, TINT, "NBITS", &(cmd->numbits), NULL, &status);
	     }

	     //Delete and recreate col 17 for data
	     fits_read_key(outfits, TSTRING, "TDIM17", tdim, NULL, &status);
	     fits_read_key(outfits, TSTRING, "TUNIT17", tunit, NULL, &status);
	     fits_delete_col(outfits, 17, &status);
	     //fprintf(stderr,"after fits_delete_col, status: %d\n", status);
	     fits_flush_buffer(outfits, 0, &status);

	     //Edit the TFORM17 column: # of data bytes per row 
	     //fits_get_colnum(outfits,1,"DATA",&dummy,&status);
	     if (cmd->numbits == 8 || DEBUG) {
	       sprintf(tformstring, "%dB", pfin.hdr.nsblk * pfin.hdr.nchan * pfin.hdr.npol);
	       sprintf(tdim, "(1,%d,%d,%d)", pfin.hdr.nchan, pfin.hdr.npol, pfin.hdr.nsblk);
	     } else {
	       sprintf(tformstring, "%dB", pfin.hdr.nsblk * pfin.hdr.nchan * pfin.hdr.npol * cmd->numbits / 8);
	       sprintf(tdim, "(1,%d,%d,%d)", pfin.hdr.nchan, pfin.hdr.npol, pfin.hdr.nsblk * cmd->numbits / 8);
	     }

	     //fprintf(stderr,"pfin.hdr.nsblk: %d pfin.hdr.nchan: %d pfin.hdr.npol: %d cmd->numbit: %d tformstring: %s\n", pfin.hdr.nsblk, pfin.hdr.nchan, pfin.hdr.npol, cmd->numbits, tformstring);
	     fits_insert_col(outfits, 17, "DATA", tformstring, &status);
	     //fprintf(stderr,"after fits_insert_col, status: %d\n", status);
	     fits_flush_buffer(outfits, 0, &status);

	     fits_update_key(outfits, TSTRING, "TDIM17", tdim, NULL, &status);
	     //fprintf(stderr,"after fits_update_key, status: %d\n", status);
	     fits_flush_buffer(outfits, 0, &status);

	     fits_update_key(outfits, TSTRING, "TUNIT17", tunit, NULL, &status);
	     //fprintf(stderr,"after fits_update_key, status: %d\n", status);
	     fits_flush_buffer(outfits, 0, &status);

	     //Need this to set the max. # of rows in output
	     fits_read_key(outfits, TINT, "NAXIS1", &dummy, NULL, &status);

	     if(strcmp(pfin.hdr.backend,"pdev") == 0) {
	       //Add key for beam number
	       fits_movabs_hdu(outfits, 1, NULL, &status);
	       fits_update_key(outfits, TSTRING, "IBEAM", ibeam, "Beam number for multibeam systems", &status);
	     }

	     fits_close_file(outfits, &status);  

	     if (DOTIME) {
	       time(&t2);
	       fprintf(stderr, "Template construction took %.0f s\n",difftime(t2,t1));
	     }
	   }

	   //fprintf(stderr,"pfin.subint.statbytes_per_subint: %d\n",pfin.sub.statbytes_per_subint );
	   
	   /*
	   for(jj=0; jj<pfin.sub.statbytes_per_subint/2; jj++)
	     {
	       fprintf(stderr,"pfin.subint.stat[%d]: %u\n",jj,pfin.sub.stat[jj]);
	     }
	   */
	   //exit(1);

	   //Set the max # of rows per file, based on the requested 
	   //output file size
	   maxrows = maxfilesize / dummy;
	   //fprintf(stderr,"maxrows: %d dummy: %d pfin.sub.bytes_per_subint: %d\n",maxrows, dummy, pfin.sub.bytes_per_subint);
	   rownum = 0;
	 }

	 while (psrfits_read_subint(&pfin, first) == 0) {
	   fprintf(stderr, "\nWorking on row %d\n", ++rownum);

	   //If this is the first row, store the length of a full subint
	   if (ii == 0 && rownum == 1)
	     fulltsubint = pfin.sub.tsubint;

	   //If this is the last row and it's partial, drop it.
	   //(It's pfin.rownum-1 below because the rownum member of the psrfits struct seems to be intended to indicate at the *start* of what row we are, i.e. a row that has not yet been read. In contrast, pfout.rownum indicates how many rows have been written, i.e. at the *end* of what row we are in the output.)

	   if (pfin.rownum-1 == numrows && fabs(pfin.sub.tsubint - fulltsubint) > pfin.hdr.dt) {
	     fprintf(stderr,
		     "Dropping partial row of length %f s (full row is %f s)\n",
		     pfin.sub.tsubint, fulltsubint);
	     break;
	   }

	   //If we just read in the 1st row, or if we already wrote the last row in the current output file, create a new output file
	   if ((ii == 0 && rownum == 1) || pfout.rownum == maxrows) {
	     //Create new output file from the template
	     pfout.fnamedigits = pfin.fnamedigits;
	     if(rownum == 1)
	       pfout.filenum = pfin.filenum;
	     else
	       pfout.filenum++;

	     sprintf(outfilename, "%s.%0*d.fits", cmd->outfile, pfout.fnamedigits, pfout.filenum);
	     fits_create_template(&outfits, outfilename, templatename, &status);
	     fits_movnam_hdu(outfits, BINARY_TBL, "SUBINT", 0, &status);
	     tmp = rownum -1;
	     fits_update_key(outfits, TINT, "NSUBOFFS", &tmp, NULL, &status);

	     //fprintf(stderr,"After fits_create_template, status: %d\n",status);
	     fits_close_file(outfits, &status);

	     //Now reopen the file so that the pfout structure is initialized
	     pfout.status = 0;
	     pfout.initialized = 0;

	     sprintf(pfout.basefilename, "%s.", cmd->outfile);

	     if (psrfits_open(&pfout, READWRITE) != 0) {
	       fprintf(stderr, "error opening file\n");
	       fits_report_error(stderr, pfout.status);
	       exit(1);
	     }
	     outfits = pfout.fptr;
	     maxval = pow(2, cmd->numbits) - 1;
	     pfout.rows_per_file = maxrows;

	     //fprintf(stderr, "pfout.hdr.npol: %d\n", pfout.hdr.npol);
	     //fprintf(stderr, "maxval1: %f\n", maxval);
	     //fprintf(stderr, "pfout.rows_per_file: %d\n",pfout.rows_per_file);

	     //These are not initialized in psrfits_open but are needed 
	     //in psrfits_write_subint (not obvious what are the corresponding 
	     //fields in any of the psrfits table headers)
	     pfout.hdr.ds_freq_fact = 1;
	     pfout.hdr.ds_time_fact = 1;

	     //Have to set this to 0 to convert Stokes data properly
	     pfout.hdr.onlyI = 0;
	   }

	   //Copy the subint struct from pfin to pfout, but correct 
	  //elements that are not the same 
            pfout.sub = pfin.sub;       //this copies array pointers too
	    //fprintf(stderr,"pfin.sub.dat_scales: %x pfout.sub.dat_scales: %x\n",pfin.sub.dat_scales,pfout.sub.dat_scales);
            pfout.sub.bytes_per_subint =
                pfin.sub.bytes_per_subint * pfout.hdr.nbits / pfin.hdr.nbits;
            pfout.sub.dataBytesAlloced = pfout.sub.bytes_per_subint;
            pfout.sub.FITS_typecode = TBYTE;

            if (first) {
	      
	      //Allocate scaling buffer and output buffer
	      datachunks = malloc(pfout.hdr.nchan * pfout.hdr.npol * sizeof(short int*));
	      for (ichan=0; ichan < pfout.hdr.nchan * pfout.hdr.npol; ichan++) 
		datachunks[ichan] = malloc(spec_per_row * sizeof(short int));
	      
	      scales = gen_fvect(pfout.hdr.nchan);
	      offsets = gen_fvect(pfout.hdr.nchan);
	      outrowdata = gen_bvect(pfout.sub.bytes_per_subint);
	      first = 0;
	    }
	    
	    pfout.sub.data = outrowdata;

	    if(pfin.hdr.nbits == 8) 
	      inrowdata8 = (unsigned char*) pfin.sub.data;
	    else if(pfin.hdr.nbits == 16) 
	      inrowdata16 = (short int *) pfin.sub.data;
	    else {
	      fprintf(stderr,"Unrecognized NBITS in input fits file: %d\n",pfin.hdr.nbits);
	      exit(1);
	    }
	      

	    if (DOTIME) {
	      time(&t1);
	    }

	    // Populate datachunks[] by picking out all time samples for ichan
	    if(pfin.hdr.nbits == 8) 
	      for (itsamp = 0; itsamp < spec_per_row; itsamp++) {
		for (ichan = 0; ichan < pfout.hdr.nchan * pfout.hdr.npol; ichan++) {
		  datachunks[ichan][itsamp] = (short int)inrowdata8[ichan + itsamp * pfout.hdr.nchan * pfout.hdr.npol];
		}
	      }

	    else
	      for (itsamp = 0; itsamp < spec_per_row; itsamp++) {
		for (ichan = 0; ichan < pfout.hdr.nchan * pfout.hdr.npol; ichan++) {
		  datachunks[ichan][itsamp] = inrowdata16[ichan + itsamp * pfout.hdr.nchan * pfout.hdr.npol];
		}
	      }

	    
            // Loop over all the channels:
	    //time(&t1);
            for (ichan = 0; ichan < pfout.hdr.nchan * pfout.hdr.npol; ichan++) {
	      // Compute the statistics here, and put the offsets and scales in
	      // pf.sub.dat_offsets[] and pf.sub.dat_scales[]
	      
	      if (rescale2(datachunks[ichan], spec_per_row, cmd->numbits, &(pfout.sub.dat_offsets[ichan]), &(pfout.sub.dat_scales[ichan])) != 0) {
		  printf("Rescale routine failed!\n");
		  return (-1);
                }
	      
	    }
	    //time(&t2);
	    //fprintf(stderr,"Rescale loop took %.0f s\n",difftime(t2,t1));


	    if(pfin.hdr.nbits == 8) {
	      // Since we have the offset and scale ready, rescale the data:
	      for (itsamp = 0; itsamp < spec_per_row; itsamp++) {
		for (ichan = 0; ichan < pfout.hdr.nchan * pfout.hdr.npol; ichan++) {
		  datum = (pfout.sub.dat_scales[ichan] == 0.0) ? 0.0 : roundf(((float)datachunks[ichan][itsamp] - pfout.sub.dat_offsets[ichan]) / pfout.sub.dat_scales[ichan]);
		
		  if (datum < 0.0) {
		    datum = 0.0;
		    //nclipped++;
		  } else if (datum > maxval) {
		    datum = maxval;
		    //nclipped++;
		  }
		  inrowdata8[ichan + itsamp * pfout.hdr.nchan * pfout.hdr.npol] = (unsigned char)datum;
		} //end loop over ichan
	      } //end loop over itsamp
	    }
	    else {
	      // Since we have the offset and scale ready, rescale the data:
	      for (itsamp = 0; itsamp < spec_per_row; itsamp++) {
		for (ichan = 0; ichan < pfout.hdr.nchan * pfout.hdr.npol; ichan++) {
		  datum = (pfout.sub.dat_scales[ichan] == 0.0) ? 0.0 : roundf(((float)datachunks[ichan][itsamp] - pfout.sub.dat_offsets[ichan]) / pfout.sub.dat_scales[ichan]);
				
		  if (datum < 0.0) {
		    datum = 0.0;
		    //nclipped++;
		  } else if (datum > maxval) {
		    datum = maxval;
		    //nclipped++;
		  }				
		  inrowdata16[ichan + itsamp * pfout.hdr.nchan * pfout.hdr.npol] = (short int) datum;
		} //end loop over ichan
	      } //end loop over itsamp
	    }

            // Then do the conversion and store the
            // results in pf.sub.data[] 

	    if(pfin.hdr.nbits == 8) {
	      if (cmd->numbits == 8 || DEBUG) {
		for (itsamp = 0; itsamp < spec_per_row; itsamp++) {
		  datidx = itsamp * pfout.hdr.nchan * pfout.hdr.npol;
		  for (ichan = 0; ichan < pfout.hdr.nchan * pfout.hdr.npol;
		       ichan++, datidx++) {
		    pfout.sub.data[datidx] = (unsigned char)inrowdata8[datidx];
		  }
		}
	      } else if (cmd->numbits == 4) {
		for (itsamp = 0; itsamp < spec_per_row; itsamp++) {
		  datidx = itsamp * pfout.hdr.nchan * pfout.hdr.npol;
		  for (ichan = 0; ichan < pfout.hdr.nchan * pfout.hdr.npol;
		       ichan += 2, datidx += 2) {
		    //packdatum = inrowdata16[datidx] * 16 + inrowdata16[datidx + 1];
		    packdatum = (inrowdata8[datidx] << 4) | inrowdata8[datidx + 1];
		    pfout.sub.data[datidx / 2] = (unsigned char) packdatum;
		  }
		}
	      } else {
		fprintf(stderr, "Only 4 or 8-bit output formats supported.\n");
		fprintf(stderr, "Bits per sample requested: %d\n", cmd->numbits);
		exit(1);
	      }
	    }
	    else {
	      if (cmd->numbits == 8 || DEBUG) {
		for (itsamp = 0; itsamp < spec_per_row; itsamp++) {
		  datidx = itsamp * pfout.hdr.nchan * pfout.hdr.npol;
		  for (ichan = 0; ichan < pfout.hdr.nchan * pfout.hdr.npol;
		       ichan++, datidx++) {
		    pfout.sub.data[datidx] = (unsigned char)inrowdata16[datidx];
		  }
		}
	      } else if (cmd->numbits == 4) {
		for (itsamp = 0; itsamp < spec_per_row; itsamp++) {
		  datidx = itsamp * pfout.hdr.nchan * pfout.hdr.npol;
		  for (ichan = 0; ichan < pfout.hdr.nchan * pfout.hdr.npol;
		       ichan += 2, datidx += 2) {

		    //packdatum = inrowdata16[datidx] * 16 + inrowdata16[datidx + 1];
		    packdatum = (inrowdata16[datidx] << 4) | inrowdata16[datidx + 1];
		    pfout.sub.data[datidx / 2] = (unsigned char) packdatum;
		  }
		}
	      } else {
		fprintf(stderr, "Only 4 or 8-bit output formats supported.\n");
		fprintf(stderr, "Bits per sample requested: %d\n", cmd->numbits);
		exit(1);
	      }
	    }
	    
	    if (DOTIME) {
	      t2 = time(&t2);
	      fprintf(stderr, "Row conversion took %.0f s\n",difftime(t2,t1));
	      fprintf(stderr, "Nch with outliers: %d\n",ndefaults);
	      ndefaults = 0;
	    }

            // Now write the row. 
            status = psrfits_write_subint(&pfout);
            if (status) {
                printf("\nError (%d) writing PSRFITS...\n\n", status);
                break;
            }

	    //If current output file has reached the max # of rows, close it
	    if (pfout.rownum == maxrows)
	      fits_close_file(outfits, &status);
        }

	 //Close the files
	 fits_close_file(infits, &status);
     }
    
    fits_close_file(outfits, &status);
    
    // Free the structure arrays too...
    for (ichan = 0; ichan < pfout.hdr.nchan * pfout.hdr.npol; ichan++)
      free(datachunks[ichan]);
    free(datachunks);

    free(infiles);
    free(pfin.sub.dat_freqs);
    free(pfin.sub.dat_weights);
    free(pfin.sub.dat_offsets);
    free(pfin.sub.dat_scales);
    free(pfin.sub.data);
    free(pfout.sub.data);

    if(strcmp(pfin.hdr.backend,"pdev") == 0) 
      free(pfin.sub.stat);

    if (DOTIME) {
      time(&t1);
      fprintf(stderr, "\nProgram took %.0f s / %.2f min / %.2f h\n",difftime(t1,t0),difftime(t1,t0)/60.0,difftime(t1,t0)/3600.0);
    }

    return 0;
}
