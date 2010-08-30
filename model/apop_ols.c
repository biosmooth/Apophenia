/** \file 
OLS models. Much of the real work is done in apop_regression.c.*/
/* Copyright (c) 2005--2007, 2010 by Ben Klemens.  Licensed under the modified GNU GPL v2; see COPYING and COPYING2.  */

#include "model.h"
#include "conversions.h"
#include "internal.h"
#include "settings.h"
#include "stats.h"
#include "asst.h"

void * apop_lm_settings_copy(apop_lm_settings *in){
  apop_lm_settings *out  = malloc(sizeof(*out));
    *out =  *in;
    out->instruments = apop_data_copy(in->instruments);
    if (in->input_distribution)
        out->input_distribution = apop_model_copy(*in->input_distribution);
    return out;
}

void apop_lm_settings_free(apop_lm_settings *in){ 
    apop_model_free(in->input_distribution);
    free(in); 
}

/** Initialize the settings for a least-squares--type model. For use
  with \ref Apop_model_add_group.  See \ref apop_lm_settings for the possible elements to set.
  */
apop_lm_settings * apop_lm_settings_init(apop_lm_settings in){
  apop_lm_settings *out  = malloc(sizeof(*out));
    *out = in;
    if (out->want_cov == 1 || !out->want_cov) out->want_cov = 'y';
    if (out->want_expected_value == 1 || !out->want_expected_value) out->want_expected_value = 'y';
    if (!out->input_distribution) 
       out->input_distribution = apop_model_copy(apop_improper_uniform);
    return out;
}

//shift first col to depvar, rename first col "one".
static void prep_names (apop_model *e){
  apop_lm_settings   *p = apop_settings_get_group(e, apop_lm);
    apop_data *predicted = apop_data_get_page(e->info, "<Predicted>");
    if (predicted){
        apop_name_add(predicted->names, (e->data->names->colct ? e->data->names->column[0] : "Observed"), 'c');
        apop_name_add(predicted->names, "Predicted", 'c');
        apop_name_add(predicted->names, "Residual", 'c');
    }
	if (e->data->names->vector) { //this is post ols shuffle.
        if (e->parameters)
            snprintf(e->parameters->names->title, 100, "Regression of %s", e->data->names->vector);
        apop_name_add(e->parameters->names, "1", 'r');
        apop_name_add(e->parameters->names, "parameters", 'v');
        for(int i=1; i< e->data->names->colct; i++)
            apop_name_add(e->parameters->names, e->data->names->column[i], 'r');
        if (p->want_cov== 'y'){
            apop_data *cov = apop_data_get_page(e->parameters, "<Covariance>");
            if (e->data->names){
                apop_name_stack(cov->names, e->data->names, 'c');
                apop_name_stack(cov->names, e->data->names, 'r', 'c');
            }
		    sprintf(cov->names->column[0], "1");
		    sprintf(cov->names->row[0], "1");
        }
	}
}

static void ols_shuffle(apop_data *d){
    if (!d->vector){
        APOP_COL(d, 0, independent);
        d->vector = apop_vector_copy(independent);
        gsl_vector_set_all(independent, 1);     //affine; first column is ones.
        if (d->names->colct > 0) {		
            apop_name_add(d->names, d->names->column[0], 'v');
            sprintf(d->names->column[0], "1");
        }
    }
}

static void ols_prep(apop_data *d, apop_model *m){
    ols_shuffle(d);
    void *mpt = m->prep; //also use the defaults.
    m->prep = NULL;
    apop_prep(d, m);
    m->prep = mpt;
}

/** The assumption that makes a log likelihood possible is that the
errors are normally distributed.

This function is a bit inefficient, in that it calculates the error terms,
which you may have already done in the OLS estimation.
 */
static double ols_log_likelihood (apop_data *d, apop_model *p){ 
    Nullcheck_m(p); Nullcheck_p(p);
  long double	ll  = 0; 
  long double      sigma, actual, weight;
  double expected, x_prob;
  apop_lm_settings *lms = Apop_settings_get_group(p, apop_lm);
  apop_model *input_distribution = lms ? lms->input_distribution : NULL;
  gsl_matrix	*data		    = d->matrix;
  gsl_vector  *errors         = gsl_vector_alloc(data->size1);
	for (size_t i=0;i< data->size1; i++){
        Apop_row(d, i, datarow);
        gsl_blas_ddot(p->parameters->vector, datarow, &expected);
        if (d->vector){ //then this has been prepped
            actual       = apop_data_get(d,i, -1);
        } else {
            actual       = gsl_matrix_get(data,i, 0);
            expected    += gsl_vector_get(p->parameters->vector,0) * (1 - actual); //data isn't affine.
        }
        gsl_vector_set(errors, i, expected-actual);
    }
    sigma   = sqrt(apop_vector_var(errors));
	for(size_t i=0;i< data->size1; i++){
        Apop_row(d, i, datarow);
        gsl_matrix_view m = gsl_matrix_view_vector(datarow, 1, datarow->size);
        apop_data justarow = {.matrix=&(m.matrix)};
        if (input_distribution)
            x_prob = apop_p(&justarow, input_distribution); //probably improper uniform, and so just 1 anyway.
        else
            x_prob = 1;
        weight       = d->weights ? gsl_vector_get(d->weights, i) : 1; 
        ll  += log(gsl_ran_gaussian_pdf(gsl_vector_get(errors, i), sigma)* weight * x_prob);
	} 
    gsl_vector_free(errors);
    return ll;
}

/* $\partial {\cal N}(x\beta - y)/\partial \beta_i = \sum{x_i} \partial {\cal N}(K)/\partial K$ (at $K=x\beta -y$) */
static void ols_score(apop_data *d, gsl_vector *gradient, apop_model *p){ 
  apop_assert_s(p->parameters, "You asked me to evaluate an un-parametrized model. Not changing the gradient");
  long double      sigma, actual, weight;
  double expected;
  gsl_matrix	*data		    = d->matrix;
  gsl_vector  *errors         = gsl_vector_alloc(data->size1);
  gsl_vector  *normscore      = gsl_vector_alloc(2);
  apop_data  *subdata      = apop_data_alloc(0,1,1);
	for(size_t i=0;i< data->size1; i++){
        APOP_ROW(d, i, datarow);
        gsl_blas_ddot(p->parameters->vector, datarow, &expected);
        if (d->vector){ //then this has been prepped
            actual       = apop_data_get(d,i, -1);
        } else {
            actual       = gsl_matrix_get(data,i, 0);
            expected    +=  gsl_vector_get(p->parameters->vector,0) * (1 - actual); //data isn't affine.
        }
        gsl_vector_set(errors, i, expected-actual);
    }
    sigma   = sqrt(apop_vector_var(errors));
    apop_model *norm = apop_model_set_parameters(apop_normal, 0.0, sigma);
    gsl_vector_set_all(gradient, 0);
	for(size_t i=0;i< data->size1; i++){
        apop_data_set(subdata, 0, 0, gsl_vector_get(errors, i));
        apop_score(subdata, normscore, norm);
        weight       = d->weights ? gsl_vector_get(d->weights, i) : 1; 
        for(size_t j=0; j< data->size2; j++)
            apop_vector_increment(gradient, j, weight * apop_data_get(d, i, j) * gsl_vector_get(normscore, 0));
	} 
    gsl_vector_free(errors);
    apop_model_free(norm);
}


static void xpxinvxpy(gsl_matrix *data, gsl_vector *y_data, gsl_matrix *xpx, gsl_vector* xpy, apop_model *out){
  apop_lm_settings   *p =  apop_settings_get_group(out, apop_lm);
	if ((p->want_cov!='y') && (p->want_expected_value != 'y') ){	
		//then don't calculate (X'X)^{-1}
		gsl_linalg_HH_solve (xpx, xpy, out->parameters->vector);
		return;
	} //else:
  gsl_vector 	*error = gsl_vector_alloc(data->size1);
  gsl_vector 	predicted;
  gsl_matrix	*cov;
  double        s_sq;
	cov	= apop_matrix_inverse(xpx);	    //not yet cov, just (X'X)^-1.
	gsl_blas_dgemv(CblasNoTrans, 1, cov, xpy, 0, out->parameters->vector);      // \beta=(X'X)^{-1}X'Y
	gsl_blas_dgemv(CblasNoTrans, 1, data, out->parameters->vector, 0, error);   // X'\beta ==predicted
	gsl_vector_sub(error,y_data);           //X'\beta - Y == error
    gsl_blas_ddot(error, error, &s_sq);   // e'e
    s_sq    /= data->size1 - data->size2;   //\sigma^2 = e'e / df
	gsl_matrix_scale(cov, s_sq);            //cov = \sigma^2 (X'X)^{-1}
	if (p->want_expected_value){
        apop_data *predicted_page = apop_data_get_page(out->info, "<Predicted>");
        gsl_matrix_set_col(predicted_page->matrix, 0, y_data);
        gsl_matrix_set_col(predicted_page->matrix, 2, error);
        predicted   = gsl_matrix_column(predicted_page->matrix, 1).vector;
        gsl_vector_memcpy(&predicted, y_data);
        gsl_vector_add(&predicted, error); //pred = y_data + error
    }
    gsl_vector_free(error);
    if (apop_data_get_page(out->parameters, "<Covariance>"))
        apop_data_rm_page(out->parameters, "<Covariance>");
    apop_data_add_page(out->parameters, apop_matrix_to_data(cov), "<Covariance>");
}

static void ols_rng(double *out, gsl_rng *r, apop_model *m){
    //X is drawn from the input distribution, then Y = X\beta + epsilon
    apop_lm_settings   *olp =  apop_settings_get_group(m, apop_lm);
    apop_draw(out+1, r, olp->input_distribution);

    gsl_vector *tempdata = gsl_vector_alloc(m->parameters->vector->size);
    double *ttt = tempdata->data;
    tempdata->data = out+1;
    gsl_blas_ddot(tempdata, m->parameters->vector, out);
    tempdata->data = ttt;
    gsl_vector_free(tempdata);

    double sigma_sq = apop_data_get(m->info, .rowname="SSE")/m->data->matrix->size1;
    out[0] += gsl_ran_gaussian(r, sigma_sq);
}

static apop_model * apop_estimate_OLS(apop_data *inset, apop_model *ep){
    Nullcheck_d(inset)
  apop_data         *set;
//    ep->status = 0;
    apop_lm_settings   *olp =  apop_settings_get_group(ep, apop_lm);
    if (!olp) 
        olp = Apop_model_add_group(ep, apop_lm);
    ep->data = inset;
    set = olp->destroy_data ? inset : apop_data_copy(inset); 
    
    gsl_vector *weights    = olp->destroy_data      //this may be NULL.
                                ? ep->data->weights 
                                : apop_vector_copy(ep->data->weights);
    if (weights)
        for (size_t i =0; i< weights->size; i++)
            gsl_vector_set(weights, i, sqrt(gsl_vector_get(weights, i)));

  gsl_vector *y_data     = apop_vector_copy(set->vector);
    if (olp->want_expected_value)
        apop_data_add_page(ep->info, apop_data_alloc(0, set->matrix->size1, 3), "<Predicted>");
    if (olp->want_cov=='y')
        apop_data_add_page(ep->parameters, apop_data_alloc(0, set->matrix->size2, set->matrix->size2), "<Covariance>");
    if (weights)
        for (int i = -1; i < set->matrix->size2; i++){
            APOP_COL(set, i, v);
            gsl_vector_mul(v, weights);
        }

    apop_data *xpx_d = apop_dot(set, set, .form1='t'); //(X'X)
    apop_data *xpy_d = apop_dot(set, set, .form1='t', .form2='v'); //(X'y)
    xpxinvxpy(set->matrix, y_data, xpx_d->matrix, xpy_d->vector, ep);
    prep_names(ep);
    gsl_vector_free(y_data); 
    apop_data_free(xpx_d);
    apop_data_free(xpy_d);

    if (!olp->destroy_data)
        apop_data_free(set);
    if (olp->want_cov == 'y')
        apop_estimate_parameter_tests(ep);
//    ep->status       = 1;
    apop_data_add_named_elmt(ep->info, "log likelihood", ols_log_likelihood(ep->data, ep));
    apop_data *r_sq = apop_estimate_coefficient_of_determination(ep); //Add R^2-type info to info page.
    apop_data_stack(ep->info, r_sq, .inplace='y');
    apop_data_free(r_sq);
    return ep;
}

apop_data *ols_predict (apop_data *in, apop_model *m){

    if (!in->vector) //in->vector = gsl_vector_alloc(in->matrix->size1);
        ols_shuffle(in);

    //OK, data is now in the right form.
    //find x dot y
    gsl_blas_dgemv (CblasNoTrans, 1, in->matrix, m->parameters->vector, 0, in->vector);
    return in;
}

apop_model *ols_param_models(apop_data *d, apop_model *m){
    apop_pm_settings *settings = Apop_settings_get_group(m, apop_pm);
    if (settings->index!=-1){
        int i = settings->index;
        double mu = apop_data_get(m->parameters, i, -1);
        double sigma = sqrt(apop_data_get(m->parameters, i, i, .page="<Covariance>"));
        int df = apop_data_get(m->info, .rowname="df", .page = "info");
        return apop_model_set_parameters(apop_t_distribution, mu, sigma, df);
    }
    //else run the default
        void *tmp = m->parameter_model;
        m->parameter_model = NULL;
        apop_model *out = apop_parameter_model(d, m);
        m->parameter_model=tmp;
        return out;
}

#include "output.h"
void ols_print(apop_model *m){
    FILE *ap = apop_opts.output_pipe;
    fprintf(ap, "Parameters:\n");
    apop_data_print(m->parameters, .output_pipe=ap);
    apop_data *predict = apop_data_rm_page(m->info, "predict", .free_p='n');
    apop_data_print(m->info, .output_pipe=ap);
    apop_data_add_page(m->info, predict, predict->names->title);
}

apop_model apop_ols = {.name="Ordinary Least Squares", .vbase = -1, .dsize=-1, .estimate =apop_estimate_OLS, 
            .log_likelihood = ols_log_likelihood, .score=ols_score, .prep = ols_prep, .predict=ols_predict, 
            .draw=ols_rng, .parameter_model = ols_param_models, .print=ols_print};


//Instrumental variables

static apop_data *prep_z(apop_data *x, apop_data *instruments){
  int       i;
  apop_data *out    = apop_data_copy(x);
    if (instruments->vector)
        for (i=0; i< instruments->vector->size; i++){
            APOP_COL(instruments, i, inv);
            APOP_COL(out, instruments->vector->data[i], outv);
            gsl_vector_memcpy(outv, inv);
        }
    else if (instruments->names->rowct)
        for (i=0; i< instruments->names->rowct; i++){
            int rownumber = apop_name_find(x->names, instruments->names->row[i], 'c');
            apop_assert_s(rownumber != -1, "You asked me to substitute instrument column %i for the data column named %s, but I could find no such name.",  i, instruments->names->row[i]);
            APOP_COL(instruments, i, inv);
            APOP_COL(out, rownumber, outv);
            gsl_vector_memcpy(outv, inv);
        }
    else 
        apop_assert_s(0, "Your instrument matrix has data, but neither a vector element nor row names indicating what columns in the original data should be replaced.");
    return out;
}


static apop_model * apop_estimate_IV(apop_data *inset, apop_model *ep){
  Nullcheck_d(inset);
  apop_data         *set, *z;
  int               i;
    apop_lm_settings   *olp =  apop_settings_get_group(ep, apop_lm);
    if (!olp) 
        olp = Apop_model_add_group(ep, apop_lm);
    olp->want_cov       = 'n';//not working yet.
    if (!olp->instruments || !olp->instruments->matrix->size2) 
        return apop_estimate(inset, apop_ols);
    ep->data = inset;
    if(ep->parameters)
        apop_data_free(ep->parameters);
    ep->parameters = apop_data_alloc(inset->matrix->size2,0,0);
    set = olp->destroy_data ? inset : apop_data_copy(inset); 
    z   = prep_z(inset, olp->instruments);
    
    gsl_vector *weights = olp->destroy_data      //the weights may be NULL.
                             ? ep->data->weights 
                             : apop_vector_copy(ep->data->weights);
    if (weights)
        for (i =0; i< weights->size; i++)
            gsl_vector_set(weights, i, sqrt(gsl_vector_get(weights, i)));

  apop_data    *y_data     = apop_data_alloc(set->matrix->size1, 0, 0); 
    if (olp->want_expected_value)
        apop_data_add_page(ep->info, apop_data_alloc(0, set->matrix->size1, 3), "<Predicted>");
    if (olp->want_cov=='y')
        apop_data_add_page(ep->parameters, apop_data_alloc(0, set->matrix->size1, set->matrix->size1), "<Covariance>");
    prep_names(ep);
    APOP_COL(set, 0, firstcol);
    gsl_vector_memcpy(y_data->vector,firstcol);
    gsl_vector_set_all(firstcol, 1);     //affine: first column is ones.
    if (weights){
        gsl_vector_mul(y_data->vector, weights);
        for (i = 0; i < set->matrix->size2; i++){
            APOP_COL(set, i, v);
            gsl_vector_mul(v, weights);
        }
    }

    apop_data *zpx    = apop_dot(z, set, 1, 0);
    apop_data *zpy    = apop_dot(z, y_data, 1, 0);
    apop_data *zpxinv = apop_matrix_to_data(apop_matrix_inverse(zpx->matrix));
    ep->parameters = apop_dot(zpxinv, zpy, 0);
    apop_data_free(y_data);
    apop_data_free(zpx); 
    apop_data_free(zpxinv);
    apop_data_free(zpy);

    if (!olp->destroy_data)
        apop_data_free(set);
//    apop_estimate_parameter_t_tests(epout);
//    ep->status   = 1;
    return ep;
}

apop_model apop_iv = {.name="instrumental variables", .vbase = -1, .dsize=-1, .estimate =apop_estimate_IV,
    .log_likelihood = ols_log_likelihood, .print=ols_print};
