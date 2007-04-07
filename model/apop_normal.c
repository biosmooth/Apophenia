/** \file apop_normal.c

Copyright (c) 2005 by Ben Klemens. Licensed under the GNU GPL version 2.
*/

//The default list. Probably don't need them all.
#include "types.h"
#include "mapply.h"
#include "bootstrap.h"
#include "regression.h"
#include "conversions.h"
#include "likelihoods.h"
#include <apophenia/model.h>
#include <apophenia/stats.h>
#include <apophenia/linear_algebra.h>
#include <gsl/gsl_rng.h>
#include <stdio.h>
#include <assert.h>
static double normal_log_likelihood(const apop_data *beta, apop_data *d, apop_params *params);


//////////////////
//The Normal (gaussian) distribution
//////////////////

/** The normal estimate
\todo Get off my ass and check the closed-form var-covar matrix, instead of using the inverse hessian. */
static apop_params * normal_estimate(apop_data * data, apop_params *parameters){
  double		mean, var;
  apop_params 	*est	    = parameters;
  apop_OLS_params *p;
    if (!parameters) {
        p = apop_OLS_params_alloc(0,data,&apop_normal, NULL);
        est   = p->ep;
    }
	apop_matrix_mean_and_var(data->matrix, &mean, &var);	
	gsl_vector_set(est->parameters->vector, 0, mean);
	gsl_vector_set(est->parameters->vector, 1, var);
	if (est->uses.log_likelihood)
		est->log_likelihood	= normal_log_likelihood(est->parameters, data, NULL);
	if (est->uses.covariance)
		apop_numerical_covariance_matrix(apop_normal, est, data);
	return est;
}

static double beta_1_greater_than_x_constraint(const apop_data *beta, apop_data *returned_beta, apop_params *v){
    //constraint is 0 < beta_2
  static apop_data *constraint = NULL;
    if (!constraint) constraint = apop_data_calloc(1,1,2);
    apop_data_set(constraint, 0, 1, 1);
    return apop_linear_constraint(beta->vector, constraint, 1e-3, returned_beta->vector);
}

static double   mu, variance;

static double apply_me(gsl_vector *v){
  int           i;
  long double    ll  = 1;
    for(i=0; i< v->size; i++)
	    ll	*= gsl_ran_gaussian_pdf((gsl_vector_get(v, i) - mu), variance);
    return ll;
}

static double apply_me2(gsl_vector *v){
  int           i;
  long double    ll  = 0;
    for(i=0; i< v->size; i++)
	    ll	+= log(gsl_ran_gaussian_pdf((gsl_vector_get(v, i) - mu), variance));
    return ll;
}

/* The log likelihood function for the Normal.

The log likelihood function and dlog likelihood don't care about your
rows of data; if you have an 8 x 7 data set, it will give you the log
likelihood of those 56 observations given the mean and variance (i.e.,
\f$\sigma^2\f$, not std deviation=\f$\sigma\f$) you provide.

\f$N(\mu,\sigma^2) = {1 \over \sqrt{2 \pi \sigma^2}} \exp (-(x-\mu)^2 / 2\sigma^2)\f$
\f$\ln N(\mu,\sigma^2) = (-(x-\mu)^2 / 2\sigma^2) - \ln (2 \pi \sigma^2)/2 \f$

\param beta	beta[0]=the mean; beta[1]=the variance
\param d	the set of data points; see notes.
*/
static double normal_log_likelihood(const apop_data *beta, apop_data *d, apop_params *params){
    mu	        = gsl_vector_get(beta->vector,0);
    variance    = gsl_vector_get(beta->vector,1);
  gsl_vector *  v       = apop_matrix_map(d->matrix, apply_me2);
  long double   ll      = apop_vector_sum(v);
    gsl_vector_free(v);
	return ll;
}

static double normal_p(const apop_data *beta, apop_data *d, apop_params *params){
    mu	        = gsl_vector_get(beta->vector,0);
    variance    = gsl_vector_get(beta->vector,1);
  gsl_vector *  v       = apop_matrix_map(d->matrix, apply_me);
  int           i;
  long double   ll      = 1;
    for(i=0; i< v->size; i++)
        ll  *= gsl_vector_get(v, i);
    gsl_vector_free(v);
	return ll;
}

/** Gradient of the log likelihood function

To tell you the truth, I have no idea when anybody would need this, but it's here for completeness.
\f$d\ln N(\mu,\sigma^2)/d\mu = (x-\mu) / \sigma^2 \f$
\f$d\ln N(\mu,\sigma^2)/d\sigma^2 = ((x-\mu)^2 / 2(\sigma^2)^2) - 1/2\sigma^2 \f$
\todo Add constraint that \f$\sigma^2>0\f$.
 */
static void normal_dlog_likelihood(const apop_data *beta, apop_data *d, 
                                    gsl_vector *gradient, apop_params *params){    
            mu      = gsl_vector_get(beta->vector,0);
double      ss      = gsl_vector_get(beta->vector,1),
            dll     = 0,
            sll     = 0,
            x;
int         i,j;
gsl_matrix  *data   = d->matrix;
    for (i=0;i< data->size1; i++)
        for (j=0;j< data->size2; j++){
            x    = gsl_matrix_get(data, i, j);
            dll += (x - mu);
            sll += gsl_pow_2(x - mu);
        }
    gsl_vector_set(gradient, 0, dll/ss);
    gsl_vector_set(gradient, 1, sll/(2*gsl_pow_2(ss))+ data->size1 * data->size2 * 0.5/ss);
}


/** An apophenia wrapper for the GSL's Normal RNG.

Two differences: this one asks explicitly for a mean, and the GSL
assumes zero and makes you add the mean yourself; Apophenia tends to
prefer the variance (\f$\sigma^2\f$) wherever possible, while the GSL
uses the standard deviation here (\f$\sigma\f$)

\param r	a gsl_rng already allocated
\param a	the mean and the variance
 */
static void normal_rng(double *out, gsl_rng *r, apop_params *p){
	*out = gsl_ran_gaussian(r, sqrt(p->parameters->vector->data[1])) + p->parameters->vector->data[0];
}

/** You know it, it's your attractor in the limit, it's the Gaussian distribution.

Where possible, Apophenia tries to report the variance, \f$\sigma^2\f$,
not the standard deviation, \f$\sigma\f$. So the first parameter for
this model is the mean, and the second is the variance.

The log likelihood function and dlog likelihood don't care about your
rows of data; if you have an 8 x 7 data set, it will give you the log
likelihood of those 56 observations given the mean and variance you provide.

\f$N(\mu,\sigma^2) = {1 \over \sqrt{2 \pi \sigma^2}} \exp (-x^2 / 2\sigma^2)\f$

\f$\ln N(\mu,\sigma^2) = (-(x-\mu)^2 / 2\sigma^2) - \ln (2 \pi \sigma^2)/2 \f$

\f$d\ln N(\mu,\sigma^2)/d\mu = (x-\mu) / \sigma^2 \f$

\f$d\ln N(\mu,\sigma^2)/d\sigma^2 = ((x-\mu)^2 / 2(\sigma^2)^2) - 1/2\sigma^2 \f$
\ingroup models
*/
apop_model apop_normal = {"Normal", 2, 0, 0,
 normal_estimate, normal_p, normal_log_likelihood, normal_dlog_likelihood, beta_1_greater_than_x_constraint, normal_rng};

/** This is a synonym for \ref apop_normal, q.v.
\ingroup models
*/
apop_model apop_gaussian = {"Gaussian", 2,0,0,
 normal_estimate, normal_p, normal_log_likelihood, normal_dlog_likelihood, beta_1_greater_than_x_constraint, normal_rng};