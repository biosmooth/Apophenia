//asst.h			  	Copyright 2007 by Ben Klemens. Licensed under the GNU GPL v2.
#ifndef __apop_asst__
#define __apop_asst__

#include <assert.h>
#include <apophenia/types.h>
#include <gsl/gsl_rng.h>
#include <gsl/gsl_matrix.h>

#undef __BEGIN_DECLS    /* extern "C" stuff cut 'n' pasted from the GSL. */
#undef __END_DECLS
#ifdef __cplusplus
# define __BEGIN_DECLS extern "C" {
# define __END_DECLS }
#else
# define __BEGIN_DECLS /* empty */
# define __END_DECLS /* empty */
#endif

__BEGIN_DECLS

double apop_generalized_harmonic(int N, double s);
void apop_error(int level, char stop, char *message, ...);
apop_model *apop_model_fix_params(apop_data *paramvals, apop_data *mask, apop_model model_in, apop_params *params_for_model, apop_params *mle_params);

apop_params * apop_update(apop_data *data, apop_model prior, apop_model likelihood, 
                        apop_params *prior_eps, apop_params * likelihood_eps, 
                        apop_data *starting_pt, gsl_rng *r, int periods, double burnin, int histosegments);


__END_DECLS
#endif