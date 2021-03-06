#include "aero_interp.h"
#include "quick_select.h"

/******************************************************************************
MODULE:  aerosol_interp_l8

PURPOSE:  Interpolates the L8 aerosol values throughout the image using the
aerosols that were calculated for each NxN window. Also cleans up the fill
pixels in the ipflag.

RETURN VALUE:
Type = N/A

PROJECT:  Land Satellites Data System Science Research and Development (LSRD)
at the USGS EROS

NOTES:
******************************************************************************/
void aerosol_interp_l8
(
    Espa_internal_meta_t *xml_metadata, /* I: XML metadata information */
    int aero_window,   /* I: size of the aerosol window */
    int half_aero_window, /* I: size of half the aerosol window */
    int16 **sband,     /* I/O: input TOA reflectance */
    uint16 *qaband,    /* I: QA band for the input image, nlines x nsamps */
    uint8 *ipflag,     /* I/O: QA flag to assist with aerosol interpolation,
                               nlines x nsamps.  It is expected that the ipflag
                               values are computed for the center of the
                               aerosol windows. */
    float *taero,      /* I/O: aerosol values for each pixel, nlines x nsamps
                          It is expected that the aerosol values are computed
                          for the center of the aerosol windows.  This routine
                          will fill in the pixels for the remaining, non-center
                          pixels of the window. */
    float median_aero, /* I: median aerosol value of clear pixels */
    int nlines,        /* I: number of lines in qaband & taero bands */
    int nsamps         /* I: number of samps in qaband & taero bands */
)
{
    int i;                 /* looping variable for the bands */
    int line, samp;        /* looping variable for lines and samples */
    int curr_pix;          /* current pixel in 1D arrays of nlines * nsamps */
    int center_line;       /* line for the center of the aerosol window */
    int center_line1;      /* line+1 for the center of the aerosol window */
    int center_samp;       /* sample for the center of the aerosol window */
    int center_samp1;      /* sample+1 for the center of the aerosol window */
    int refl_indx = -99;   /* index of band 1 or first band */
    int tmp_percent = 0;   /* current percentage for printing status */
    int curr_tmp_percent;  /* percentage for current line */
    int red_band = 0;      /* red band index */
    int nir_band = 0;      /* NIR band index */
    int aero_pix11;        /* pixel location for aerosol window values
                              [lcmg][scmg] */
    int aero_pix12;        /* pixel location for aerosol window values
                              [lcmg][scmg2] */
    int aero_pix21;        /* pixel location for aerosol window values
                              [lcmg2][scmg] */
    int aero_pix22;        /* pixel location for aerosol window values
                              [lcmg2][scmg2] */
    float xaero, yaero;    /* x/y location for aerosol pixel within the overall
                              larger aerosol window grid */
    float aero11;          /* aerosol value at window line, samp */
    float aero12;          /* aerosol value at window line, samp+1 */
    float aero21;          /* aerosol value at window line+1, samp */
    float aero22;          /* aerosol value at window line+1, samp+1 */
    float u, v;            /* line, sample fractional distance from current
                              pixel (weight applied to furthest line, sample) */
    float one_minus_u;     /* 1.0 - u (weight applied to closest line) */
    float one_minus_v;     /* 1.0 - v (weight applied to closest sample) */
    float one_minus_u_x_one_minus_v;  /* (1.0 - u) * (1.0 - v) */
    float one_minus_u_x_v; /* (1.0 - u) * v */
    float u_x_one_minus_v; /* u * (1.0 - v) */
    float u_x_v;           /* u * v */

    /* Setup NIR and red bands */
    red_band = SR_L8_BAND4;
    nir_band = SR_L8_BAND5;

    /* Use band 1 band-related metadata for the reflectance information for
       Landsat (Level 1 products). If band 1 isn't available then just use the
       first band in the XML file. */
    for (i = 0; i < xml_metadata->nbands; i++)
    {
        if (!strcmp (xml_metadata->band[i].name, "b1") &&
            !strncmp (xml_metadata->band[i].product, "L1", 2))
        {
            /* this is the index we'll use for the band info */
            refl_indx = i;
        }
    }
    if (refl_indx == -99)
        refl_indx = 1;   /* default use second band in XML file */

    /* Interpolate the aerosol data for each pixel location */
    tmp_percent = 0;
    for (line = 0; line < nlines; line++)
    {
        /* update status */
        curr_tmp_percent = 100 * line / nlines;
        if (curr_tmp_percent > tmp_percent)
        {
            tmp_percent = curr_tmp_percent;
            if (tmp_percent % 10 == 0)
            {
                printf ("%d%% ", tmp_percent);
                fflush (stdout);
            }
        }

        /* Determine the line of the representative center pixel in the
           aerosol NxN window array */
        center_line = (int) (line / aero_window) * aero_window +
            half_aero_window;

        /* Determine fractional location of this line in the aerosol window.
           Negative values are at the top of the window. */
        yaero = (float) (line - center_line) / aero_window;
        u = yaero - (int) yaero;

        /* Determine if this pixel is closest to the line below or the line
           above. If the fractional value is in the top part of the aerosol
           window, then use the line above.  Otherwise use the line below. */
        if (u < 0.0)
        {
            center_line1 = center_line - aero_window;

            /* If the aerosol window line value is outside the bounds of the
               scene, then just use the same line in the aerosol window */
            if (center_line1 < 0)
                center_line1 = center_line;
        }
        else
        {
            center_line1 = center_line + aero_window;

            /* If the aerosol window line value is outside the bounds of the
               scene, then just use the same line in the aerosol window */
            if (center_line1 >= nlines-1)
                center_line1 = center_line;
        }

        curr_pix = line * nsamps;
        for (samp = 0; samp < nsamps; samp++, curr_pix++)
        {
            /* If this pixel is fill, then don't process */
            if (level1_qa_is_fill (qaband[curr_pix]))
                continue;

            /* If this pixel is cloud or shadow, then don't process. Use
               median aerosol values.  Flag them separately. */
            else if (is_cloud (qaband[curr_pix]))
            {
                taero[curr_pix] = median_aero;
                ipflag[curr_pix] = (1 << IPFLAG_CLOUD);
                continue;
            }
            else if (is_shadow (qaband[curr_pix]))
            {
                taero[curr_pix] = median_aero;
                ipflag[curr_pix] = (1 << IPFLAG_SHADOW);
                continue;
            }

            /* If this pixel is water, then don't process. Use median aerosol
               values. */
            else if (is_water (sband[red_band][curr_pix],
                               sband[nir_band][curr_pix]))
            {
                taero[curr_pix] = median_aero;
                ipflag[curr_pix] = (1 << IPFLAG_WATER);
                continue;
            }

            /* Determine the sample of the representative center pixel in the
               aerosol NxN window array */
            center_samp = (int) (samp / aero_window) * aero_window +
                half_aero_window;

            /* Determine fractional location of this sample in the aerosol
               window.  Negative values are at the left of the window. */
            xaero = (float) (samp - center_samp) / aero_window;
            v = xaero - (int) xaero;

            /* If the current line, sample are the same as the center line,
               sample, then skip to the next pixel.  We already have the
               aerosol value. */
            if (samp == center_samp && line == center_line)
                continue;

            /* Determine if this pixel is closest to the sample to the left or
               the sample to the right.  If the fractional value is on the left
               side of the aerosol window, then use the sample to the left.
               Otherwise use the sample to the right. */
            if (v < 0.0)
            {
                center_samp1 = center_samp - aero_window;
    
                /* If the aerosol window sample value is outside the bounds of
                   the scene, then just use the same sample in the aerosol
                   window */
                if (center_samp1 < 0)
                    center_samp1 = center_samp;
            }
            else
            {
                center_samp1 = center_samp + aero_window;
    
                /* If the aerosol window sample value is outside the bounds of
                   the scene, then just use the same sample in the aerosol
                   window */
                if (center_samp1 >= nsamps-1)
                    center_samp1 = center_samp;
            }

            /* Determine the four aerosol window pixels to be used for
               interpolating the current pixel */
            aero_pix11 = center_line * nsamps + center_samp;
            aero_pix12 = center_line * nsamps + center_samp1;
            aero_pix21 = center_line1 * nsamps + center_samp;
            aero_pix22 = center_line1 * nsamps + center_samp1;

            /* Get the aerosol values */
            aero11 = taero[aero_pix11];
            aero12 = taero[aero_pix12];
            aero21 = taero[aero_pix21];
            aero22 = taero[aero_pix22];

            /* From here make the fractional distance positive, regardless of
               where it is in the window. */
            u = fabs (u);
            v = fabs (v);

            /* Determine the fractional distance between the integer location
               and floating point pixel location to be used for interpolation */
            one_minus_u = 1.0 - u;
            one_minus_v = 1.0 - v;
            one_minus_u_x_one_minus_v = one_minus_u * one_minus_v;
            one_minus_u_x_v = one_minus_u * v;
            u_x_one_minus_v = u * one_minus_v;
            u_x_v = u * v;

            /* Interpolate the aerosol */
            taero[curr_pix] = aero11 * one_minus_u_x_one_minus_v +
                              aero12 * one_minus_u_x_v +
                              aero21 * u_x_one_minus_v +
                              aero22 * u_x_v;

            /* Set the aerosol to window interpolated. Clear anything else. */
            ipflag[curr_pix] = (1 << IPFLAG_INTERP_WINDOW);

            /* If any of the window pixels used in the interpolation were
               water pixels, then mask this pixel with water (in addition to
               the interpolation bit already set) */
            if (btest (ipflag[aero_pix11], IPFLAG_WATER) ||
                btest (ipflag[aero_pix12], IPFLAG_WATER) ||
                btest (ipflag[aero_pix21], IPFLAG_WATER) ||
                btest (ipflag[aero_pix22], IPFLAG_WATER))
                ipflag[curr_pix] |= (1 << IPFLAG_WATER);
        }  /* end for samp */
    }  /* end for line */

    /* Clean up the ipflag in the center of the NxN windows, for the fill
       pixels. If an NxN window is a mixture of fill and non-fill, the center
       of the window can be flagged as fill and some other QA based on the
       other pixels in that window. At the end, we want fill to be fill. */
    for (curr_pix = 0; curr_pix < nlines * nsamps; curr_pix++)
    {
        if (level1_qa_is_fill (qaband[curr_pix]))
            ipflag[curr_pix] = (1 << IPFLAG_FILL);
    }

    /* Update final status */
    printf ("100%%\n");
    fflush (stdout);
}


/******************************************************************************
MODULE:  aerosol_interp_s2

PURPOSE:  Interpolates the S2 aerosol values throughout the image using the
aerosols that were calculated for each NxN window.

RETURN VALUE:
Type = N/A

PROJECT:  Land Satellites Data System Science Research and Development (LSRD)
at the USGS EROS

NOTES:
******************************************************************************/
void aerosol_interp_s2
(
    int aero_window,   /* I: size of the aerosol window */
    uint8 *ipflag,     /* I/O: QA flag to assist with aerosol interpolation,
                               nlines x nsamps.  It is expected that the ipflag
                               values are computed for the UL of the aerosol
                               windows to match taero. */
    float *taero,      /* I/O: aerosol values for each pixel, nlines x nsamps
                          It is expected that the aerosol values are computed
                          for the UL of the aerosol windows.  This routine
                          will fill in the pixels for the remaining pixels of
                          the window. */
    int nlines,        /* I: number of lines in ipflag & taero bands */
    int nsamps         /* I: number of samps in ipflag & taero bands */
)
{
    int line, samp;    /* looping variable for lines and samples */
    int iline, isamp;  /* looping variable for lines and samples in the
                          aerosol window */
    int curr_pix;      /* current pixel in 1D arrays of nlines * nsamps */
    int curr_win_pix;  /* current pixel in the nxn window for atm corr */
    int next_samp_pix; /* pixel location of the next sample */
    int next_line_pix; /* pixel location of the next line */
    int next_line_samp_pix; /* pixel location of next line and next sample */
    int awline;        /* line for the next aerosol window */
    int awsamp;        /* sample for the next aerosol window */
    int sq_aero_win;   /* square of the aerosol window */
    int awline_iline;  /* awline - iline */
    int iline_line;    /* iline - line */

    /* Use the UL corner of the aerosol windows to interpolate the remaining
       pixels in the window */
    sq_aero_win = aero_window * aero_window;
    for (line = 0; line < nlines; line++)
    {
        /* Determine the current pixel */
        curr_pix = line * nsamps;

        /* Determine the line for the next aerosol window */
        awline = line + aero_window;

        for (samp = 0; samp < nsamps; samp++, curr_pix++)
        {
            /* Determine the next line and next sample to be used for
               interpolating */
            next_samp_pix = line * nsamps + (samp + aero_window);
            next_line_pix = (line + aero_window) * nsamps + samp;
            next_line_samp_pix = (line + aero_window) * nsamps +
                (samp + aero_window);

            /* Determine the sample for the next aerosol window */
            awsamp = samp + aero_window;

            /* Loop through an NxN window with the current pixel being the UL
               corner of the window */
            for (iline = line; iline < awline; iline++)
            {
                /* Skip if this isn't a valid line */
                if (iline >= nlines) continue;

                awline_iline = awline - iline;
                iline_line = iline - line;
                for (isamp = samp; isamp < awsamp; isamp++)
                {
                    /* Skip if this isn't a valid sample */
                    if (isamp >= nsamps) continue;

                    curr_win_pix = iline * nsamps + isamp;
                    taero[curr_win_pix] = taero[curr_pix] *
                        (awline_iline) * (awsamp-isamp);

                    if ((awline < nlines) && (awsamp < nsamps))
                        taero[curr_win_pix] +=
                            (isamp-samp)*(awline_iline) * taero[next_samp_pix] +
                            (awsamp-isamp)*(iline_line) * taero[next_line_pix] +
                            (isamp-samp)*(iline_line) *
                             taero[next_line_samp_pix];

                    else if ((awline >= nlines) && (awsamp < nsamps))
                        taero[curr_win_pix] +=
                            (isamp-samp)*(awline_iline) * taero[next_samp_pix] +
                            (awsamp-isamp)*(iline_line) * taero[curr_pix] +
                            (isamp-samp)*(iline_line) * taero[next_samp_pix];

                    else if ((awline < nlines) && (awsamp >= nsamps))
                        taero[curr_win_pix] +=
                            (isamp-samp)*(awline_iline) * taero[curr_pix] +
                            (awsamp-isamp)*(iline_line) * taero[next_line_pix] +
                            (isamp-samp)*(iline_line) * taero[next_line_pix];

                    else if ((awline >= nlines) && (awsamp >= nsamps))
                        taero[curr_win_pix] +=
                            (isamp-samp)*(awline_iline) * taero[curr_pix] +
                            (awsamp-isamp)*(iline_line) * taero[curr_pix] +
                            (isamp-samp)*(iline_line) * taero[curr_pix];

                    /* Compute the average */
                    taero[curr_win_pix] /= sq_aero_win;
                }
            }

            /* If not one of the representative window pixels, flag as
               interpolated */
            if ((line % aero_window != 0) || (samp % aero_window != 0))
                ipflag[curr_pix] = (1 << IPFLAG_INTERP_WINDOW);
        }  /* for samp */
    }  /* for line */
}


/******************************************************************************
MODULE:  aerosol_fill_median_l8

PURPOSE:  Changes the L8 aerosol window values for water, cloud, and shadow to
be the median aerosol value of the clear pixels.

RETURN VALUE:
Type = N/A

PROJECT:  Land Satellites Data System Science Research and Development (LSRD)
at the USGS EROS

NOTES:
******************************************************************************/
void aerosol_fill_median_l8
(
    uint8 *ipflag,     /* I/O: QA flag to assist with aerosol interpolation,
                               nlines x nsamps.  It is expected that the ipflag
                               values are computed for the center of the
                               aerosol windows. */
    float *taero,      /* I/O: aerosol values for each pixel, nlines x nsamps
                          It is expected that the aerosol values are computed
                          for the center of the aerosol windows. */
    int aero_window,   /* I: size of the aerosol window */
    int half_aero_window, /* I: size of half the aerosol window */
    float median_aero, /* I: median aerosol value of clear pixels */
    int nlines,        /* I: number of lines in ipflag & taero bands */
    int nsamps         /* I: number of samps in ipflag & taero bands */
)
{
    int line, samp;       /* looping variable for lines and samples */
    int curr_pix;         /* current pixel in 1D arrays of nlines * nsamps */

    /* Loop through the center of the NxN window pixels */
    for (line = half_aero_window; line < nlines; line += aero_window)
    {
        curr_pix = line * nsamps + half_aero_window;
        for (samp = half_aero_window; samp < nsamps;
             samp += aero_window, curr_pix += aero_window)
        {
            /* Find cloud, shadow, and water pixels and reset the default
               aerosol value to that of the median aerosol value */
            if (btest (ipflag[curr_pix], IPFLAG_CLOUD) ||
                btest (ipflag[curr_pix], IPFLAG_SHADOW) ||
                btest (ipflag[curr_pix], IPFLAG_WATER))
            {
                taero[curr_pix] = median_aero;
            }
        }
    }
}


/******************************************************************************
MODULE:  aerosol_fill_median_s2

PURPOSE:  Changes the S2 failed aerosol window values (i.e. water, cloud,
shadow, urban, barren, etc.) to be the median aerosol value of the clear
land pixels.

RETURN VALUE:
Type = N/A

PROJECT:  Land Satellites Data System Science Research and Development (LSRD)
at the USGS EROS

NOTES:
******************************************************************************/
void aerosol_fill_median_s2
(
    uint8 *ipflag,     /* I/O: QA flag to assist with aerosol interpolation,
                               nlines x nsamps.  It is expected that the ipflag
                               values are computed for the UL of the aerosol
                               windows. */
    float *taero,      /* I/O: aerosol values for each pixel, nlines x nsamps
                          It is expected that the aerosol values are computed
                          for the UL of the aerosol windows */
    int aero_window,   /* I: size of the aerosol window */
    float median_aero, /* I: median aerosol value of clear pixels */
    int nlines,        /* I: number of lines in ipflag & taero bands */
    int nsamps         /* I: number of samps in ipflag & taero bands */
)
{
    int line, samp;       /* looping variable for lines and samples */
    int curr_pix;         /* current pixel in 1D arrays of nlines * nsamps */

    /* Loop through the UL of the NxN window pixels */
    for (line = 0; line < nlines; line += aero_window)
    {
        curr_pix = line * nsamps;
        for (samp = 0; samp < nsamps;
             samp += aero_window, curr_pix += aero_window)
        {
            /* Find any pixel flagged as failed and reset the default aerosol
               value to that of the median aerosol value */
            if (btest (ipflag[curr_pix], IPFLAG_FAILED))
                taero[curr_pix] = median_aero;
        }
    }
}


/******************************************************************************
MODULE:  find_median_aerosol_l8

PURPOSE:  Finds the median aerosol value for the valid L8 land aerosols.

RETURN VALUE:
Type = float
Value           Description
-----           -----------
zero            Error allocating memory for the aerosol array
non-zero        Median aerosol value of the array

PROJECT:  Land Satellites Data System Science Research and Development (LSRD)
at the USGS EROS

NOTES:
******************************************************************************/
float find_median_aerosol_l8
(
    uint8 *ipflag,     /* I: QA flag to assist with aerosol interpolation,
                             nlines x nsamps.  It is expected that the ipflag
                             values are computed for the center of the aerosol
                             windows. */
    float *taero,      /* I: aerosol values for each pixel, nlines x nsamps
                             It is expected that the aerosol values are computed
                             for the center of the aerosol windows */
    int aero_window,   /* I: size of the aerosol window */
    int half_aero_window, /* I: size of half the aerosol window */
    int nlines,        /* I: number of lines in taero band */
    int nsamps         /* I: number of samps in taero band */
)
{
    char errmsg[STR_SIZE];                         /* error message */
    char FUNC_NAME[] = "find_median_aerosol_l8";   /* function name */
    int line, samp;       /* looping variable for lines and samples */
    int curr_pix;         /* current pixel in 1D arrays of nlines * nsamps */
    int nbclrpix;         /* number of clear aerosol pixels in this array */
    int nwindows;         /* number of NxN windows in the image */
    float median;         /* median clear aerosol value */
    float *aero = NULL;   /* array of the clear aerosol values */

    /* Determine how many NxN windows there are in this array of data */
    nwindows = ceil ((float) nlines / aero_window) *
               ceil ((float) nsamps / aero_window);

    /* Allocate memory for the aerosols in each window */
    aero = calloc (nwindows, sizeof (float));
    if (aero == NULL)
    {
        sprintf (errmsg, "Error allocating memory for clear aerosol array");
        error_handler (true, FUNC_NAME, errmsg);
        return (0.0);
    }

    /* Loop through the NxN center window values and write the clear aerosol
       values to the aerosol array for determining the median */
    nbclrpix = 0;
    for (line = half_aero_window; line < nlines; line += aero_window)
    {
        curr_pix = line * nsamps + half_aero_window;
        for (samp = half_aero_window; samp < nsamps;
             samp += aero_window, curr_pix += aero_window)
        {
            /* Process clear aerosols */
            if (btest (ipflag[curr_pix], IPFLAG_CLEAR))
            {
                aero[nbclrpix] = taero[curr_pix];
                nbclrpix++;
            }  /* if pixel is clear */
        }  /* for samp */
    }  /* for line */

    /* If no clear aerosols were available, then just return a default value */
    if (nbclrpix == 0)
        median = DEFAULT_AERO;
    else
    {
        /* Get the median of the clear pixels */
        median = quick_select (aero, nbclrpix);
    }

    /* Free memory */
    free (aero);

    /* Successful completion */
    return (median);
}


/******************************************************************************
MODULE:  find_median_aerosol_s2

PURPOSE:  Finds the median Sentinel-2 aerosol value for the valid land aerosols.

RETURN VALUE:
Type = float
Value           Description
-----           -----------
zero            Error allocating memory for the aerosol array
non-zero        Median aerosol value of the array

PROJECT:  Land Satellites Data System Science Research and Development (LSRD)
at the USGS EROS

NOTES:
******************************************************************************/
float find_median_aerosol_s2
(
    uint8 *ipflag,     /* I: QA flag to assist with aerosol interpolation,
                             nlines x nsamps.  It is expected that the ipflag
                             values are computed for the UL of the aerosol
                             windows. */
    float *taero,      /* I: aerosol values for each pixel, nlines x nsamps
                             It is expected that the aerosol values are computed
                             for the UL of the aerosol windows */
    int aero_window,   /* I: size of the aerosol window */
    int nlines,        /* I: number of lines in ipflag and taero band */
    int nsamps         /* I: number of samps in ipflag and taero band */
)
{
    char errmsg[STR_SIZE];                         /* error message */
    char FUNC_NAME[] = "find_median_aerosol_s2";   /* function name */
    int line, samp;       /* looping variable for lines and samples */
    int curr_pix;         /* current pixel in 1D arrays of nlines * nsamps */
    int nbclrpix;         /* number of clear aerosol pixels in this array */
    int nwindows;         /* number of NxN windows in the image */
    float median;         /* median clear aerosol value */
    float *aero = NULL;   /* array of the clear aerosol values */

    /* Determine how many NxN windows there are in this array of data */
    nwindows = ceil ((float) nlines / aero_window) *
               ceil ((float) nsamps / aero_window);

    /* Allocate memory for the aerosols in each window */
    aero = calloc (nwindows, sizeof (float));
    if (aero == NULL)
    {
        sprintf (errmsg, "Error allocating memory for clear aerosol array");
        error_handler (true, FUNC_NAME, errmsg);
        return (0.0);
    }

    /* Loop through the NxN UL window values and write the clear aerosol
       values to the aerosol array for determining the median */
    nbclrpix = 0;
    for (line = 0; line < nlines; line += aero_window)
    {
        curr_pix = line * nsamps;
        for (samp = 0; samp < nsamps;
             samp += aero_window, curr_pix += aero_window)
        {
            /* Process clear aerosols */
            if (btest (ipflag[curr_pix], IPFLAG_CLEAR))
            {
                aero[nbclrpix] = taero[curr_pix];
                nbclrpix++;
            }  /* if pixel is clear */
        }  /* for samp */
    }  /* for line */

    /* If no clear aerosols were available, then just return a default value */
    if (nbclrpix == 0)
        median = DEFAULT_AERO;
    else
    {
        /* Get the median of the clear pixels */
        median = quick_select (aero, nbclrpix);
    }

    /* Free memory */
    free (aero);

    /* Successful completion */
    return (median);
}

