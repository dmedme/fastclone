/*
 * fastclone.c - a new approach to script generation that makes better use
 * of the machine resources that are available today.
 ***********************************************************************
 * Parameters
 * 1 - Name of seed script
 * 2 - The PID
 * 3 - The bundle
 * 4 - Number of users
 * 5 - Number of transactions each will do
 * 6 - Whether or not variable length substitutions are allowed
 * 7 - Event Wait Time
 * 8 - Whether or not data values can be re-used
 * Options:
 * -c  Simply output the numbers of records required from each file.
 ***********************************************************************
 * The original scripts streamed each file in turn through a multiple-sed
 * pipeline. The approach uses very little memory, but masses of CPU.
 *
 * Furthermore, data re-use was a problem. The original design attempted to
 * spread the input data over a data file, by starting each script from a
 * different place in the data file, recording the used values, then deleting
 * the used values from the data files afterwards, but there were three issues:
 * -   The spread was carried out without knowledge of how many records each
 *     script actually used. Collisions would happen even when the data
 *     files were very large, necessitating changes to the script generation
 *     logic client by client.
 * -   It was tricky to work out which data values would actually get
 *     used; this made designing complex data interdependencies between scripts
 *     difficult.
 * -   The spread logic tended not to avoid collisions when the values required
 *     were a significant fraction of the values available. Assigning, say, 200
 *     users to 200 scripts was most easily accomplished with a post-hoc edit
 *     script. 
 *
 * This version:
 * -    Works a bundle at a time
 * -    Reads the entire script file in to memory
 * -    Reads the def file, if there is one
 * -    Works out how many records are needed from each data file,
 *      and pre-reads them.
 * -    Constructs a write control structure, listing the file fragments and
 *      lengths, and where replacement values come from.
 * -    Writes all the scripts
 * -    Writes out spent data
 * -    If data values can be re-used, appends the used values to the back of
 *      the original file.
 ***********************************************************************
 * Actually, we can do even better than this. If we do the whole scenario at
 * once, we can count up the records needed for all the scripts, and extract
 * them all in one go. But then we might struggle if we have to do thousands
 * of scripts.
 */
static char * sccs_id =  "@(#) $Name$ $Id$\n\
Copyright (c) E2 Systems Limited 1993\n";
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#ifndef LCC
#include <unistd.h>
#endif
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include "e2conv.h"
#include "bmmatch.h"
#include "e2dfflib.h"
static char * path_home;
static char * path_ext;
/*
 * The original script version provided values for def file references that:
 * -   Weren't in the data file (column or datafile not known)
 * -   Just needed values (the 'C' option)
 * I now think that was misconceived, or perhaps insufficiently sophisticated.
 * Don't do it for now
 */
#ifndef LINUX
char * strdup();
#endif
extern int optind;
extern char * optarg;
/*
 * Structure that controls the writing out process
 */
struct write_control {
/*
 * The script file is the anchor for all the pieces to be written. It starts
 * off with the whole file in one piece, but the chain of pieces gets extended
 * as different locations are identified
 */
   struct file_control script_file;
/*
 * Where the def file is co-ordinated from
 */
   struct file_control def_file;
/*
 * Keeps track of each data file.
 */
   struct file_control * data_anchor;
   int var_flag;               /* Whether length changes are allowed or not */
};
/*
 * Track data files
 */
struct file_control * track_data_file(wcp, def_rp)
struct write_control * wcp;
struct row * def_rp;
{
struct file_control * fcp;
struct file_control * fcp1;
char * def_fname;

    def_fname = (char *) malloc(strlen(path_home) + strlen(def_rp->colp[2])
                  + 10);
    sprintf(def_fname, "%s/data/%s.db", path_home, def_rp->colp[2]);
/*
 * See if we have already encountered this data file
 */
    for (fcp = wcp->data_anchor;
            fcp != NULL
         && strcmp(fcp->fname, def_fname);
                fcp = fcp->next_file);
/*
 * If we have already seen this data file, and this entry is an F, increment
 * the rows.
 */
    if (fcp != NULL)
    {
        if (def_rp->colp[4][0] == 'F')
            fcp->content.data.recs += 1;
        free(def_fname);
        return fcp;
    }
/*
 * Otherwise, we need to allocate a new file_control structure for the data
 * file. We don't do anything with it at this stage.
 */
    if ((fcp = (struct file_control *) malloc(sizeof(struct file_control)))
                 == NULL)
        return NULL;
    memset(fcp, 0, sizeof(struct file_control));
    fcp->fname = def_fname;
    if (wcp->data_anchor == NULL)
        wcp->data_anchor = fcp;
    else
    {
        for (fcp1 = wcp->data_anchor;
                fcp1->next_file != NULL;
                    fcp1 = fcp1->next_file);
        fcp1->next_file = fcp;
    }
    fcp->content.data.recs = (def_rp->colp[4][0] == 'F') ? 2 : 1;
    return fcp;
}
/*
 * Functions for writing out scripts etc.
 */
void write_script_frag(ofp, pp)
FILE * ofp;
struct piece * pp;
{
    fwrite(pp->p, sizeof(char), pp->len, ofp);
    return;
}
/*
 * Functions for writing out scripts etc.
 */
void write_sub_frag(ofp, pp)
FILE * ofp;
struct piece * pp;
{
    if (pp->p[0] == 'F')
    {
        pp->fcp->content.data.cur_row++;
        if (pp->fcp->content.data.cur_row >= pp->fcp->content.data.recs)
            pp->fcp->content.data.cur_row = 0;
    }
/*
 * We have not preserved the original length of the substitution, so this
 * program does not honour the 'No variable length substitution' setting.
 */ 
    if (pp->len < pp->fcp->content.data.col_defs->cols)
        fputs(pp->fcp->content.data.rows[pp->fcp->content.data.cur_row]->colp[
                      pp->len], ofp);
    return;
}
/*
 * Read a script file in to memory
 */
int get_script(fcp)
struct file_control * fcp;
{
struct stat path_stat;
FILE * fp;

    if (stat(fcp->fname, &path_stat) < 0)
    {
        fprintf(stderr, "Script file %s error\n", fcp->fname);
        perror("stat() on file failed");
        return 0;     /* Cannot open file */
    }
    if (!S_ISREG(path_stat.st_mode))
    {
        fprintf(stderr, "Script file %s is not a regular file\n", fcp->fname);
        return 0;
    }
    fcp->content.piece_anchor = (struct piece *) malloc(sizeof(struct piece));
    memset(fcp->content.piece_anchor, 0, sizeof(struct piece));
    fcp->content.piece_anchor->len = path_stat.st_size;
    if ((fcp->content.piece_anchor->p =
          (char *) malloc( path_stat.st_size)) == NULL)
    {
        fprintf(stderr, "Could not allocate %u to read file %s\n",
                   path_stat.st_size, fcp->fname);
        return 0;
    }
    if ((fp = fopen(fcp->fname, "rb")) == NULL)
    {
        fprintf(stderr, "Script file %s fopen() error\n", fcp->fname);
        perror("fopen()");
        free(fcp->content.piece_anchor->p);
        return 0;     /* Cannot open file */
    }
    fread(fcp->content.piece_anchor->p, sizeof(char), path_stat.st_size, fp);
    fclose(fp);
    fcp->content.piece_anchor->write_fun = write_script_frag;
    return 1;
}
/*
 * Comparator for pieces to get them in location order, and within location
 * length (that is, the longest matches are favoured).
 */
int piece_comp(s1, s2)
void * s1;
void * s2;
{
struct piece* r1 = *((struct piece**) s1);
struct piece* r2 = *((struct piece**) s2);

    if (r1->p < r2->p)
        return -1;
    else
    if (r1->p > r2->p)
        return 1;
    else
    {
        if (r1->len > r2->len)
            return -1;
        else
        if (r1->len > r2->len)
            return -1;
        else
            return 0;
    }
}
/*
 * Comparator for the def file line numbers
 */
int def_comp(s1, s2, not_used)
void * s1;
void * s2;
void * not_used;
{
/* struct row* r1 = *((struct row**) s1);
 * struct row* r2 = *((struct row**) s2);
 */
struct row* r1 = ((struct row*) s1);
struct row* r2 = ((struct row*) s2);
int l1;
int l2;

    l1 = atoi(r1->colp[0]);
    l2 = atoi(r2->colp[0]);
#ifdef DEBUG
    fprintf(stderr, "%x (%s, %d) - %x (%s, %d)\n", (long) r1, r1->colp[0], l1, 
     (long) r2, r2->colp[0], l2);
#endif
    if (l1 < l2)
        return -1;
    else
    if (l2 > l1)
        return 1;
    else
        return 0;
}
/*
 * Read the def file in to memory
 * -    Open the file
 * -    We know the columns ...
 * -    Read the entire file
 */
int get_def(fcp)
struct file_control * fcp;
{
int i;

    fcp->content.data.col_defs = 
           col_defs("LINE_NO|MATCH|DATA_FILE|COLUMN|DISPOSITION\n");
    if (!get_data(fcp) || fcp->content.data.recs < 1)
        return 0;
#ifdef DEBUG
    fprintf(stderr, "fcp->content.data.recs = %d\n", 
                    fcp->content.data.recs);
#endif
/*
 * The def file needs to be in ascending order of line number
 */
    if (fcp->content.data.recs > 1)
        qeng(fcp->content.data.rows, fcp->content.data.recs, def_comp, NULL);
/*
 * Unbelievable as it may seem, the Microsoft qsort() implementation doesn't
 * sort things into the correct order. So use qeng() instead.
 *
 *      qsort(fcp->content.data.rows, fcp->content.data.recs,
 *             sizeof(struct row *), def_comp);
 */
#ifdef DEBUG
    for (i = 0; i < fcp->content.data.recs; i++)
         fprintf(stderr, "row: %d line: %s\n", i, fcp->content.data.rows[i]->colp[0]);
#endif
    fclose(fcp->fp);
    fcp->fp = NULL;
    return 1;
}
/*
 * Break a piece in to two or three based on a range. Return the piece inserted.
 */
struct piece * update_pieces(pp, begp, endp)
struct piece * pp;
char * begp;
char * endp;
{
char * xp;
char * ep;
int row = 0;
struct piece * npp;
struct piece * nnpp;

    npp = (struct piece *) malloc(sizeof(struct piece));
/*
 * At the beginning
 */
    if (begp == pp->p)
    {
        npp->p = endp + 1;
        npp->len = (pp->p + pp->len) - (endp + 1);
        npp->next_piece = pp->next_piece;
        pp->next_piece = npp;
        pp->p = begp;
        pp->len = (endp - begp) + 1;
        npp->write_fun = pp->write_fun;
        return pp; 
    }
    else
/*
 * At the end
 */
    if (endp >= pp->p + pp->len - 1)
    {
        pp->len = (begp - pp->p);
        npp->next_piece = pp->next_piece;
        pp->next_piece = npp;
        npp->p = begp;
        npp->len = (endp - begp) + 1;
        npp->write_fun = pp->write_fun;
        return npp;
    }
    else
/*
 * In the middle.
 */
    {
        nnpp = (struct piece *) malloc(sizeof(struct piece));
        nnpp->next_piece = pp->next_piece;
        pp->next_piece = npp;
        npp->next_piece = nnpp;
        nnpp->p = endp + 1;
        nnpp->len = (pp->p + pp->len) - (endp + 1);
        npp->p = begp;
        npp->len = (endp - begp) + 1;
        pp->len = begp - pp->p;
        npp->write_fun = pp->write_fun;
        nnpp->write_fun = pp->write_fun;
        return npp;
    }
}
/*
 * Sort out the substitutions that will apply to a single line. This code
 * deals with multiple substitutions, and overlapping substitutions in the
 * following ways.
 * -   When two substitutions overlap, the one that starts first has precedence.
 * -   When two substitutions start from the same point, the longer
 *     substitution takes precedence.
 * -   A substitution never matches a value that has been substituted.
 * -   A single def file line makes a single substitution
 * -   Multiple identical items that need to be substituted on the same line
 *     are handle by repeating the def file line as many times as required. The
 *     logic will apply one to the first, another to the second, until matches
 *     or def file lines applying to this line are exhausted.
 */
static struct piece * sort_out_one_line(wcp, row, npp, xp, ep)
struct write_control * wcp;
int row;
struct piece * npp;
char * xp;
char * ep;
{
int matches = 0;
struct bm_table * sub_match;
char * mp;
struct piece ** spp;
struct piece ** xspp;
struct piece * nnpp;
struct piece * mpp;
int i;
int j;

#ifdef DEBUG
    fprintf(stderr, "Line: %d (%.*s)\n", row, (ep - xp), xp);
#endif
    mpp = NULL;      /* Chain of matches on this line */
    while ( row == atoi(
     wcp->def_file.content.data.rows[wcp->def_file.content.data.cur_row]
                           ->colp[0]))
    {
/*
 * If it does, check the match, and if it does match, construct the re-write
 * instructions and interpose them in the current piece as above.
 *
 * There could be problems with the algorithm that maintains the piece chain if
 * there are multiple matches for the same line.
 * -    What if they are not in order?
 * -    What if they overlap?
 * -    What if there are multiple matches
 * Solution: Find all possible matches, sort them into ascending order, and
 * then pick them off one at a time.
 */
        sub_match = bm_compile(
  wcp->def_file.content.data.rows[wcp->def_file.content.data.cur_row]->colp[1]);
        if ((mp = bm_match(sub_match, xp, ep)) == NULL)
        {
            fprintf(stderr, "User Error: (%s|%s|%s|%s|%s) does not match %s\n",
  wcp->def_file.content.data.rows[wcp->def_file.content.data.cur_row]->colp[0],
  wcp->def_file.content.data.rows[wcp->def_file.content.data.cur_row]->colp[1],
  wcp->def_file.content.data.rows[wcp->def_file.content.data.cur_row]->colp[2],
  wcp->def_file.content.data.rows[wcp->def_file.content.data.cur_row]->colp[3],
  wcp->def_file.content.data.rows[wcp->def_file.content.data.cur_row]->colp[4],
                    wcp->script_file.fname);
        }
        else
        {
            do
            {
                matches++;
                if (mpp == NULL)
                {
                    mpp = (struct piece *) malloc(sizeof(struct piece));
                    mpp->next_piece = NULL;
                }
                else
                {
                    nnpp = (struct piece *) malloc(sizeof(struct piece));
                    nnpp->next_piece = mpp;
                    mpp = nnpp;
                }
/*
 * Provide a back pointer
 */
                mpp->fcp = (struct file_control *) (0x7ffffffL &  
                             wcp->def_file.content.data.cur_row);
                mpp->p = mp; 
                mpp->len = sub_match->match_len; 
            }
            while ((mp = bm_match(sub_match, mp + 1, ep)) != NULL);
        }
        free(sub_match);
        wcp->def_file.content.data.cur_row++;
        if ( wcp->def_file.content.data.cur_row >=
             wcp->def_file.content.data.recs)
        {
/*
 * Signal that no more def file processing is needed
 */
            free(wcp->def_file.fname);
            wcp->def_file.fname = NULL;
            break;
        }
    }
/*
 * If there are matches, find out which ones will actually apply, and to what
 */
    if (matches)
    {
        if (matches == 1)
            spp = &mpp;
        else
        {
            spp = (struct piece **) malloc(matches * sizeof(struct piece *));
            for (xspp = spp,
                 nnpp = mpp;
                     nnpp != NULL;
                         xspp++, nnpp = nnpp->next_piece)
                *xspp = nnpp;
/*
 * Now sort the matches into ascending order of location
 */
            qsort(spp, matches, sizeof(struct piece *), piece_comp);
        }
/*
 * Process the matches
 */ 
        for (xspp = spp, i = 0; i < matches; i++, xspp++)
        {
/*
 * Ignore it if it has already been overtaken
 */
            if ((*xspp)->p > npp->p)
            {
                npp = update_pieces(npp, (*xspp)->p,
                                         (*xspp)->p + (*xspp)->len - 1);
                j = (int) (((long int) (*xspp)->fcp) & 0x7fffffffL);
                                        /* The corresponding data row */
                npp->fcp = (struct file_control *)
                          (wcp->def_file.content.data.rows[j]->rowp);
                if ( npp->fcp->content.data.recs <= 0)
                {
fprintf(stderr, "User Error: data file %s for (%s|%s|%s|%s|%s) cannot supply rows\n",
                    npp->fcp->fname,
                                wcp->def_file.content.data.rows[j]->colp[0],
                                wcp->def_file.content.data.rows[j]->colp[1],
                                wcp->def_file.content.data.rows[j]->colp[2],
                                wcp->def_file.content.data.rows[j]->colp[3],
                                wcp->def_file.content.data.rows[j]->colp[4]);
                }
                else
                {
                    npp->write_fun = write_sub_frag;
                    for (npp->len = 0;
                             npp->len < npp->fcp->content.data.col_defs->cols;
                                 npp->len++)
                        if (!strcmp(
                          npp->fcp->content.data.col_defs->colp[npp->len],
                          (wcp->def_file.content.data.rows[j]->colp[3])))
                            break;
                    if (npp->len >= npp->fcp->content.data.col_defs->cols)
                    {
fprintf(stderr, "User Error: (%s|%s|%s|%s|%s) does not match any of the columns %s in %s\n",
                            wcp->def_file.content.data.rows[j]->colp[0],
                            wcp->def_file.content.data.rows[j]->colp[1],
                            wcp->def_file.content.data.rows[j]->colp[2],
                            wcp->def_file.content.data.rows[j]->colp[3],
                            wcp->def_file.content.data.rows[j]->colp[4],
                                          npp->fcp->content.data.col_defs->rowp,
                                          npp->fcp->fname);
                    }
                    npp->p = wcp->def_file.content.data.rows[j]->colp[4];
/*
 * Now prevent any further matches for this pattern being applied
 */ 
                    for (nnpp = (*xspp)->next_piece; 
                             nnpp != NULL && ((int) (((long int) nnpp->fcp) & 0x7fffffff) == j);
                                 nnpp = nnpp->next_piece)
                        nnpp->p = NULL;
                }
                npp = npp->next_piece;
            } 
        }
/*
 * Free the match list
 */
        for (xspp = spp, i = 0; i < matches; i++, xspp++)
            free(*xspp);
/*
 * If there was a sort buffer allocated, free it
 */                
        if (matches > 1)
            free(spp);
    }
    return npp;
}
/*
 * Complete the data structures that will drive the clone operation.
 */
static void assemble_clone_instructions(wcp, think_time_buf)
struct write_control * wcp;
char * think_time_buf;
{
/*
 * The script file starts as one big piece. We now need to go through the file,
 * marking out:
 * -  \Wn\ substitutions (where the think time is going)
 * -  Def file substitutions, if applicable
 */
int row;
struct piece * npp;
struct piece * nnpp;
struct piece * mpp;
char * xp;
char * ep;

/*
 * Start at the beginning of the script
 * Initialise the def file substitution tracker
 * Initialise the piece we are in on the script.
 * Initialise line counter
 */
    for (xp = wcp->script_file.content.piece_anchor->p,
         ep = strchr(xp, '\n'),
         row = 1,
         npp = wcp->script_file.content.piece_anchor;
         xp < npp->p + npp->len;
              xp = ep + 1, ep = strchr(xp, '\n'), row++)
    {
/*
 * Delineate the line.
 */
        if (ep == NULL)
            ep = npp->p + npp->len - 1;
/*
 * See if it has a W directive on it.
 * If it does, split the current piece, and interpose the W directive writer.
 * This could be before, in, or after, the current piece. The write function
 * is the same as for a fragment of script.
 */
        if (*xp =='\\' && *(xp + 1) == 'W' && *(ep - 1) == '\\')
        {
            npp = update_pieces(npp, xp, ep);
            npp->p = think_time_buf;
            npp->len = strlen(think_time_buf);
            npp = npp->next_piece;
        }
/*
 * Otherwise, see if the line matches the current def file target.
 */
        else
        if (wcp->def_file.fname != NULL && row == atoi(
             wcp->def_file.content.data.rows[wcp->def_file.content.data.cur_row]
                           ->colp[0]))
            npp = sort_out_one_line(wcp, row, npp, xp, ep);
#ifdef DEBUG
        else
        fprintf(stderr, "Script: %d Def: %d Match: (%s)\n", row, 
             wcp->def_file.content.data.cur_row,
             wcp->def_file.content.data.rows[wcp->def_file.content.data.cur_row]->colp[0]);
#endif
    }
    return;
}
/*
 * Connect the lines in the def file to the data files, work out how many
 * records we are going to need from each, and read them unless all we are
 * doing is counting our requirement.
 */
static void collect_needed_data(wcp, mult, count_flag)
struct write_control * wcp;
int mult;
int count_flag;
{
int i;
struct file_control * dfcp;

/*
 * Find the data files for the def file lines
 */
    for (i = 0; i < wcp->def_file.content.data.recs; i++)
    {
/*
 * Before this allocation, rowp points within the single allocation for the
 * row, so it doesn't need to be free()ed.
 */
        wcp->def_file.content.data.rows[i]->rowp = (char *)
                   track_data_file(wcp, wcp->def_file.content.data.rows[i]);
    }
/*
 * Then read in the data files chained to the write_control structure.
 */
    for (dfcp = wcp->data_anchor;
             dfcp != NULL;
                 dfcp = dfcp->next_file)
    {
        dfcp->content.data.recs *= mult;
        if (!count_flag)
            get_data(dfcp);
    }
    return;
}
/*
 * Actually generate the output scripts; nusers files with ntrans transactions
 * in each.
 */
static void do_the_clone(wcp, pid, bundle, nusersp, ntransp)
struct write_control * wcp;
char * pid;
char * bundle;
char * nusersp;
char * ntransp;
{
struct piece * npp;
int nusers;
int ntrans;
int i;
int j;
struct file_control * dfp;

    if (strlen(wcp->script_file.fname) < (strlen(pid) + 8 +
            strlen(nusersp) + strlen(bundle)))
    {
/*
 * Make sure there is space for the longest possible echo file name
 */
        free(wcp->script_file.fname);
        wcp->script_file.fname = (char *) malloc(
           strlen(pid) + 8 + strlen(bundle) + strlen(nusersp));
    }
    nusers = atoi(nusersp);
    ntrans = atoi(ntransp);
    for (i = 0; i < nusers; i++)
    {
        sprintf(wcp->script_file.fname, "echo%s.%s.%d", pid, bundle, i);
        if ((wcp->script_file.fp = fopen(wcp->script_file.fname, "wb")) == NULL)
        {
            fprintf(stderr, "Failed to open %s for write\n",
                                       wcp->script_file.fname);
            perror("fopen()");
            continue;
        }
        for (j = 0; j < ntrans; j++)
        {
            for (npp = wcp->script_file.content.piece_anchor;
                     npp != NULL;
                         npp = npp->next_piece)
                npp->write_fun(wcp->script_file.fp, npp);
/*
 * Bump on all the data files
 */
            for (dfp = wcp->data_anchor; dfp != NULL; dfp = dfp->next_file)
            {
                dfp->content.data.cur_row++;
                if (dfp->content.data.cur_row >= dfp->content.data.recs)
                    dfp->content.data.cur_row = 0;
            }
        }
        fclose(wcp->script_file.fp);
    }
    return;
}
/******************************************************************************
 * Tidy up the data files used to feed the merge
 ******************************************************************************
 * Loop through the data file chain. For each data file used:
 * -    We add any data we have taken to the spent file.
 * -    We write out the rest of the data file to a new version of same
 * -    If we are re-using data, we add our used data to the end of each file
 */
static void final_data_tidy(wcp, reuse_flag)
struct write_control * wcp;
int reuse_flag;
{
char * spent_file_name;
FILE * ofp;
struct file_control * fcp;
char buf[65536];
int i;
int len;

    for (fcp = wcp->data_anchor; fcp != NULL; fcp = fcp->next_file)
    {
        if (fcp->content.data.recs < 1)
            continue;
/*
 * Create spent file name and open spent file
 */
        spent_file_name =(char *) malloc(strlen(fcp->fname) + 7);
        sprintf(spent_file_name, "%s.spent", fcp->fname);
        if ((ofp = fopen(spent_file_name, "ab")) == NULL)
        {
            fprintf(stderr, "Failed to open %s for append\n",
                                       spent_file_name);
            perror("fopen()");
            free(spent_file_name);
            continue;
        }
/*
 * Write out spent records to spent file
 */
        for (i = 0; i < fcp->content.data.recs; i++)
            fwrite(fcp->content.data.rows[i]->rowp, sizeof(char), 
                              fcp->content.data.rows[i]->len, ofp);
        fclose(ofp);
/*
 * Now the new file
 */
        sprintf(spent_file_name, "%s.new", fcp->fname);
        if ((ofp = fopen(spent_file_name, "wb")) == NULL)
        {
            fprintf(stderr, "Failed to open %s for write\n",
                                       spent_file_name);
            perror("fopen()");
            free(spent_file_name);
            continue;
        }
/*
 * Write out the heading
 */
        fwrite(fcp->content.data.col_defs->rowp, sizeof(char),
                      fcp->content.data.col_defs->len, ofp);
/*
 * Now write out the remaining data from the current file
 */
        while ((len = fread(buf, sizeof(char), sizeof(buf), fcp->fp)) > 0)
            fwrite(buf, sizeof(char), len, ofp);
        fclose(fcp->fp);
/*
 * Now, if we can reuse values, put the used back on the end of the file.
 */
        if (reuse_flag)
        {
            for (i = 0; i < fcp->content.data.recs; i++)
                fwrite(fcp->content.data.rows[i]->rowp, sizeof(char), 
                              fcp->content.data.rows[i]->len, ofp);
        }
        fclose(ofp);
/*
 * We could free up the allocated memory, but the program will exit shortly
 * anyway, so why bother ...
 */
        unlink(fcp->fname);                  /* Unlink needed for Windows ... */
        lrename(spent_file_name, fcp->fname);/* Works across devices          */
    }
    return;
}
/***********************************************************************
 * Parameters. Note that making re-use global to all the data files isn't
 * right; it should probably be in the def file, since some data may be
 * re-usable with some scripts but not with others.
 */
static char * usage = "Option -h outputs this message.\n\
Option -c outputs needed record counts rather than doing the clone.\n\
Parameters should be:\n\
 1 - Name of seed script (the directory in $PATH_HOME/scripts)\n\
 2 - The PID (the run id)\n\
 3 - The bundle\n\
 4 - Number of users\n\
 5 - Number of transactions each will do\n\
 6 - Whether or not variable length substitutions are allowed (Y/N) (ignored)\n\
 7 - Event Wait Time (Think Time in seconds)\n\
 8 - Whether or not data values can be re-used (Y/N)\n";
/****************************************************************************
 * Main program starts here
 * VVVVVVVVVVVVVVVVVVVVVVVV
 */
int main(argc, argv)
int argc;
char ** argv;
{
struct write_control wc;
char * fname;
int ntrans;
int nusers;
int reuse_flag;
int count_flag;
int think_time;
int mult;
struct file_control * dfp;
char think_time_buf[15];

    if ((path_home = getenv("PATH_HOME")) == NULL)
    {
        fputs("You must have PATH_HOME in your environment\n", stderr);
        exit(1);
    }
    if ((path_ext = getenv("PATH_EXT")) == NULL)
    {
        fputs("You must have PATH_EXT in your environment\n", stderr);
        exit(1);
    }
/*
 * Look for options
 */
    count_flag = 0;
    memset((unsigned char *) &wc, 0, sizeof(wc));
    while ( ( mult = getopt( argc, argv, "hc" ) ) != EOF )
    {
        switch ( mult )
        {
        case 'c':
            count_flag = 1;
            break;
        case 'h':
        default:
             fputs(usage, stderr);
             exit(1);
        }
    }
/*
 * Validate the arguments
 */
    if (argc - optind < 8)
    {
        fputs("Too few parameters\n", stderr);
        fputs(usage, stderr);
        exit(1);
    }
    if ((nusers = atoi(argv[optind + 3])) < 1)
    {
        fprintf(stderr, "Illegal number of users %s\n", argv[optind + 3]);
        fputs(usage, stderr);
        exit(1);
    }
    if ((ntrans = atoi(argv[optind + 4])) < 1)
    {
        fprintf(stderr, "Illegal number of transactions %s\n", argv[optind + 4]);
        fputs(usage, stderr);
        exit(1);
    }
    if (*argv[optind + 5] == 'Y' || *argv[optind + 5] == 'y')
        wc.var_flag = 1;
    else
    if (*argv[optind + 5] == 'N' || *argv[optind + 5] == 'n')
        wc.var_flag = 0;
    else
    {
        fprintf(stderr,
"Illegal variable length substitution safe indication %s; must by Y or N (or y or n)\n",
                argv[optind + 5]);
        exit(1);
    }
    if ((think_time = atoi(argv[optind + 6])) < 1)
    {
        fprintf(stderr, "Illegal think time %s\n", argv[optind + 6]);
        fputs(usage, stderr);
        exit(1);
    }
/*
 * Construct a piece output control structure to use to patch the think_time
 */
    sprintf(think_time_buf, "\\W%d\\\n", think_time);
    if (*argv[optind + 7] == 'Y' || *argv[optind + 7] == 'y')
        reuse_flag = 1;
    else
    if (*argv[optind + 7] == 'N' || *argv[optind + 7] == 'n')
        reuse_flag = 0;
    else
    {
        fprintf(stderr,
           "Illegal data re-use indication %s; must by Y or N (or y or n)\n",
                argv[optind + 7]);
        fputs(usage, stderr);
        exit(1);
    }
/*
 * Attempt to load the script file. Give up if failed.
 *
 * The script file is the anchor for all the pieces to be written. It starts
 * off with the whole file in one piece, but the chain of pieces gets extended
 * as different locations are identified
 */
    wc.script_file.fname = (char *) malloc(strlen(path_home) +
                            2 * strlen(argv[optind]) +
                            strlen(path_ext) + 12);
    sprintf(wc.script_file.fname, "%s/scripts/%s/%s.%s",
               path_home, argv[optind], argv[optind], path_ext);
    if (!get_script(&wc.script_file))
        exit(1);
/*
 * Attempt to load the def file. A missing def file is not an error.
 */ 
    wc.def_file.fname = (char *) malloc(strlen(path_home) +
                            2 * strlen(argv[optind]) + 15);
    sprintf(wc.def_file.fname, "%s/scripts/%s/%s.def",
               path_home, argv[optind], argv[optind]);
    if (get_def(&wc.def_file))
/*
 * Process the def file, and work out how many records we need from each data
 * file.
 */
        collect_needed_data(&wc, nusers * ntrans, count_flag);
    else
    {
        free(wc.def_file.fname);
        wc.def_file.fname = NULL;
    }
/*
 * If we are just being asked to count the records required, do so and exit
 */
    if (count_flag)
    {
        for (dfp = wc.data_anchor; dfp != NULL; dfp = dfp->next_file)
            printf("%s|%d\n", dfp->fname, dfp->content.data.recs);

        exit(0);
    }
/*
 * Otherwise, we are cloning the script. Create the linkages that will control
 * the merge
 */
    assemble_clone_instructions(&wc, think_time_buf);
/*
 * We now loop through the write control instructions for each output file,
 * and for each transaction in each output file, creating the script output
 * files.
 */
    do_the_clone(&wc, argv[optind + 1], argv[optind + 2], argv[optind + 3],
                      argv[optind + 4]);
/*
 * Write out the spent data and re-write the data files
 */
    final_data_tidy(&wc, reuse_flag);
/*
 * Finish
 */
    exit(0);
}
