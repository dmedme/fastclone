/************************************************************************
 * e2dfflib.c - Common support routines for Delimited Flat Files
 * files.
 */
static char * sccs_id =  "@(#) $Name$ $Id$\n\
Copyright (c) E2 Systems Limited 2009\n";
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef AIX
#include <memory.h>
#endif
#include "e2dfflib.h"
#ifndef LINUX
char * strdup();
#endif
/*
 * Bitmap macros
 */
#define E2BIT_TEST(bitmap,index)\
         (*((bitmap) + ((index)/32)) & (1 << ((index) % 32)))
#define E2BIT_SET(bitmap,index)\
         (*((bitmap) + ((index)/32)) |= (1 << ((index) % 32)))
#define E2BIT_CLEAR(bitmap,index)\
         (*((bitmap) + ((index)/32)) &= ~(1 << ((index) % 32)))

static char * FS = "|";
void set_fs(fs)
char * fs;
{
    FS = fs;
    return;
}
char * get_fs()
{
    return FS;
}
/*
 * awk-like input line read routine. If the caller free()s the strdup()'ed
 * line buffer, the pointer to it must be set to NULL. 
 *
 * The separator is only one character.
 *
 * Consecutive delimiters delineate multiple null fields.
 *
 * An escape character is recognised. It is fixed; '\'.
 */
struct in_rec * rec_anal(in_rec)
struct in_rec * in_rec;
{
int i, l;
char *x;
unsigned char * got_to;

    in_rec->fcnt = 0;
    in_rec->fptr[0] = strdup(in_rec->buf);
    if (in_rec->fptr[0][0] == '\0')
        return in_rec;
/*
 * This version, with a single match character, recognises \ as an escape.
 * This is why it is preserved.
 */
    if (FS[1] == '\0')
    {
    char * src = strdup(in_rec->buf);

        x = &in_rec->buf[0];
        in_rec->fptr[1] = nextasc_r(src, *FS, '\\', &got_to, x,
                 &in_rec->buf[sizeof(in_rec->buf)]);
        if (in_rec->fptr[1] == (char *) NULL)
        {
            free(src);
            return in_rec;
        }
        i = 2;
        x += strlen(x);
        *x = '\0';
        x++;
        while (i < 1024 && (in_rec->fptr[i] = nextasc_r(NULL, *FS, 0, &got_to, x,
                 &in_rec->buf[sizeof(in_rec->buf)])) != (char *) NULL)
        {
            x += strlen(x);
            *x = '\0';
            i++;
            x++;
        }
        x -= 2;
        free(src);
    }
    else
    {
        in_rec->fptr[1] = in_rec->buf;
        for (i = 1; i < 1024 && in_rec->fptr[i][0] != '\0';)
        {
            l = strcspn(in_rec->fptr[i],FS);
            in_rec->fptr[i + 1] = in_rec->fptr[i] + l;
            i++;
            if (*(in_rec->fptr[i]) != '\0')
            {
                *(in_rec->fptr[i]) = '\0';
                in_rec->fptr[i]++;
            }
        }
        x = in_rec->fptr[i] - 1;
    }
    in_rec->fcnt = i - 1;
/*
 * Strip any trailing carriage returns and new lines
 */
    while(x >= &in_rec->buf[0] && (*x == '\n' || *x == '\r'))
    {
        *x = '\0';
        x--;
    }
#ifdef DEBUG
    fprintf(stderr, "Line: (%s) Fields: %d\n", in_rec->buf, in_rec->fcnt); 
    for (i = 0; i <= in_rec->fcnt; i++)
        fprintf(stderr, "  %d - (%s)\n", i, in_rec->fptr[i]); 
#endif
    return in_rec;
}
struct in_rec * get_next(in_rec, fp)
struct in_rec * in_rec;
FILE * fp;
{
int i, l;
char *x;
unsigned char * got_to;
char discard[1024];

    in_rec->buf[sizeof(in_rec->buf)-1] = '\0';
    if (fgets(in_rec->buf,sizeof(in_rec->buf) - 1, fp) == (char *) NULL)
        return (struct in_rec *) NULL;
    if (in_rec->fptr[0] != (char *) NULL)
        free(in_rec->fptr[0]);
    if (in_rec->buf[strlen(in_rec->buf) - 1] != '\n')
    {
        do
        {
            discard[sizeof(discard)-1] = '\0';
            if (fgets(discard,sizeof(discard) - 1, fp) == (char *) NULL)
                return (struct in_rec *) NULL;
        }
        while (discard[strlen(discard) - 1] != '\n');
    }
    return rec_anal(in_rec);
}
/*
 * Construct a data row from awk-like input in a single allocation
 * This code doesn't know about column definitions; it will allocate as few or
 * as many as there are.
 */
struct row * new_row(in_rec)
struct in_rec * in_rec;
{
struct row * rp;
int col_len;
int rec_len;
int i;
int cols;
unsigned char * xp;
/*
 * Ignore empty rows
 */
    if (in_rec->fcnt == 0)
        return NULL;
    if ((rp = malloc(sizeof(struct row) + 2* strlen(in_rec->fptr[0]) + 1 +
              (in_rec->fcnt + 1) *sizeof(unsigned char *))) == NULL)
    {
        fputs("Not enough memory ...\n", stderr);
        return NULL;
    }
    rp->cols = in_rec->fcnt;
    rp->colp = (unsigned char **) (rp + 1);
    rp->rowp = (unsigned char *) (rp->colp + rp->cols);
    rp->len = strlen(in_rec->fptr[0]);
    memcpy(rp->rowp, in_rec->fptr[0], rp->len + 1);
    for (i = 1, xp = rp->rowp + rp->len + 1; i <= rp->cols; i++)
    {
        rp->colp[i - 1] = xp; 
        col_len = strlen( in_rec->fptr[i]) + 1;
        memcpy(xp, in_rec->fptr[i], col_len);
        xp += col_len;
    } 
    return rp;
}
/*
 * Create a set of column headings from a string rather than by reading
 * the first line of a file
 */
struct row * col_defs(headings)
char * headings;
{
struct in_rec in_rec;

    strncpy(in_rec.buf, headings, sizeof(in_rec.buf));
    in_rec.buf[sizeof(in_rec.buf) - 1] = '\0';
    rec_anal(&in_rec);
    return new_row(&in_rec);
}
/*
 * Read some number of rows from a delimited file in to memory
 */
void get_rows(fp, rtp)
FILE *fp;
struct row_track * rtp;
{
struct in_rec in_rec;
struct in_rec * cur_rec;
int old_alloc;
int i;

    memset((unsigned char *) &in_rec, 0, sizeof(struct in_rec));
    if (rtp->recs > 0)
        rtp->alloc = rtp->recs;
    else
        rtp->alloc = 128;
    rtp->rows = (struct row **)
                     malloc(sizeof(struct row *)*rtp->alloc);
/*
 * Read in the entire file, or the number of records requested
 */
    for (old_alloc = 0;;)
    {
        for (i = old_alloc; i < rtp->alloc;)
        {
            if ((cur_rec = get_next(&in_rec, fp)) == NULL)
            {
                rtp->recs = i;
                return;
            }
/*
 * Skip corrupt records. Note that def files may have comments in them which
 * we use this logic to skip; therefore too few columns aren't reported as an
 * error. Too many is certainly dodgy, but my test example was like that, so
 * that isn't treated as an error either; we have enough to be getting on with.
 */
            if (in_rec.fcnt < rtp->col_defs->cols)
                continue;
            rtp->rows[i++] = new_row(&in_rec);
        }
        if (rtp->recs > 0)
            return;
        old_alloc = rtp->alloc;
        rtp->alloc = old_alloc + old_alloc;
        rtp->rows = (struct row **)
              realloc(rtp->rows, sizeof(struct row *)*rtp->alloc);
    }
    return;
}
/*
 * Find a column name in the column headings. Note that it is case sensitive.
 */
int col_ind(headings, colname)
struct row * headings;
char * colname;
{
int i = headings->cols - 1;

    while(i >= 0)
    {
        if (!strcmp(colname, headings->colp[i]))
            break;
        i--;
    }
    return i;
}
/*
 * This is somewhere that C++ scores; instance variables would avoid the
 * necessity for a customised sort routine. But we need one anyway for
 * Microsoft ...
 */
int row_comp(row1, row2, inds)
struct row* row1;
struct row* row2;
int * inds;
{
int i;
int ret;

    for (i = 1; i <= inds[0]; i++)
        if ((ret = strcmp(row1->colp[i], row2->colp[i])) != 0)
            return ret;
    return 0;
}
/*****************************************************************************
 * Quick Sort routines that provide extra data to the comparison function.
 *****************************************************************************
 */
static __inline char ** find_any(low, high, match_word, cmpfn, config)
char ** low;
char ** high;
char * match_word;
int (*cmpfn)();                        /* Comparison function (eg. strcmp())  */
void * config;
{
char **guess;
int i;

    while (low <= high)
    {
        guess = low + ((high - low) >> 1);
        i = cmpfn(*guess, match_word, config);
        if (i == 0)
            return guess + 1; 
        else
        if (i < 0)
            low = guess + 1;
        else
            high = guess - 1;
    }
    return low;
}
void qeng(a1, cnt, cmpfn, config)
char **a1;                             /* Array of pointers to be sorted      */
int cnt;                               /* Number of pointers to be sorted     */
int (*cmpfn)();                        /* Comparison function (eg. strcmp())  */
void * config;
{
char * mid;
char * swp;
char **top; 
char **bot; 
char **cur; 
char **ptr_stack[128];
int cnt_stack[128];
int stack_level = 0;
int i, j, l;

    ptr_stack[0] = a1;
    cnt_stack[0] = cnt;
    stack_level = 1;
/*
 * Now Quick Sort the stuff
 */
    while (stack_level > 0)
    {
        stack_level--;
        cnt = cnt_stack[stack_level];
        a1 = ptr_stack[stack_level];
        top = a1 + cnt - 1;
        if (cnt < 4)
        {
            for (bot = a1; bot < top; bot++)
                 for (cur = top ; cur > bot; cur--)
                 {
                     if (cmpfn(*bot, *cur, config) > 0)
                     {
                         mid = *bot;
                         *bot = *cur;
                         *cur = mid;
                     }
                 }
            continue;
        }
        bot = a1;
        cur = a1 + 1;
/*
 * Check for all in order
 */
        while (bot < top && cmpfn(*bot, *cur, config) <= 0)
        {
            bot++;
            cur++;
        }
        if (cur > top)
            continue;
                       /* They are all the same, or they are in order */
        mid = *cur;    /* First out of order value */
        if (cur == top)
        {              /* One needs to be swapped, then we are done? */
            *cur = *bot;   /* This is the highest value */
            *bot = mid;    /* Put the top one back one below */
            cur = bot - 1;
            top = find_any(a1, cur, mid, cmpfn, config);
            if (top == bot)
                continue;  /* We put it in the right place already */
            while (cur >= top)
                *bot-- = *cur--; /* Shuffle up from where it should be */
            *bot = mid;    /* Now it is going in the right place */
            continue;
        }
        if (cnt > 18)
        {
        unsigned char * samples[31];  /* Where we are going to put samples */

            samples[0] = mid; /* Ensure that we do not pick the highest value */
            l = bot - a1;
            if (cnt < (2 << 5))
                i = 3;
            else 
            if (cnt < (2 << 7))
                i = 5;
            else 
            if (cnt < (2 << 9))
                i = 7;
            else 
            if (cnt < (2 << 11))
                i = 11;
            else 
            if (cnt < (2 << 15))
                i = 19;
            else 
                i = 31;
            if (l >= i && cmpfn(a1[l >> 1], a1[l], config) < 0)
            {
                mid = a1[l >> 1];
                bot = a1;
            }
            else
            {
                j = cnt/i;
                for (bot = &samples[i - 1]; bot > &samples[0]; top -= j)
                    *bot-- = *top;
                qeng(&samples[0], i, cmpfn, config);
                cur = &samples[(i >> 1)];
                top = &samples[i - 1];
                while (cur > &samples[0] && !cmpfn(*cur, *top, config))
                    cur--;
                mid = *cur;
                top = a1 + cnt - 1;
                if (cmpfn(*(a1 + l), mid, config) <= 0)
                    bot = a1 + l + 2;
                else
                    bot = a1;
            }
        }
        else
            bot = a1;
        while (bot < top)
        {
            while (bot < top && cmpfn(*bot, mid, config) < 1)
                bot++;
            if (bot >= top)
                break;
            while (bot < top && cmpfn(*top, mid, config) > 0)
                top--;
            if (bot >= top)
                break;
            swp = *bot;
            *bot++ = *top;
            *top-- = swp;
        }
        if (bot == top && cmpfn(*bot, mid, config) < 1)
            bot++;
        if (stack_level > 125)
        {
            qeng(a1, bot - a1, cmpfn, config);
            qeng(bot, cnt - (bot - a1), cmpfn, config);
        }
        else
        {
            ptr_stack[stack_level] = a1;
            cnt_stack[stack_level] = bot - a1;
            stack_level++;
            ptr_stack[stack_level] = bot;
            cnt_stack[stack_level] = cnt - (bot - a1);
            stack_level++;
        }
    }
    return;
}
/*
 * Sort rows
 */
void sort_rows(rtp, sort_order)
struct row_track *rtp;
char * sort_order;
{
int i;
struct row * sort_cols = col_defs(sort_order);
int * sortcon = (int *) malloc(sizeof(int) * (1 + sort_cols->cols));

/*
 * Find the columns in the headings for the array and turn them in to
 * indices.
 */
    sortcon[0] = sort_cols->cols;
    for (i = 1; i <= sort_cols->cols; i++)
        sortcon[i] = col_ind(rtp->col_defs, sort_cols->colp[i - 1]);
/*
 * Now sort the row pointers into the appropriate order
 */
    qeng(rtp->rows, rtp->recs, row_comp, sortcon );
    free(sortcon);
    return;
}
/*
 * Read data in to memory
 * -    Open the file
 * -    Get the columns off the first row, if they haven't already been put
 *      there
 * -    Read the requested number of rows
 */
int get_data(fcp)
struct file_control * fcp;
{
struct in_rec in_rec;
struct in_rec * cur_rec;
int old_alloc;
int i;

    if (!strcmp(fcp->fname, "-"))
        fcp->fp = stdin;
    else
    if ((fcp->fp = fopen(fcp->fname, "rb")) == NULL)
    {
        fprintf(stderr, "Failed to open data file %s\n", fcp->fname);
        perror("fopen()");
        fcp->content.data.recs = 0;
        return 0;
    }
    if (fcp->content.data.col_defs == NULL)
    {
        memset((unsigned char *) &in_rec, 0, sizeof(struct in_rec));
        if ((cur_rec = get_next(&in_rec, fcp->fp)) == NULL)
        {
            fprintf(stderr, "No header line in %s\n", fcp->fname);
            fcp->content.data.recs = 0;
            return 0;
        }
        fcp->content.data.col_defs = new_row(&in_rec);
    }
    get_rows(fcp->fp, &(fcp->content.data));
    return 1;
}
struct file_control * new_data_file_control(fname, prev_fcp)
char * fname;
struct file_control * prev_fcp;
{
struct file_control * fcp = (struct file_control *) calloc(1, sizeof(*fcp));

    fcp->fname = strdup(fname);
    fcp->flen = 0;
    fcp->fp = NULL;
    if (prev_fcp != NULL)
        prev_fcp->next_file = fcp;
    fcp->next_file = NULL;
    fcp->content.data.col_defs = NULL;
    get_data(fcp);
    return fcp;
}
void zap_data_file_control(fcp, prev_fcp)
struct file_control * fcp;
struct file_control * prev_fcp;
{
int i;

    if (prev_fcp != NULL)
        prev_fcp->next_file = fcp->next_file;
    if (fcp->fname != NULL)
        free(fcp->fname);
    if (fcp->content.data.col_defs != NULL)
        free(fcp->content.data.col_defs);
    if (fcp->content.data.rows != NULL)
    {
        for (i = 0; i < fcp->content.data.recs; i++)
            free(fcp->content.data.rows[i]);
        free(fcp->content.data.rows);
    }
    if (fcp->fp != NULL)
        fclose(fcp->fp);
    free(fcp);
    return;
}
int * get_sizes(fcp)
struct file_control * fcp;
{
int i;
int j;
int l;
int * max_sizes = (int *) calloc(fcp->content.data.col_defs->cols, sizeof(int));

    for (i = 0; i < fcp->content.data.recs; i++)
        for (j = 0; j < fcp->content.data.col_defs->cols; j++)
        {
            l = strlen(fcp->content.data.rows[i]->colp[j]);
            if (l > max_sizes[j])
                max_sizes[j] = l; 
        }
    return max_sizes;
}
/*
 * Routines to (help) construct SQL statements corresponding to our flat file
 * records, using the list of column names
 ****************************************************************************
 * Utility for doing useful things with lists of columns
 */
static char * part_list(cdp, frag0, frag1_n, i)
struct row * cdp;
char * frag0;
char * frag1_n;
int i;
{
char * x;
char * buf = (char *) malloc(cdp->len + strlen(frag0) - 2 + 
                       (cdp->cols - 1) * (strlen(frag1_n) - 2) + 1); 

    x = buf + sprintf(buf, frag0, cdp->colp[i]);
    for (i++; i < cdp->cols; i++)
         x = x + sprintf(x, frag1_n, cdp->colp[i]);
    return buf;
}
char * key_where(cdp)
struct row * cdp;
{
    return part_list(cdp, " WHERE %s = ?", " AND %s = ?", 0);
}
char * default_order(cdp)
struct row * cdp;
{
    return part_list(cdp, " ORDER BY %s", ",%s", 0);
}
char * get_open_select_SQL(cdp, tabp)
struct row * cdp;
char * tabp;
{
char * x = part_list(cdp, "SELECT %s", ",%s", 0);
char * buf = (char *) malloc(strlen(x) + strlen(tabp) + 7);

    sprintf(buf, "%s FROM %s", x, tabp);
    free(x);
    return buf;
}
char * create_lookup_SQL(cdp, tabp, keyp)
struct row * cdp;
char * tabp;
struct row * keyp;
{
char * x =  get_open_select_SQL(cdp, tabp);
char * x1 = key_where(keyp);
char * buf = (char *) malloc(strlen(x) + strlen(x1) + 1);

    sprintf(buf, "%s%s", x, x1);
    free(x);
    free(x1);
    return buf;
}
/*
 * To count stuffing
 */
static int countstuff(ip)
char * ip;
{
int i;
int like_flag;

    for (i = 0, like_flag = 0; *ip != '\0'; i++, ip++)
    {
        if (*ip == '\'')
            i++;
        else
        if (*ip == '%' || *ip == '?')
            like_flag = 1;
    }
    if (like_flag)
        return -i;
    else
        return i;
}
static char * quotestuff(op, ip)
char *op;
char *ip;
{

    while (*ip != '\0')
    {
        if (*ip == '\'')
            *op++ = *ip;
        *op++ = *ip++;
    }
    return op;
}
/*
 * unquote.c, e2srclib.c, tabdiff.c and this file have a lot of similar code
 * that ought to be rationalised ...
 */
char * quoterow(row, firstn)
struct row * row;
int firstn;
{
int i;
int len;
int l;
int ret;
char * buf;
char * x;

    for (i = firstn, len = 1 + 3*row->cols; i < row->cols; i++)
    {
        l = countstuff(row->colp[i]);
        if (l < 0)
            len -= l;
        else
            len += l;
    }
    buf = (char *) malloc(len);
    for (x = buf, i = firstn; i < row->cols; i++)
    {
        *x++ = '\'';
        x = quotestuff(x, row->colp[i]);
        *x++ = '\'';
        *x++ = ',';
    }
    x--;
    *x++ = '\n';
    *x = '\0';
    return buf;
}
char * where_fragment(col, mtch)
char * col;
char * mtch;
{
char * buf;
int like_flag = 0;
int stufflen;
char * x;

    if (!strcmp(mtch,"%"))
        return NULL;
    stufflen = countstuff(mtch);
    if (stufflen < 0)
    {
        like_flag = 1;
        stufflen = -stufflen;
    }
    buf = (char *) malloc(stufflen + ((like_flag ? 9 : 6) + strlen(col)));
    x = buf + sprintf(buf, "%s %s '", col, ((like_flag ? "like" : "=")));
    x = quotestuff(x, mtch);
    *x++ = '\'';
    *x = 0;
    return buf;
}
/*
 * Use the contents of a  row_track to build a dynamic where clause with QBE
 */
char * dynamic_where(rtp)
struct row_track * rtp;
{
int len;
int i;
char ** frag_array = (char **) malloc(sizeof(char *) * rtp->col_defs->cols);
char ** xfap;
char * x;
char * buf;

    for (xfap = frag_array, i = 0, len = 3; i < rtp->col_defs->cols; i++, xfap++)
    {
        *xfap = where_fragment(rtp->col_defs->colp[i],
                               rtp->rows[rtp->cur_row]->colp[i]);
        if (*xfap != NULL)
            len += 5 + strlen(*xfap);
    }
    if (len == 3)
        return NULL;  /* Open query */
    buf = (char *) malloc(len);
    strcpy(buf, " WHERE ");
    for (xfap = frag_array, i = 0, x = buf + 7;
             i < rtp->col_defs->cols;
                 i++, xfap++)
    {
         if (xfap != NULL)
         {
             x += sprintf(x, "%s", *xfap);
             free(*xfap);
             break;
         }
    }
    for (; i < rtp->col_defs->cols; i++, xfap++)
         if (xfap != NULL)
         {
             x += sprintf(x," AND %s", *xfap);
             free(*xfap);
         }
    return buf;
}
char * create_delete_SQL(tabp, keyp)
char * tabp;
struct row * keyp;
{
char * x1 = key_where(keyp);
char * buf = (char *) malloc(strlen(tabp) + strlen(x1) + 13);

    sprintf(buf, "DELETE FROM %s%s", tabp, x1);
    free(x1);
    return buf;
}
char * create_custom_update_SQL(tabp, rtp1, rtp2)
char * tabp;
struct row_track * rtp1;
struct row_track * rtp2;
{
char * x;
char * buf;
int i;
int l;
int len;
int chk;
int * bitmap;
/*
 * Return NULL if the two rows are identical
 */
    if (!strcmp(rtp1->rows[rtp1->cur_row]->rowp,
                rtp2->rows[rtp2->cur_row]->rowp))
        return NULL;
    bitmap = (int *) calloc((rtp1->col_defs->cols - 1)/32 +1, sizeof(int));
    l = countstuff(rtp1->rows[rtp1->cur_row]->colp[0]);
    if (l < 0)
        l = -l;
    for (i = 1, len = strlen(tabp) + 22 + strlen(rtp1->col_defs->colp[0]) + l;
              i < rtp1->col_defs->cols; i++)
    {
        if (strcmp(rtp1->rows[rtp1->cur_row]->colp[i], rtp2->rows[rtp2->cur_row]->colp[i]))
        {
            E2BIT_SET(bitmap, i);
            len += 12  + 2*strlen(rtp1->col_defs->colp[i]);
            l = countstuff(rtp1->rows[rtp1->cur_row]->colp[i]);
            if (l < 0)
                len -= l;
            else
                len += l;
            l = countstuff(rtp2->rows[rtp2->cur_row]->colp[i]);
            if (l < 0)
                len -= l;
            else
                len += l;
        }
    }
    buf = (char *) malloc(len);
    x = buf + sprintf(buf, "UPDATE %s SET", tabp);
    for (i = 1, chk = 0; i < rtp1->col_defs->cols; i++)
    {
        if (E2BIT_TEST(bitmap, i))
        {
            x += sprintf(x, "%c%s='", ((chk == 0)?' ':','),
                            rtp1->col_defs->colp[i]);
            x = quotestuff(x, rtp2->rows[rtp2->cur_row]->colp[i]);
            *x++ = '\'';
            chk = 1;
        }
    }
    x += sprintf(x, " WHERE %s=%s",
                   rtp1->col_defs->colp[0],
                   rtp1->rows[rtp1->cur_row]->colp[0]);
    for (i = 1; i < rtp1->col_defs->cols; i++)
    {
        if (E2BIT_TEST(bitmap, i))
        {
            x += sprintf(x, " AND %s='", 
                            rtp1->col_defs->colp[i]);
            x = quotestuff(x, rtp1->rows[rtp1->cur_row]->colp[i]);
            *x++ = '\'';
        }
    }
    *x++ = '\0';
    if (x - buf > len)
        abort();
    free(bitmap);
    return buf;
}
char * create_insert_SQL(tabp, cdp)
char * tabp;
struct row * cdp;
{
char * x = part_list(cdp, "%s", ",%s", 1);
char * x1 = part_list(cdp, "?", ",?", 1);
char * buf = (char *) malloc(strlen(tabp) + strlen(x)+ strlen(x1) + 26);

    sprintf(buf, "INSERT INTO %s (%s) VALUES (%s)", tabp, x, x1);
    free(x);
    free(x1);
    return buf;
}
