#include "apop_internal.h"

/* \amodel apop_dcomposition Use random draws or parameter estimates output from a first model as input data for a second model.

\li This is still in beta. Expect the interface to change.

\adoc Input_format The input data is sent to the first model, so use the input format for that model.
\adoc Settings   \ref apop_composition_settings

*/

typedef struct {
    apop_model *generator_m;
    apop_model *ll_m;
    gsl_rng *rng;
    int draw_ct;
} apop_composition_settings;/**< All of the elements of this struct should be considered private.*/

Apop_settings_copy(apop_composition,)

Apop_settings_free(apop_composition,)

Apop_settings_init(apop_composition,
    Apop_varad_set(draw_ct, 1e4);
    Apop_varad_set(rng, apop_rng_alloc(apop_opts.rng_seed++));
)

#define Get_cs(inmodel, outval) \
    apop_composition_settings *cs = Apop_settings_get_group(inmodel, apop_composition); \
    Apop_stopif(!cs, return outval, 0, "At this point, I expect your model to" \
            "have an apop_compose settings group. Perhaps set up the " \
            "model using apop_model_dcompose");

static int unpack(apop_model *m){
   //predict table --> real param set
   Get_cs(m, -1)
   int generator_psize = cs->generator_m->vbase + cs->generator_m->m1base + cs->generator_m->m2base;
   int ll_psize = cs->ll_m->vbase + cs->ll_m->m1base + cs->ll_m->m2base;
   Apop_stopif(generator_psize+ll_psize && !m->parameters, return -1,
           0, "Trying to use parameters for an apop_dcompose model, but they are NULL. Run apop_estimate() first?");

   if (generator_psize){
       Apop_data_rows(m->parameters, 0, generator_psize, genparams);
       apop_data_unpack(genparams->vector, cs->generator_m->parameters);
   }
   if (ll_psize){
       Apop_data_rows(m->parameters, generator_psize, ll_psize, llparams);
       apop_data_unpack(llparams->vector, cs->ll_m->parameters);
   }
   return 0;
}

static int pack(apop_model *m){
    Get_cs(m, -1);
   int generator_psize = cs->generator_m->vbase + cs->generator_m->m1base + cs->generator_m->m2base;
   int ll_psize = cs->ll_m->vbase + cs->ll_m->m1base + cs->ll_m->m2base;

   if (generator_psize){
       Apop_data_rows(m->parameters, 0, generator_psize, genparams);
       apop_data_pack(cs->generator_m->parameters, genparams->vector);

   }
   if (ll_psize){
       Apop_data_rows(m->parameters, generator_psize, ll_psize, llparams);
       apop_data_pack(cs->ll_m->parameters, llparams->vector);
   }
   return 0;
}

static void compose_prep(apop_data *d, apop_model *m){
    apop_composition_settings *cs = Apop_settings_get_group(m, apop_composition); 
    Apop_stopif(!cs, m->error='s', 0, "missing apop_composition_settings group. "
            "Maybe initialize this with apop_model_dcompose?");

   int gen_psize = cs->generator_m->vbase + cs->generator_m->m1base + cs->generator_m->m2base;
   int ll_psize = cs->ll_m->vbase + cs->ll_m->m1base + cs->ll_m->m2base;

    if (gen_psize && !cs->generator_m->parameters) apop_prep(d, cs->generator_m);
    if (ll_psize && !cs->ll_m->parameters)         apop_prep(d, cs->ll_m);

    m->vbase = gen_psize + ll_psize;
    if (m->vbase) {
        m->parameters = apop_data_alloc(m->vbase);
        gsl_vector_set_all(m->parameters->vector, GSL_NAN);
    }
    pack(m);

    m->dsize = cs->ll_m->dsize;
}

static double compose_ll(apop_data *indata, apop_model*composition){
    Get_cs(composition, GSL_NAN)
    Apop_stopif(unpack(composition), return GSL_NAN, 0, "Trouble unpacking parameters.");
    apop_data *draws = apop_data_alloc(cs->draw_ct, cs->generator_m->dsize);
    for (int i=0; i< cs->draw_ct; i++){
        Apop_row(draws, i, onerow);
        apop_draw(onerow->data, cs->rng, cs->generator_m);
    }
    double ll = apop_log_likelihood(draws, cs->ll_m);
    apop_data_free(draws);
    return ll;
}

static double composed_constraint(apop_data *data, apop_model *m){
    Get_cs(m, GSL_NAN)
    Apop_stopif(unpack(m), return GSL_NAN, 0, "Trouble unpacking parameters.");
    if(!cs->generator_m->constraint && !cs->ll_m->constraint) return 0;

    double penalty = 
            (cs->generator_m->constraint ? cs->generator_m->constraint(data, cs->generator_m) : 0)
          + (cs->ll_m->constraint ? cs->ll_m->constraint(NULL, cs->ll_m) : 0);
    pack(m);
    return penalty;
}


apop_model apop_composition = {"Data-composed model", .prep=compose_prep, .log_likelihood=compose_ll, .constraint=composed_constraint};


/** \def apop_model_dcompose
<em>Data composition</em> is using either random draws or parameter estimates from
the output of one model as the input data for another model. 

\li The \ref apop_dcomposition model relies on the \ref apop_composition_settings struct, qv. This macro takes the elements of that struct as input. You can use the designated initializer syntax to specify them.

\return An \ref apop_model that is a copy of \ref apop_composition.

\ingroup model_transformations
*/
