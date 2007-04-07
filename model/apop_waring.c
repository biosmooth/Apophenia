/** \file apop_waring.c

  The Waring distribution. 

Copyright (c) 2005 by Ben Klemens. Licensed under the GNU GPL version 2.
*/

//The default list. Probably don't need them all.
#include "types.h"
#include "stats.h"
#include "model.h"
#include "mapply.h"
#include "conversions.h"
#include "likelihoods.h"
#include "linear_algebra.h"
#include <gsl/gsl_rng.h>
#include <gsl/gsl_sort.h>
#include <gsl/gsl_histogram.h>
#include <gsl/gsl_sort_vector.h>
#include <gsl/gsl_permutation.h>
#include <stdio.h>
#include <assert.h>

/////////////////////////
//The Waring distribution
/////////////////////////
/** The Waring distribution

\f$W(x, b,a) 	= (b-1) \gamma(b+a) \gamma(k+a) / [\gamma(a+1) \gamma(k+a+b)]\f$

\f$\ln W(x, b, a) = \ln(b-1) + \ln\gamma(b+a) + \ln\gamma(k+a) - \ln\gamma(a+1) - \ln\gamma(k+a+b)\f$

\f$dlnW/db	= 1/(b-1)  + \psi(b+a) - \psi(k+a+b)\f$

\f$dlnW/da	= \psi(b+a) + \psi(k+a) - \psi(a+1) - \psi(k+a+b)\f$

\ingroup likelihood_fns
*/
static apop_params * waring_estimate(apop_data * data, apop_params *parameters){
	return apop_maximum_likelihood(data, apop_waring, parameters);
}

static double beta_zero_and_one_greater_than_x_constraint(const apop_data *beta, apop_data *returned_beta, apop_params *v){
    //constraint is 1 < beta_1 and  0 < beta_2
  static apop_data *constraint = NULL;
    if (!constraint)constraint= apop_data_calloc(2,2,2);
    apop_data_set(constraint, 0, -1, 1);
    apop_data_set(constraint, 0, 0, 1);
    apop_data_set(constraint, 1, 1, 1);
    return apop_linear_constraint(beta->vector, constraint, 1e-3, returned_beta->vector);
}


double a, bb;
static double apply_me(gsl_vector *data){
  size_t    i;
  double    likelihood  = 0,
            val, ln_bb_a_k, ln_a_k;
    for (i=0; i< data->size; i++){
        val          = gsl_vector_get(data, i);
        ln_bb_a_k	 = gsl_sf_lngamma(val + a + bb);
        ln_a_k		 = gsl_sf_lngamma(val + a);
        likelihood	+=  ln_a_k - ln_bb_a_k;
    }
    return likelihood;
}

static double waring_log_likelihood(const apop_data *beta, apop_data *d, apop_params *p){
    bb	= gsl_vector_get(beta->vector, 0),
    a	= gsl_vector_get(beta->vector, 1);
  double 		likelihood 	= 0,
		        ln_bb_a		= gsl_sf_lngamma(bb + a),
                ln_a_mas_1	= gsl_sf_lngamma(a + 1),
                ln_bb_less_1= log(bb - 1);
  gsl_vector * v = apop_matrix_map(d->matrix, apply_me);
    likelihood   = apop_vector_sum(v);
    gsl_vector_free(v);
	likelihood	+= (ln_bb_less_1 + ln_bb_a - ln_a_mas_1)* d->matrix->size1 * d->matrix->size2;
	return likelihood;
}

/** The derivative of the Waring distribution, for use in likelihood
 minimization. You'll probably never need to call this directy.*/
static void waring_dlog_likelihood(const apop_data *beta, apop_data *d, gsl_vector *gradient, apop_params *p){
	//Psi is the derivative of the log gamma function.
    bb		        = gsl_vector_get(beta->vector, 0);
	a		        = gsl_vector_get(beta->vector, 1);
  int 		    i, k;
  gsl_matrix	*data		    = d->matrix;
  double		bb_minus_one_inv= 1/(bb-1), val,
		        psi_a_bb	        = gsl_sf_psi(bb + a),
		        psi_a_mas_one	    = gsl_sf_psi(a+1),
		        psi_a_k,
		        psi_bb_a_k,
		        d_bb		        = 0,
		        d_a		            = 0;
	for (i=0; i< data->size1; i++){
	    for (k=0; k< data->size2; k++){	
            val          = gsl_matrix_get(data, i, k);
		    psi_bb_a_k	 = gsl_sf_psi(val + a + bb);
		    psi_a_k		 = gsl_sf_psi(val + a);
			d_bb	    -= psi_bb_a_k;
			d_a		    += (psi_a_k - psi_bb_a_k);
		}
	}
	d_bb	+= (bb_minus_one_inv + psi_a_bb) * data->size1 * data->size2;
	d_a	    += (psi_a_bb- psi_a_mas_one) * data->size1 * data->size2;
	gsl_vector_set(gradient, 0, d_bb);
	gsl_vector_set(gradient, 1, d_a);
}

static double waring_p(const apop_data *beta, apop_data *d, apop_params *p){
    return exp(waring_log_likelihood(beta, d, p));
}

/** Give me parameters, and I'll draw a ranking from the appropriate
Waring distribution. [I.e., if I randomly draw from a Waring-distributed
population, return the ranking of the item I just drew.]

Page seven of:
L. Devroye, <a href="http://cgm.cs.mcgill.ca/~luc/digammapaper.ps">Random
variate generation for the digamma and trigamma distributions</a>, Journal
of Statistical Computation and Simulation, vol. 43, pp. 197-216, 1992.
*/
static void waring_rng(double *out, gsl_rng *r, apop_params *eps){
//The key to covnert from Devroye's GHgB3 notation to what I
//consider to be the standard Waring notation in \ref apop_waring:
// a = a + 1
// b = 1 
// c = b - 1 
// n = k - 1 , so if it returns 0, that's first rank.
// OK, I hope that clears everything up.
  double		x, u,
                b   = gsl_vector_get(eps->parameters->vector, 0),
                a   = gsl_vector_get(eps->parameters->vector, 1),
		params[]	={a+1, 1, b-1};
	do{
		x	= apop_GHgB3_rng(r, params);
		u	= gsl_rng_uniform(r);
	} while (u >= (x + a)/(GSL_MAX(a+1,1)*x));
	*out = x+1;
}

/** The Waring distribution
Ignores the matrix structure of the input data, so send in a 1 x N, an N x 1, or an N x M.

apop_waring.estimate() is an MLE, so feed it appropriate \ref apop_params.

\f$W(x,k, b,a) 	= (b-1) \gamma(b+a) \gamma(k+a) / [\gamma(a+1) \gamma(k+a+b)]\f$

\f$\ln W(x,k, b, a) = ln(b-1) + lng(b+a) + lng(k+a) - lng(a+1) - lng(k+a+b)\f$

\f$dlnW/db	= 1/(b-1)  + \psi(b+a) - \psi(k+a+b)\f$

\f$dlnW/da	= \psi(b+a) + \psi(k+a) - \psi(a+1) - \psi(k+a+b)\f$
\ingroup models
\bugs This function needs better testing.
*/
//apop_model apop_waring = {"Waring", 2, apop_waring_log_likelihood, NULL, NULL, 0, NULL, apop_waring_rng};
apop_model apop_waring = {"Waring", 2,0,0, 
	waring_estimate, waring_p,  waring_log_likelihood, waring_dlog_likelihood, beta_zero_and_one_greater_than_x_constraint,  waring_rng};