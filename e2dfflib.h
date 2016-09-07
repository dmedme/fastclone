/************************************************************************
 * e2dfflib.h - Common support routines for Delimited Flat Files
 *
 * We have two views of the files.
 *
 * One is that they are viewed as a series of rows, which are organised into
 * columns; structured data.
 *
 * The other is that they are a series of pieces. This is the approach taken
 * by fastclone when scripts are being merged with data, but it may also be the
 * approach taken when editing arbitrarily large datafiles; the pieces in this
 * case would relate to fragments that have been sent to the browser. 
 *
 * At the moment we don't do this (we send the whole file to the browser) but
 * we may in the future.
 *
 * @(#) $Name$ $Id$ Copyright (c) E2 Systems Limited 2009
 */
#ifndef E2DFFLIB_H
#define E2DFFLIB_H
#include "e2conv.h"
/*************************************************************************
 * Structures used to track files and records
 *************************************************************************
 * A row, consisting of a header, followed by the row and separate columns
 * in a single memory allocation.
 */
struct row {
   int len;
   unsigned char * rowp;
   int cols;
   unsigned char ** colp;
};
/*
 * The header for a collection of rows from a single file.
 */
struct row_track {
    struct row * col_defs;
    int recs;
    int alloc;
    int cur_row;
    struct row ** rows;
};
/*
 * Struct used for tracking things to be written out. We put the function
 * to call and two arguments in the structure.
 */
struct piece {
    unsigned long len;         /* Holds col for data piece     */
    char * p;                  /* Holds def line operand for data piece */
    void (*write_fun)();       /* Function for writing   */ 
    struct file_control * fcp; /* Used with data file    */
    struct piece * next_piece;
};
struct file_control {
    char * fname;
    unsigned int flen;
    FILE * fp;
    struct file_control * next_file;
    union {
       struct piece * piece_anchor;
       struct row_track data;
    } content;
};
/*****************************************************************************
 * awk-inspired input line processing
 *
 * Read in a line and return an array of pointers
 */
struct in_rec {
   int fcnt;
   char * fptr[1024];
   char buf[65536];
};
struct in_rec * get_next();
struct in_rec * rec_anal();
struct row * new_row();
void get_rows();
void sort_rows();
struct row * col_defs();
int get_data();
int * get_sizes();
int col_ind();
void set_fs();
void qeng();
int row_comp();
char * get_fs();
char * key_where();
char * default_order();
char * get_open_select_SQL();
char * create_lookup_SQL();
char * where_fragment();
char * dynamic_where();
char * create_delete_SQL();
char * create_update_SQL();
char * create_insert_SQL();
char * quoterow();
void zap_data_file_control();
#endif
