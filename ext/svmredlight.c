#include "ruby.h"
#include "svm_light/svm_common.h"
#include "string.h"

/* Helper function to determine if a model uses linear kernel, this could be a #define
 * macro */
int 
is_linear(MODEL *model){
  return model->kernel_parm.kernel_type == 0; 
}

// Modules and Classes
static VALUE rb_mSvmLight;
static VALUE rb_cModel;
static VALUE rb_cDocument;

// GC functions

/* Not using deep free anymore, let ruby call free on the documents otherwise we might end
 * up having double free problems, from svm_learn_main: Warning: The model contains
 * references to the original data 'docs'.  If you want to free the original data, and
 * only keep the model, you have to make a deep copy of 'model'.
 * deep_copy_of_model=copy_model(model); */
void 
model_free(MODEL *m){
  if(m)
    free_model(m, 0);
}

void
doc_free(DOC *d){
  if(d)
    free_example(d, 1);
}

/* Read a svm_light model from a file generated by svm_learn receives the filename as
 * argument do make sure the file exists before calling this!  otherwise exit(1) might be
 * called and the ruby interpreter will die.*/
static VALUE
model_read_from_file(VALUE klass, VALUE filename){
  Check_Type(filename, T_STRING);
  MODEL *m;

  m = read_model(StringValuePtr(filename));

  if(is_linear(m))
    add_weight_vector_to_linear_model(m);

  return Data_Wrap_Struct(klass, 0, model_free, m);
}

/* Helper function type checks a string meant to be used as a learn_parm, in case of error
 * returns 1 and sets the correct exception message in error, on success returns 0 and
 * copies the c string data of new_val to target*/
int 
check_string_param(VALUE new_val, 
                             const char *default_val, 
                             char *target, 
                             const char *name,
                             char *error){

  if(TYPE(new_val) == T_STRING){
    strlcpy(target, StringValuePtr(new_val), 199);
  }else if(NIL_P(new_val)){
    strlcpy(target, default_val, 199);
  }else{
    sprintf(error, "The value of the learning option '%s' must be a string", name);
    return 1;
  }

  return 0;
}

/* Helper function type checks a long meant to be used as a learn_parm or kernel_parm, in
 * case of error returns 1 and sets the correct exception message in error, on success
 * returns 0 and copies the c string data of new_val to target*/
int 
check_long_param(VALUE new_val, 
                           long default_val, 
                           long *target, 
                           const char *name,
                           char *error){
  if(TYPE(new_val) == T_FLOAT || TYPE(new_val) == T_FIXNUM){
    *target = FIX2LONG(new_val);
  }else if(NIL_P(new_val)){
    *target = default_val;
  }else{
    sprintf(error, "The value of the learning option '%s' must be a numeric", name);
    return 1;
  }

  return 0;
}

/* Helper function type checks a double meant to be used as a learn_parm or kernel_parm, in
 * case of error returns 1 and sets the correct exception message in error, on success
 * returns 0 and copies the c string data of new_val to target*/
int 
check_double_param(VALUE new_val, 
                         double default_val, 
                         double *target, 
                         const char *name,
                         char *error){
  if(TYPE(new_val) == T_FLOAT || TYPE(new_val) == T_FIXNUM){
    *target = NUM2DBL(new_val);
  }else if(NIL_P(new_val) ){
    *target = default_val;
  }else{
    sprintf(error, "The value of the learning option '%s' must be a numeric", name);
    return 1;
  }

  return 0;
}

/* Helper function type checks an int meant to be used as a boolean learn_parm or
 * kernel_parm, in case of error returns 1 and sets the correct exception message in
 * error, on success returns 0 and copies the c string data of new_val to target*/
int 
check_bool_param(VALUE new_val, 
                       long default_val, 
                       long *target, 
                       const char *name,
                       char *error){
  if(TYPE(new_val) == T_TRUE){
    *target = 1L;
  }else if(TYPE(new_val) == T_FALSE){
    *target = 0L;
  }else if(NIL_P(new_val) ){
    *target = default_val;
  }else{
    sprintf(error, "The value of the learning option '%s' must be a true or false", name);
    return 1;
  }

  return 0;
}

/* Helper function in charge of setting up the learn parameters before they are passed to
 * the svm_learn_classification copies part of the logic in svm_learn_main.c */
int 
setup_learn_params(LEARN_PARM *c_learn_param, VALUE r_hash, char *error_message){
  // Defaults taken from from svm_learn_main
  VALUE inter_val, temp_ary, svm_type, svm_type_ruby_str;
  char *svm_type_str;

  inter_val = rb_hash_aref(r_hash, rb_str_new2("predfile"));
  if(1 == check_string_param(inter_val, 
                                   "trans_predictions", 
                                   &c_learn_param->predfile, 
                                   "predfile",
                                   error_message)){
    return 1;
  }

  inter_val = rb_hash_aref(r_hash, rb_str_new2("alphafile"));
  if(1 == check_string_param(inter_val, 
                                   "", 
                                   &c_learn_param->alphafile, 
                                   "alphafile",
                                   error_message)){
    return 1;
  }

  inter_val = rb_hash_aref(r_hash, rb_str_new2("biased_hyperplane"));
  if(1 == check_bool_param(inter_val, 
                                 1L, 
                                 &(c_learn_param->biased_hyperplane), 
                                 "biased_hyperplane",
                                 error_message)){
    return 1;
  }

  inter_val = rb_hash_aref(r_hash, rb_str_new2("sharedslack"));
  if(1 == check_bool_param(inter_val, 
                                 0L, 
                                 &(c_learn_param->sharedslack), 
                                 "sharedslack",
                                 error_message)){
    return 1;
  }

  inter_val = rb_hash_aref(r_hash, rb_str_new2("remove_inconsistent"));
  if(1 == check_bool_param(inter_val, 
                                 0L, 
                                 &(c_learn_param->remove_inconsistent), 
                                 "remove_inconsistent",
                                 error_message)){
    return 1;
  }

  inter_val = rb_hash_aref(r_hash, rb_str_new2("skip_final_opt_check"));
  if(1 == check_bool_param(inter_val, 
                                 0L, 
                                 &(c_learn_param->skip_final_opt_check), 
                                 "skip_final_opt_check",
                                 error_message)){
    return 1;
  }

  inter_val = rb_hash_aref(r_hash, rb_str_new2("svm_newvarsinqp"));
  if(1 == check_bool_param(inter_val, 
                                 0L, 
                                 &(c_learn_param->svm_newvarsinqp), 
                                 "svm_newvarsinqp",
                                 error_message)){
    return 1;
  }

  inter_val = rb_hash_aref(r_hash, rb_str_new2("compute_loo"));
  if(1 == check_bool_param(inter_val, 
                                 0L, 
                                 &(c_learn_param->compute_loo), 
                                 "compute_loo",
                                 error_message)){
    return 1;
  }


  inter_val = rb_hash_aref(r_hash, rb_str_new2("svm_maxqpsize"));
  if(1 == check_long_param(inter_val, 
                                 10L, 
                                 &(c_learn_param->svm_maxqpsize), 
                                 "svm_maxqpsize",
                                 error_message)){
    return 1;
  }
  
  inter_val = rb_hash_aref(r_hash, rb_str_new2("svm_iter_to_shrink"));
  if(1 == check_long_param(inter_val, 
                                 -9999, 
                                 &(c_learn_param->svm_iter_to_shrink), 
                                 "svm_iter_to_shrink",
                                 error_message)){
    return 1;
  }

  inter_val = rb_hash_aref(r_hash, rb_str_new2("maxiter"));
  if(1 == check_long_param(inter_val, 
                                 100000, 
                                 &(c_learn_param->maxiter), 
                                 "maxiter",
                                 error_message)){
    return 1;
  }

  inter_val = rb_hash_aref(r_hash, rb_str_new2("kernel_cache_size"));
  if(1 == check_long_param(inter_val, 
                                 40L, 
                                 &(c_learn_param->kernel_cache_size), 
                                 "kernel_cache_size",
                                 error_message)){
    return 1;
  }

  inter_val = rb_hash_aref(r_hash, rb_str_new2("xa_depth"));
  if(1 == check_long_param(inter_val, 
                                 0L, 
                                 &(c_learn_param->xa_depth), 
                                 "xa_depth",
                                 error_message)){
    return 1;
  }

  inter_val = rb_hash_aref(r_hash, rb_str_new2("svm_c"));
  if(1 == check_double_param(inter_val, 
                                 0.0, 
                                 &(c_learn_param->svm_c), 
                                 "svm_c",
                                 error_message)){
    return 1;
  }

  inter_val = rb_hash_aref(r_hash, rb_str_new2("eps"));
  if(1 == check_double_param(inter_val, 
                                 0.1, 
                                 &(c_learn_param->eps), 
                                 "eps",
                                 error_message)){
    return 1;
  }

  inter_val = rb_hash_aref(r_hash, rb_str_new2("transduction_posratio"));
  if(1 == check_double_param(inter_val, 
                                 -1.0, 
                                 &(c_learn_param->transduction_posratio), 
                                 "transduction_posratio",
                                 error_message)){
    return 1;
  }

  inter_val = rb_hash_aref(r_hash, rb_str_new2("svm_costratio"));
  if(1 == check_double_param(inter_val, 
                                 1.0, 
                                 &(c_learn_param->svm_costratio), 
                                 "svm_costratio",
                                 error_message)){
    return 1;
  }

  inter_val = rb_hash_aref(r_hash, rb_str_new2("svm_costratio_unlab"));
  if(1 == check_double_param(inter_val, 
                                 1.0, 
                                 &(c_learn_param->svm_costratio_unlab), 
                                 "svm_costratio_unlab",
                                 error_message)){
    return 1;
  }

  inter_val = rb_hash_aref(r_hash, rb_str_new2("svm_unlabbound"));
  if(1 == check_double_param(inter_val, 
                                 1.0000000000000001e-05, 
                                 &(c_learn_param->svm_unlabbound), 
                                 "svm_unlabbound",
                                 error_message)){
    return 1;
  }

  inter_val = rb_hash_aref(r_hash, rb_str_new2("epsilon_crit"));
  if(1 == check_double_param(inter_val,
                                 0.001, 
                                 &(c_learn_param->epsilon_crit), 
                                 "epsilon_crit",
                                 error_message)){
    return 1;
  }

  inter_val = rb_hash_aref(r_hash, rb_str_new2("epsilon_a"));
  if(1 == check_double_param(inter_val, 
                                 1E-15, 
                                 &(c_learn_param->epsilon_a), 
                                 "epsilon_a",
                                 error_message)){
    return 1;
  }

  c_learn_param->rho=1.0;
  inter_val = rb_hash_aref(r_hash, rb_str_new2("rho"));
  if(1 == check_double_param(inter_val, 
                                 1.0, 
                                 &(c_learn_param->rho), 
                                 "rho",
                                 error_message)){
    return 1;
  }


  return 0;
}

int 
setup_kernel_params(KERNEL_PARM *c_kernel_param, VALUE r_hash, char *error_message){
  VALUE inter_val;
  inter_val = rb_hash_aref(r_hash, rb_str_new2("poly_degree"));
  if(1 == check_long_param(inter_val, 
                                 3L, 
                                 &(c_kernel_param->poly_degree), 
                                 "poly_degree",
                                 error_message)){
    return 1;
  }
  
  inter_val = rb_hash_aref(r_hash, rb_str_new2("rbf_gamma"));
  if(1 == check_double_param(inter_val, 
                                 1.0, 
                                 &(c_kernel_param->rbf_gamma), 
                                 "rbf_gamma",
                                 error_message)){
    return 1;
  }
  
  inter_val = rb_hash_aref(r_hash, rb_str_new2("coef_lin"));
  if(1 == check_double_param(inter_val, 
                                 1.0, 
                                 &(c_kernel_param->coef_lin), 
                                 "coef_lin",
                                 error_message)){
    return 1;
  }

  inter_val = rb_hash_aref(r_hash, rb_str_new2("coef_const"));
  if(1 == check_double_param(inter_val, 
                                 1.0, 
                                 &(c_kernel_param->coef_const), 
                                 "coef_const",
                                 error_message)){
    return 1;
  }
  
  // No support for custom kernel yet just set it to empty
  strlcpy(c_kernel_param->custom,"empty", 49);
  return 0;
}

/* Do logic checks for the learn and kernel params, this logic is copied from
 * svm_learn_main.c */
int check_kernel_and_learn_params_logic(KERNEL_PARM *c_kernel_param, 
    LEARN_PARM *c_learn_param, char *error_msg){

  if(c_learn_param->svm_iter_to_shrink == -9999) {
    if(c_kernel_param->kernel_type == LINEAR) 
      c_learn_param->svm_iter_to_shrink=2;
    else
      c_learn_param->svm_iter_to_shrink=100;
  }

  //It does not make sense to skip the final optimality check for linear kernels.
  if((c_learn_param->skip_final_opt_check) 
     && (c_kernel_param->kernel_type == LINEAR)) {
    c_learn_param->skip_final_opt_check=0;
  }    

  if((c_learn_param->skip_final_opt_check) 
     && (c_learn_param->remove_inconsistent)) {
    strncpy(error_msg,"It is necessary to do the final optimality "
        "check when removing inconsistent examples.", 300);
    return 1;
  }

  if((c_learn_param->svm_maxqpsize<2)) {
    snprintf(error_msg, 300, "Maximum size of QP-subproblems "
        "not in valid range: %ld [2..]",c_learn_param->svm_maxqpsize); 
    return 1;
  }

  if((c_learn_param->svm_maxqpsize<c_learn_param->svm_newvarsinqp)) {
    snprintf(error_msg, 300, "Maximum size of QP-subproblems [%ld] must be larger than the number of",
            "new variables [%ld] entering the working set in each iteration.\n",c_learn_param->svm_maxqpsize
             ,c_learn_param->svm_newvarsinqp); 
    return 1;
  }

  if(c_learn_param->svm_iter_to_shrink<1) {
    snprintf(error_msg, 300, "Maximum number of iterations for shrinking not"
        " in valid range: %ld [1,..]",c_learn_param->svm_iter_to_shrink);
    return 1;
  }

  if(c_learn_param->svm_c<0) {
    strncpy(error_msg,"The C parameter must be greater than zero", 300);
    return 1;
  }

  if(c_learn_param->transduction_posratio>1) {
    strncpy(error_msg,"The fraction of unlabeled examples to classify as positives must"
        "be less than 1.0", 300);
    return 1;
  }

  if(c_learn_param->svm_costratio<=0) {
    strncpy(error_msg,"The COSTRATIO parameter must be greater than zero", 300);
    return 1;
  }

  if(c_learn_param->epsilon_crit<=0) {
    strncpy(error_msg,"The epsilon parameter must be greater than zero", 300);
    return 1;
  }

  if(c_learn_param->rho<0) {
    strncpy(error_msg, "The parameter rho for xi/alpha-estimates and leave-one-out pruning must"
            "be greater than zero (typically 1.0 or 2.0, see T. Joachims, Estimating the"
            "Generalization Performance of an SVM Efficiently, ICML, 2000.)!", 300);
    return 1;
  }

  if((c_learn_param->xa_depth<0) || (c_learn_param->xa_depth>100)) {
    strncpy(error_msg, "The parameter depth for ext. xi/alpha-estimates must be in [0..100] (zero) "
            "for switching to the conventional xa/estimates described in T. Joachims,"
            "Estimating the Generalization Performance of an SVM Efficiently, ICML, 2000.)", 300);
    return 1;
  }

  return 0;
}

/* This function will let you train a new SVM model, for now we *only* support
 * classification SVMs and linear kernels, in ruby-land the kernel and learning params
 * will be represented by hashes where the keys are the name of the respective field in
 * the options structure
 *
 * @param [Array] r_docs_and_classes is an array of arrays where each of the inner arrays must have two elements, the first a Document and the second a label (1, -1 ) for classification
 * @param [Hash] learn_params the learning options, each key is the name of a filed in the LEARN_PARM struct
 * @param [Hash] kernel_params the kernel options, each key is the name of a filed in the KERNEL_PARM struct
 * @param [Bool] use_cache, useless for now caches cannot be set for linear kernels
 * @param [Array] alpha, array of alpha values
 * */
static VALUE
model_learn_classification(VALUE klass, 
                           VALUE r_docs_and_classes,  // Docs + labels array of arrays
                           VALUE learn_params,        // Options hash with learning options
                           VALUE kernel_params,       // Options hash with kernel options
                           VALUE use_cache,          // If no linear
                           VALUE alpha
                          ){
  int i;
  double *labels = NULL, *alpha_in = NULL;
  long totdocs, totwords = 0,  fnum = 0;
  MODEL  *m = NULL;
  DOC    **c_docs = NULL;
  LEARN_PARM c_learn_param;
  KERNEL_PARM c_kernel_param;
  VALUE temp_ary, exception = rb_eArgError;
  char error_msg[300];

  Check_Type(r_docs_and_classes, T_ARRAY);
  Check_Type(learn_params, T_HASH);
  Check_Type(kernel_params, T_HASH);

  if(!(TYPE(alpha) == T_ARRAY || NIL_P(alpha) ))
    rb_raise(rb_eTypeError, "alpha must be an numeric array or nil");
  
  if(TYPE(alpha) == T_ARRAY){

    alpha_in = my_malloc(sizeof(double) * RARRAY_LEN(alpha));

    for(i=0; i < RARRAY_LEN(alpha); i++){

      if(TYPE(RARRAY_PTR(alpha)[i]) != T_FLOAT && 
         TYPE(RARRAY_PTR(alpha)[i]) != T_FIXNUM ){

        strncpy(error_msg,"All elements of the alpha array must be numeric ", 300);
        goto bail;
      }
      
      alpha_in[i] = NUM2DBL(RARRAY_PTR(alpha)[i]);
    }
  }

  if(setup_learn_params(&c_learn_param, learn_params, error_msg) != 0){
    goto bail;
  }

  c_learn_param.type = CLASSIFICATION;

  if(setup_kernel_params(&c_kernel_param, kernel_params, error_msg) != 0){
    goto bail;
  }

  //TODO Setup kernel cache when we support non linear kernels
  c_kernel_param.kernel_type = LINEAR;

  if(check_kernel_and_learn_params_logic(&c_kernel_param, &c_learn_param, error_msg) != 0){
    goto bail;
  }

  totdocs = (long)RARRAY_LEN(r_docs_and_classes);

  if (totdocs == 0){
    strncpy(error_msg, "Cannot create Model from empty Documents array", 300);
    goto bail;
  }
  
  c_docs  = (DOC **)my_malloc(sizeof(DOC *)*(totdocs)); 
  labels  = (double*)my_malloc(sizeof(double)*totdocs);

  for(i=0; i < totdocs; i++){
    // Just one of the documents and classes arrays, we expect temp_ary to have a Document
    // and a label (long)
    temp_ary = RARRAY_PTR(r_docs_and_classes)[i] ;

    if( TYPE(temp_ary) != T_ARRAY || 
        RARRAY_LEN(temp_ary) < 2  ||
        rb_obj_class(RARRAY_PTR(temp_ary)[0]) != rb_cDocument ||  
        (TYPE(RARRAY_PTR(temp_ary)[1]) != T_FLOAT && TYPE(RARRAY_PTR(temp_ary)[1]) != T_FIXNUM )){
      
      strncpy(error_msg, "All elements of documents and labels should be arrays,"
          "where the first element is a document and the second a number", 300);

      goto bail;
    }
      
    Data_Get_Struct(RARRAY_PTR(temp_ary)[0], DOC, c_docs[i]);
    labels[i] = NUM2DBL(RARRAY_PTR(temp_ary)[1]);

    fnum = 0;

    // Increase feature number while there are still words in the vector
    while(c_docs[i]->fvec->words[fnum].wnum) {
      fnum++;
    }
    
    if(c_docs[i]->fvec->words[fnum -1].wnum > totwords)
      totwords = c_docs[i]->fvec->words[fnum-1].wnum;

    if(totwords > MAXFEATNUM){
      strncpy(error_msg, "The number of features exceeds MAXFEATNUM the maximun "
                    "number of features defined for this version of SVMLight", 300);
      goto bail;
    }
  }
  
  m = (MODEL *)my_malloc(sizeof(MODEL));

  svm_learn_classification(c_docs, labels, totdocs, totwords, 
      &c_learn_param, &c_kernel_param, NULL, m, alpha_in);

  free(alpha_in);
  free(labels);

  // If need arises to free the data do a deep copy of m and create the ruby object with
  // that data.
  // free(c_docs);
  return Data_Wrap_Struct(klass, 0, model_free, m);

bail:
  free(alpha_in);
  free(labels);
  free(c_docs);
  rb_raise(exception, error_msg, "%s");
}

/*  Classify, takes an example (instance of Document) and returns its classification */
static VALUE
model_classify_example(VALUE self, VALUE example){
  DOC *ex;
  MODEL *m;
  double result;

  Data_Get_Struct(example, DOC, ex);
  Data_Get_Struct(self, MODEL, m);

  /* Apparently unnecessary code 
   
  if(is_linear(m))
    result = classify_example_linear(m, ex);
  else
  */
  
  result = classify_example(m, ex);

  return rb_float_new((float)result);
}

static VALUE
model_support_vectors_count(VALUE self){
  MODEL *m;
  Data_Get_Struct(self, MODEL, m);
 
  return INT2FIX(m->sv_num);
}

static VALUE
model_total_words(VALUE self){
  MODEL *m;
  Data_Get_Struct(self, MODEL, m);

  return INT2FIX(m->totwords);
}

static VALUE
model_totdoc(VALUE self){
  MODEL *m;
  Data_Get_Struct(self, MODEL, m);

  return INT2FIX(m->totdoc);
}

static VALUE
model_maxdiff(VALUE self){
  MODEL *m;
  Data_Get_Struct(self, MODEL, m);

  return DBL2NUM(m->maxdiff);
}

/* Creates a DOC from an array of words it also takes an id
 * -1 is normally OK for that value when using in filtering it also takes the C (cost)
 * parameter for the SVM.
 * words_ary an array of arrays like this
 * [[wnum, weight], [wnum, weight], ...] so we do not waste memory, defeating the svec implementation and do
 * not introduce a bunch of 0's that seem to be OK when classifying but screw all up on
 * training*/
static VALUE
doc_create(VALUE klass, VALUE id, VALUE cost, VALUE slackid, VALUE queryid, VALUE words_ary ){
  long docnum, i, c_slackid, c_queryid;
  double c;
  WORD *words;
  SVECTOR *vec;
  DOC *d;
  VALUE inner_array;

  Check_Type(words_ary, T_ARRAY);
  Check_Type(slackid, T_FIXNUM);
  Check_Type(queryid, T_FIXNUM);

  if (RARRAY_LEN(words_ary) == 0)
    rb_raise(rb_eArgError, "Cannot create Document from empty arrays");

  words = (WORD*) my_malloc(sizeof(WORD) * (RARRAY_LEN(words_ary) + 1));

  for(i=0; i < (long)RARRAY_LEN(words_ary); i++){
    inner_array = RARRAY_PTR(words_ary)[i];
    Check_Type(inner_array, T_ARRAY);
    Check_Type(RARRAY_PTR(inner_array)[0], T_FIXNUM);

    if(!(TYPE(RARRAY_PTR(inner_array)[1]) == T_FLOAT ||  TYPE(RARRAY_PTR(inner_array)[1]) == T_FIXNUM ))
      rb_raise(rb_eArgError, "Feature weights must be numeric");
    
    if(FIX2LONG(RARRAY_PTR(inner_array)[0]) <= 0 )
      rb_raise(rb_eArgError, "Feature number has to be greater than zero");

    (words[i]).wnum     = FIX2LONG(RARRAY_PTR(inner_array)[0]);
    (words[i]).weight   = (FVAL)(NUM2DBL(RARRAY_PTR(inner_array)[1]));
  }
  words[i].wnum = 0;

  vec    = create_svector(words, (char*)"", 1.0);
  c      = NUM2DBL(cost);
  docnum = FIX2INT(id);

  d = create_example(docnum, FIX2LONG(queryid), FIX2LONG(slackid), c, vec);

  return Data_Wrap_Struct(klass, 0, doc_free, d);
}

static VALUE
doc_get_docnum(VALUE self){
  DOC *d;
  Data_Get_Struct(self, DOC, d);
 
  return INT2FIX(d->docnum);
}

static VALUE
doc_get_slackid(VALUE self){
  DOC *d;
  Data_Get_Struct(self, DOC, d);
 
  return INT2FIX(d->slackid);
}

static VALUE
doc_get_queryid(VALUE self){
  DOC *d;
  Data_Get_Struct(self, DOC, d);
 
  return INT2FIX(d->queryid);
}

static VALUE
doc_get_costfactor(VALUE self){
  DOC *d;
  Data_Get_Struct(self, DOC, d);
 
  return DBL2NUM(d->costfactor);
}

void
Init_svmredlight(){
  rb_mSvmLight = rb_define_module("SVMLight");
  //Model
  rb_cModel = rb_define_class_under(rb_mSvmLight, "Model", rb_cObject);
  rb_define_singleton_method(rb_cModel, "read_from_file", model_read_from_file, 1);
  rb_define_singleton_method(rb_cModel, "learn_classification", model_learn_classification, 5);
  rb_define_method(rb_cModel, "support_vectors_count", model_support_vectors_count, 0);
  rb_define_method(rb_cModel, "total_words", model_total_words, 0);
  rb_define_method(rb_cModel, "classify", model_classify_example, 1);
  rb_define_method(rb_cModel, "totdoc", model_totdoc,0);
  rb_define_method(rb_cModel, "maxdiff", model_maxdiff,0);
  //Document
  rb_cDocument = rb_define_class_under(rb_mSvmLight, "Document", rb_cObject);
  rb_define_singleton_method(rb_cDocument, "create", doc_create, 5);
  rb_define_method(rb_cDocument, "docnum", doc_get_docnum, 0);
  rb_define_method(rb_cDocument, "costfactor", doc_get_costfactor, 0);
  rb_define_method(rb_cDocument, "slackid", doc_get_slackid, 0);
  rb_define_method(rb_cDocument, "queryid", doc_get_queryid, 0);
}
