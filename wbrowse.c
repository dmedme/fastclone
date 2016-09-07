/*
 * wbrowse.c - Edit flat files in a structured way with a web browser
 ****************************************************************************
 * Our standard data files are | delimited ASCII files, with the column names
 * on the first line.
 *
 * We need provision for passing the columns on the command line (for DEF files,
 * for instance).
 *
 * This program is to provide a Web interface for editing such files.
 *
 * We also want to use it for editing Property files, which are structured as
 * files of shell variable assignments.
 *
 * It is to be utterly generic; it will work on any file of the requisite
 * format. More generally, it should be able, via the PATH Dictionary compiled
 * for the Rails adventure, to be able to edit any structured file, with
 * provision for animal-specific dialogues along the way.
 *
 * It might also host the debugger, growing the document as the session
 * progressed. 
 *
 * We allow for a domain dictionary, that could be language-specific, or even
 * file specific (pass as an argument), which consists of names and a variable
 * number of labels.
 *
 * We want to support Query By Example.
 *
 * For application development purposes, we would include columns from other
 * tables, block co-ordination etc. etc..
 *
 * Appropriate for an application, but since we are firstly trying to provide a
 * more convenient interface to anonymous data files, probably provision for
 * cross-domain global edits is more useful.
 *
 * The domain dictionary could infer things to use as explanations for codes.
 * But again, this is extending into the application arena.
 *
 * If successful, we replace almost all the existing status tracking based on
 * presence in directories etc. etc. with conventional tables.
 *
 * We brings us to the thorny issue of SQL. The 'Document' for everything is
 * best for personal productivity, but doesn't lend itself so well to team-based
 * operation.
 *
 * Without SQL, we have to do generational updates; serial merging.   
 *
 * To begin with, do the whole file at once? Appropriate for use on property
 * lists etc.
 *
 * In keeping with our design choice of 'right way round', the program puts
 * up a web-page and deals with the response. It would be the responsibility of
 * the shared library that hooked it in to Apache or IIS to route the response
 * back to it, and to work out whether it had been orphaned, and if so that it
 * needed to be told to go away.
 */
static char * sccs_id =  "@(#) $Name$ $Id$\n\
Copyright (c) E2 Systems Limited 2009\n";
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "e2dfflib.h"
#include "hashlib.h"
extern int optind;
extern char * optarg;
/*
 * Structure for managing a dictionary of domain details, so that
 * improved domain descriptions for input variables can be looked up.
 */
struct dict_con {
    struct dv * anchor;         /* Anchor for chain of member variables      */
    HASH_CON * dvt;             /* Hash table of member domain variables     */
};
/*
 * Structure for recording domain values in a dictionary
 */
struct dv {
    struct dv * next_dv;
    char * dname;
    char * dbrief;
    char * dlong;
    int (*dom_val)();    /* Domain validation; reserved for future use */
};
/***************************************************************************
 * Allocate a dictionary, to remember data type details.
 */
struct dict_con * new_dict(cnt)
int cnt;                /* Anticipated number of inhabitants */
{
struct dict_con * x;

    if ((x = (struct dict_con *) malloc(sizeof(*x)))
             == (struct dict_con *) NULL)
        return x;
    x->dvt = hash(cnt,string_hh,
          (COMPFUNC) strcasecmp);    /* Bind Variable Hash Table */
    x->anchor = (struct dv *) NULL;             /* Bind Variable chain      */
    return x;
}
/*
 * We only supply the data we are interested in; no precision or scale
 * for instance
 */
void dict_add(dict, dname,dbrief,dlong)
struct dict_con * dict;
char * dname;
char * dbrief;
char * dlong;
{
register struct dv * x;
int dnlen = strlen(dname);
int dblen = strlen(dbrief);
int dllen = strlen(dlong);

    if ((x = (struct dv *) malloc(
         sizeof(struct dv) + dnlen + dblen + dllen + 3)) == (struct dv *) NULL)
        return;
    x->next_dv = dict->anchor;
    dict->anchor = x;
    x->dom_val = 0;
    x->dname = ((char *) x) + sizeof(struct dv);
    strcpy(x->dname, dname);
    x->dbrief = x->dname + dnlen + 1;
    strcpy(x->dbrief, dbrief);
    x->dlong = x->dbrief + dblen + 1;
    strcpy(x->dlong, dlong);
    insert(dict->dvt,x->dname, (char *) x);
    return;
}
/*
 * Search for a domain variable in a dictionary
 */
struct dv * find_dv(dict,dname,dlen)
struct dict_con * dict;
char * dname;
int dlen;
{
char sname[64];
int slen = (dlen > 63) ? 63 : dlen;
HIPT * h;

    memcpy(sname,dname,slen);
    *(&(sname[0]) + slen) = '\0';
    if ((h = lookup(dict->dvt, &sname[0])) == (HIPT *) NULL)
        return (struct dv *) NULL;
    else
        return (struct dv *) (h->body);
}
/*
 * Free all memory allocated to a dictionary
 */
void dict_deall(dict)
struct dict_con * dict;
{
    iterate(dict->dvt, free, NULL);
    cleanup(dict->dvt);
    free(dict);
    return;
}
/*
 * Load dictionary
 */
void load_dict(dcp, fcp)
struct dict_con * dcp;
struct file_control * fcp;
{
int i;

    fcp->content.data.col_defs = 
           col_defs("NAME|SHORT|LONG\n");
    if (get_data(fcp))
        for (i = 0; i < fcp->content.data.recs; i++)
        {
            if (fcp->content.data.rows[i]->cols > 1);
                dict_add(dcp, fcp->content.data.rows[i]->colp[0],
                          fcp->content.data.rows[i]->colp[1],
                          fcp->content.data.rows[i]->colp[2]);
        }
    return;
}
/*
 * Deal with special characters in the values
 */
static void out_stuff(out, out_len, in, len)
unsigned char * out;
int out_len;
unsigned char * in;
int len;
{
register unsigned char *x1=in, *x2=out;

    for(out_len--; (len > 0 && out_len > 0); out_len--, len--)
    {
        if (*x1 == (unsigned char) '&')
        {
            *x2++ = '&';
            *x2++ = 'a';
            *x2++ = 'm';
            *x2++ = 'p';
            *x2++ = ';';
            out_len -= 4;
            x1++;
            continue;
        }
        else
        if (*x1 == (unsigned char) '"')
        {
            *x2++ = '&';
            *x2++ = '#';
            *x2++ = '3';
            *x2++ = '4';
            *x2++ = ';';
            out_len -= 4;
            x1++;
            continue;
        }
        *x2++ = *x1++;
    }
    *x2 = '\0';
    return;
}
/*
 * Boiler-plate to allow the addition of new items via a pop-up
 */
static void support_addition(dcp, cdp, max_sizes)
struct dict_con * dcp;
struct row * cdp;
int * max_sizes;
{
int i;
char buf[2000];
struct dv * dvp;
/*
 * Fuction to return the forms layer, creating it if necessary. Note that
 * there is no CSS element for the FORMS element just now, so the scrollTop
 * does nothing. The form appears at the end.
 */
    puts("<script>\n\
function cancel_add() {\n\
    document.getElementById(\"add_popup\").innerHTML = \"\";\n\
    return false;\n\
}\n\
function get_forms() {\n\
    var forms_layer = document.getElementById(\"add_popup\");\n\
    if (!forms_layer)\n\
    {\n\
        forms_layer = document.createElement('DIV');\n\
        forms_layer.setAttribute('CLASS', 'FORMS');\n\
        forms_layer.setAttribute('id', 'add_popup');\n\
        document.body.appendChild(forms_layer);\n\
    } \n\
    return forms_layer;\n\
}\n");
/*
 * Function to add the contents of the pop-up to the list of elements.
 */
        puts("function save_add() {\n\
    var new_row = document.createElement('TR');\n\
    var new_cell;\n");
        for (i = 0; i < cdp->cols; i++) 
        {
            printf("    new_cell = document.createElement('TD');\n\
            new_cell.innerHTML = \"<td><input type=text name=\\\"r\" + document.pform0.rows.value +\n\
      \"c%d\\\" size=%d value=\\\"\" +document.pform1.c%d.value +\"\\\"></td>\";\n\
    new_row.appendChild(new_cell);\n", i, max_sizes[i], i);
        }
        puts("    document.pform0.rows.value = (parseInt(document.pform0.rows.value) + 1)+'';\n\
    document.getElementById(\"ptab0\").insertBefore(new_row, document.getElementById(\"pform0sub\"));\n\
    return cancel_add();\n\
}\n");
/*
 * Function to render the pop-up.
 */
    puts("function add_form() {\n\
    var forms_layer = get_forms();\n\
    var txt = \"<h1>New Record</h1>\\n<form name=\\\"pform1\\\" id=\\\"pform1\\\" style=\\\"display:block\\\" onsubmit=\\\"return false;\\\" >\" +\n\
    \"<table id=\\\"ptab1\\\" style=\\\"display:block\\\">\" + ");
    for (i = 0; i < cdp->cols; i++)
    {
        if (dcp != NULL
         && (dvp = find_dv(dcp, cdp->colp[i],
                   strlen(cdp->colp[i]))) != NULL)
            printf(
"\"<tr><td>%s</td><td><input type=text name=\\\"c%d\\\" size=60 value=\\\"\\\"></td></tr>\" +\n",

                 dvp->dlong, i);
        else
            printf(
"\"<tr><td>%s</td><td><input type=text name=\\\"c%d\\\" size=60 value=\\\"\\\"></td></tr>\" + \n",
                 cdp->colp[i], i);
    }
    puts("\"<tr><td><input type=\\\"submit\\\" name=\\\"Save\\\" value=\\\"Save\\\" onClick=\\\"return save_add();\\\"></td>\" +\n\
\"<tr><td><input type=\\\"submit\\\" name=\\\"Cancel\\\" value=\\\"Cancel\\\" onClick=\\\"return cancel_add();\\\"></td></tr>\" + \n\
     \"</table></form>\";\n\
    forms_layer.innerHTML = txt;\n\
    forms_layer.style.top = document.body.scrollTop;\n\
    document.pform1.c0.focus();\n\
    return false;\n\
}\n\
</script>");
}
/***********************************************************************
 * Parameters.
 */
static char * usage = "Option -h outputs this message.\n\
-c Column names (in which case they are not on the first line of the file)\n\
-d Dictionary name  (in the directory $PATH_HOME/rules)\n\
-l Label alternative for file name\n\
-p Property List (First column is label)\n\
-r Read Only (no form, simply tabulate the data)\n\
-t Separator (default |)\n\
Parameters should be:\n\
 1 Name of data file to be edited (relative to the directory $PATH_HOME)\n";
/*
 * Ultimate Strategy
 * =================
 * The whole file is loaded on the server.
 *
 * The browser offers a sliding window.
 *
 * Updates use the fastclone segment logic to write out.
 *
 * Actually, we only need to use the fastclone program if we do do segmented
 * updates, and then this program essentially sends back range patches. If we
 * do file-at-a-time, we send the whole file with an XmlHttpRequest PUT,
 * and the existing code in webmenu.c will put it back for us.
 *
 * So we have started with the only-a-whole-file-at-once approach.
 * 
 * There are a number of issues with respect to the sliding window and the range.
 * -   We do not want to put the range in the header, because:
 *     -    We may have to edit the environment variable html_head
 *     -    We may have multiple forms on the web page
 * -   We could effectively edit a whole file, but have the logic that invokes
 *     this program:
 *     -    Include buttons such as 'Next' and 'Previous' prior to the output
 *          of this program
 *     -    Handle the merging back of the returned data submitted as a whole file
 *     However:
 *     -    Supporting seamless scrolling would not be elegant
 *     -    Global edits would be interesting
 *     -    What about sorting the grid?
 *     -    What do we do about concurrent updates to the same file?
 *
 * It is needed for arbitrarily large input, but not for the numbers of
 * records we will encounter within PATH for now.
 *
 * This means we only need to write the form.
 ****************************************************************************
 * Main program starts here
 * VVVVVVVVVVVVVVVVVVVVVVVV
 */
int main(argc, argv)
int argc;
char ** argv;
{
struct dict_con * dcp = NULL;
struct dv * dvp;
int property_flag = 0;
int read_only = 0;
int id_gen = 0;
char * fname;
struct file_control data_file;
struct file_control dict_file;
int external_cols = 0;
char * path_home;
char * html_head;
char * html_tail;
char * label = NULL;
char * message_text;
int * max_sizes;
int i;
int j;
int mult;
char buf[2048];

    if ((path_home = getenv("PATH_HOME")) == NULL)
    {
        fputs("You must have PATH_HOME in your environment\n", stderr);
        exit(1);
    }
    memset((char *) &data_file, 0, sizeof(data_file));
/*
 * Look for options
 */
    while ( ( mult = getopt( argc, argv, "c:d:hl:prt:" ) ) != EOF )
    {
        switch ( mult )
        {
        case 'c':
            data_file.content.data.col_defs = col_defs(optarg);
            external_cols = 1;
            break;
        case 'd':
            dcp = new_dict(1024);
            memset((char *) &dict_file, 0, sizeof(dict_file));
            dict_file.fname = optarg;
            load_dict(dcp, &dict_file);
            break;
        case 'l':
            label = optarg;
            break;
        case 'p':
            property_flag = 1;
            break;
        case 'r':
            read_only = 1;
            break;
        case 't':
            set_fs(optarg);
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
    if (argc - optind < 1)
    {
        fputs("Too few parameters\n", stderr);
        fputs(usage, stderr);
        exit(1);
    }
/*
 * Attempt to load the data file. Give up if failed.
 */
    fname = argv[optind];
    if (!strcmp(fname, "-"))
        data_file.fname = "-";
    else
    {
        data_file.fname = (char *) malloc(strlen(path_home) +
                            strlen(argv[optind]) + 2);
        for (i = sprintf(data_file.fname, "%s/%s",
                              path_home, fname), i--;
                i > -1 && (data_file.fname[i] == '\r' || data_file.fname[i] == '\n'); data_file.fname[i--] = '\0') ;
    }
/*
 * Attempt to load the data file.
 */ 
    if (!get_data(&data_file))
        exit(-1);
    if (!read_only && !property_flag)
        max_sizes = get_sizes(&data_file);
/*
 * Process the data file, producing the web page
 */
    if ((html_head = getenv("html_head")) != NULL)
        puts(html_head);
/*
 * This logic could be abstracted into a separate library. But without it here,
 * we miss out on the opportunity to hard code the actual number of columns
 * and the variable names.
 */
    if (!read_only)
    {
        printf("<script>\n\
function drawform()\n\
{\n\
document.pform0.%s.focus();\n\
}\n\
var req=null;\n\
function handleresp()\n\
{\n\
    return;\n\
}\n\
function put_file(fname, txt) {\n\
/*\n\
 * Mozilla and WebKit\n\
 */\n\
if (window.XMLHttpRequest)\n\
    req = new XMLHttpRequest();\n\
else\n\
if (window.ActiveXObject)\n\
{\n\
/*\n\
 * Internet Explorer (new and old)\n\
 */\n\
    try\n\
    {\n\
       req = new ActiveXObject(\"Msxml2.XMLHTTP\");\n\
    }\n\
    catch (e)\n\
    {\n\
       try\n\
       {\n\
           req = new ActiveXObject(\"Microsoft.XMLHTTP\");\n\
       }\n\
       catch (e)\n\
       {}\n\
    }\n\
} \n\
req.open(\"PUT\", fname, true);\n\
req.setRequestHeader(\"Content-type\",\"application/octet-stream\");\n\
req.setRequestHeader(\"Expect\",\"100-Continue\");\n\
req.onreadystatechange = handleresp;\n\
try\n\
{\n\
req.send(txt);\n\
}\n\
catch (e)\n\
{\n\
    alert(\"Error: \" + e)\n\
}\n\
return;\n\
}\n", ((property_flag) ? "r0c1" : "r0c0"));
        printf("function ready_data() {\n\
    var rows = parseInt(document.pform0.rows.value);\n\
    var cols = %d;\n\
    var sep = \"%s\";\n",
         data_file.content.data.col_defs->cols,
         get_fs());
    if (!property_flag && !external_cols)
        puts("var txt = document.pform0.col0.value;\n\
    for (var i = 1; i < cols; i++)\n\
    {\n\
        txt = txt + sep + eval(\"document.pform0.col\" + i + \".value\");\n\
    }\n\
    txt = txt + \"\\n\";\n");
    else
        puts("var txt = \"\";\n");
    puts("for (i = 0; i < rows; i++)\n\
    {\n\
        txt = txt + eval(\"document.pform0.r\" + i + \"c0.value\");\n\
        for (var j = 1; j < cols; j++)\n\
            txt = txt + sep + eval(\"document.pform0.r\" + i + \"c\" +\n\
                   j + \".value\");\n\
        txt = txt + \"\\n\";\n\
    }\n\
    return txt;\n\
}\n\
function do_submit() {\n\
    put_file(document.pform0.filename.value, ready_data());\n\
    return false;\n\
}\n\
</script>");
        if (!property_flag)
            support_addition(dcp, data_file.content.data.col_defs, max_sizes);
    }
    if ((message_text = getenv("message_text")) != NULL
      && strlen(message_text) > 0)
        printf(
"<p style=\"color:red; background-color:white;\"><b>%s</b></p><hr />\n",
               message_text);
/*
 * Output the title
 */
    if (label != NULL)
        printf("<h1>%s</h1>\n", label);
    else
    if (dcp != NULL && (dvp = find_dv(dcp, fname, strlen(fname))) != NULL)
        printf("<h1>%s</h1>\n", dvp->dlong);
    else
        printf("<h1>%s</h1>\n", fname);
    if (property_flag && data_file.content.data.col_defs->cols < 2)
    {
        fputs("Property flag set but less than 2 columns!?\n", stderr);
        property_flag = 0;
        read_only = 1;
    }
    if (!read_only)
    {
/*
 * Now need to output:
 * -  Hidden variables
 *    -  Name of file
 *    -  Count of columns
 *    -  Column Names
 *    -  Separator
 *    -  Count of rows
 */
        puts("<FORM name=\"pform0\" onSubmit=\"return do_submit();\" action=\"/\" method=get id=\"pform0\">");
        out_stuff(buf, sizeof(buf), fname, strlen(fname));
        printf("<input name=\"filename\" type=\"hidden\" value=\"%s\">\n", buf);
        printf("<input name=\"col_cnt\" type=\"hidden\" value=\"%d\">\n", 
                        data_file.content.data.col_defs->cols);
        for (i = 0; i < data_file.content.data.col_defs->cols; i++)
        {
            out_stuff(buf, sizeof(buf), data_file.content.data.col_defs->colp[i],
                  strlen(data_file.content.data.col_defs->colp[i]));
            printf("<input name=\"col%d\" type=\"hidden\" value=\"%s\">\n",
                 i, buf);
        }
        out_stuff(buf, sizeof(buf), get_fs(), strlen(get_fs()));
        printf("<input name=\"sep\" type=\"hidden\" value=\"%s\">\n", 
                        buf);
        printf("<input name=\"rows\" type=\"hidden\" value=\"%d\">\n", 
                 data_file.content.data.recs);
    }
    puts("<table style=\"display:block\"><tbody id=\"ptab0\">");
    if (!property_flag)
    {
/*
 * -  Heading information from dictionary, if there is any
 */
        fputs("<tr>", stdout);
        for (i = 0; i < data_file.content.data.col_defs->cols; i++)
            if (dcp != NULL && (dvp = find_dv(dcp, 
                     data_file.content.data.col_defs->colp[i],
                     strlen(data_file.content.data.col_defs->colp[i]))) != NULL)
                 printf("<th>%s</th>", dvp->dbrief);
            else
                printf("<th>%s</th>",
                     data_file.content.data.col_defs->colp[i]);
        puts("</tr>");
    }
/*
 * Data Rows - 3 cases
 * -  Property flag set
 *    -  Data is name value pair
 *    -  Long description in first column
 *    -  Data value in second
 * -  Read Only
 *    -  Just output in tabular form
 * -  Property flag clear
 *    -  Short description went in columns
 *    -  Data rows
 * Actual variable names are generated sequentially
 */
    for (i = 0; i < data_file.content.data.recs; i++)
    {
        fputs("<tr>", stdout);
        if (property_flag)
        {
            if (dcp != NULL && (dvp = find_dv(dcp, 
                     data_file.content.data.rows[i]->colp[0],
                     strlen(data_file.content.data.rows[i]->colp[0]))) != NULL)
                printf("<td>%s</td>", dvp->dlong);
            else
                printf("<td>%s</td>",
                     data_file.content.data.rows[i]->colp[0]);
            out_stuff(buf, sizeof(buf), 
                     data_file.content.data.rows[i]->colp[0],
                     strlen(data_file.content.data.rows[i]->colp[0]));
            printf("<input type=hidden name=\"r%dc0\" id=\"r%dc0\" value=\"%s\">",
                 i, i, buf);
            out_stuff(buf, sizeof(buf), 
                     data_file.content.data.rows[i]->colp[1],
                     strlen(data_file.content.data.rows[i]->colp[1]));
            printf("<td><input type=text size=60 name=\"r%dc1\" id=\"r%dc1\" value=\"%s\"></td>",
                 i, i, buf);
        }
        else
        if (read_only)
        {
            for (j = 0; j < data_file.content.data.col_defs->cols; j++)
                printf("<td>%s</td>",
                     data_file.content.data.rows[i]->colp[j]);
        }
        else
        {
            for (j = 0; j < data_file.content.data.col_defs->cols; j++)
            {
                out_stuff(buf, sizeof(buf), 
                     data_file.content.data.rows[i]->colp[j],
                     strlen(data_file.content.data.rows[i]->colp[j]));
                printf("<td><input type=text size=%d name=\"r%dc%d\" id=\"r%dc%d\" value=\"%s\"></td>",
                 max_sizes[j], i, j, i, j, buf);
            }
        }
        puts("</tr>");
    }
    if (!read_only)
    {
        puts("<tr id=\"pform0sub\"><td><input type=\"submit\" name=\"Submit\" value=\"Submit\"></td>");
        if (property_flag)
            puts("</tr>");
        else
            puts("<td><input type=\"submit\" name=\"Add\" value=\"Add\" onClick=\"return add_form();\"></td></tr>");
    }
    puts("</tbody></table>");
    if (!read_only)
        puts("</form>");
    if ((html_tail = getenv("html_tail")) != NULL)
        puts(html_tail);
/*
 * Finish
 */
    exit(0);
}
