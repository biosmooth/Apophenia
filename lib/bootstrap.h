#include <gsl/gsl_matrix.h>

gsl_matrix * apop_jackknife(apop_data *data, apop_model model,
                                 apop_estimation_params e);
gsl_rng *apop_rng_alloc(int seed);
