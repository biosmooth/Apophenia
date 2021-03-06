/**\file db.h	*/
/* Copyright (c) 2005--2007 by Ben Klemens.  Licensed under the modified * GNU GPL v2; see COPYING and COPYING2.  */
#ifndef apop_db_included
#define apop_db_included
#include "types.h"
#include "variadic.h"
#include "asst.h"
#include <gsl/gsl_matrix.h>

#ifdef __cplusplus
extern "C" {
#endif

Apop_var_declare( int apop_table_exists(char const *name, char remove) )

int apop_db_open(char const *filename);
Apop_var_declare( int apop_db_close(char vacuum) )

int apop_query(const char *q, ...) __attribute__ ((format (printf,1,2)));
gsl_matrix * apop_query_to_matrix(const char * fmt, ...) __attribute__ ((format (printf,1,2)));
apop_data * apop_query_to_text(const char * fmt, ...) __attribute__ ((format (printf,1,2)));
apop_data * apop_query_to_data(const char * fmt, ...) __attribute__ ((format (printf,1,2)));
apop_data * apop_query_to_mixed_data(const char *typelist, const char * fmt, ...) __attribute__ ((format (printf,2,3)));
gsl_vector * apop_query_to_vector(const char * fmt, ...) __attribute__ ((format (printf,1,2)));
double apop_query_to_float(const char * fmt, ...) __attribute__ ((format (printf,1,2)));

int apop_data_to_db(const apop_data *set, const char *tabname, char);

double apop_db_t_test(char * tab1, char *col1, char *tab2, char *col2);
double apop_db_paired_t_test(char * tab1, char *col1, char *col2);

#ifdef __cplusplus
}
#endif
#endif
