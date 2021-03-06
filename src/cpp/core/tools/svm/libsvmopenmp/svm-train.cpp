#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <iostream>
#include "svm-train.h"
#include "core/util/Config.h"
#include "core/opencl/libsvm/OpenCLToolsTrain.h"


#define Malloc(type,n) (type *)malloc((n)*sizeof(type))

namespace core{
    namespace tools{
        namespace svm{
            namespace libsvmopenmp {

                using namespace std;
                using namespace core::util;                
#ifdef _OPENCL                
                using namespace core::opencl::libsvm;
#endif
                
                void print_null(const char *s) {
                }                

                void exit_input_error(int line_num) {
                    fprintf(stderr, "Wrong input format at line %d\n", line_num);
                    exit(1);
                }

                void parse_command_line(int argc, char **argv, char *input_file_name, char *model_file_name);
                void read_problem(const char *filename);
                void do_cross_validation();

                struct svm_parameter param; // set by parse_command_line
                struct svm_problem prob; // set by read_problem
                struct svm_model *model;
                struct svm_node *x_space;
                int cross_validation;
                int nr_fold;

                static char *line = NULL;
                static int max_line_len;

                static char* readline(FILE *input) {
                    int len;

                    if (fgets(line, max_line_len, input) == NULL)
                        return NULL;

                    while (strrchr(line, '\n') == NULL) {
                        max_line_len *= 2;
                        line = (char *) realloc(line, max_line_len);
                        len = (int) strlen(line);
                        if (fgets(line + len, max_line_len - len, input) == NULL)
                            break;
                    }
                    return line;
                }

                int train(char* input_file_name, char* model_file_name) throw(SDException&){
                    cout << "Start training" << endl;
#ifdef _OPENCL
                    OpenCLToolsTrain* oclt = OpenCLToolsTrain::getInstancePtr();
                    if (oclt->hasInitialized() == false){
                        SDException exc(SHADOW_OPENCL_TOOLS_NOT_INITIALIZED, "libsvm train");
                        throw exc;
                    }
#endif
                    Config* conf = Config::getInstancePtr();
                    
                    param.svm_type = C_SVC;
                    param.kernel_type = RBF;
                    param.degree = 3;
                    param.gamma = 0;	// 1/num_features
                    param.coef0 = 0;
                    param.nu = 0.5;
                    param.cache_size = 100;
                    param.C = 1;
                    param.eps = 1e-3;
                    param.p = 0.1;
                    param.shrinking = 1;
                    param.probability = 0;
                    param.nr_weight = 0;
                    param.weight_label = NULL;
                    param.weight = NULL;
                    cross_validation = 0;
                    
                    string strVal = conf->getPropertyValue("settings.svm.svm_type");
                    int val = atoi(strVal.c_str());
                    if (val >= 0 && val < 5)
                        param.svm_type = val;
                    else{
                        SDException e(SHADOW_INALID_SVM_TYPE, "train");
                        throw e;
                    }
                    strVal = conf->getPropertyValue("settings.svm.kernel_type");
                    val = atoi(strVal.c_str());
                    if (val >= 0 && val < 4)
                        param.kernel_type = val;
                    else{                        
                        SDException e(SHADOW_INVALID_KERNEL_TYPE, "train");
                        throw e;
                    }
                    
                    void (*print_func)(const char*) = NULL;	// default printing to stdout
                    svm_set_print_string_function(print_func);
                                        
                    cout << "Start read problem" << endl;
                    read_problem(input_file_name);
                    cout << "Finished read problem" << endl;
                    model = svm_train(&prob, &param);
                    if (svm_save_model(model_file_name, model)) {
                        fprintf(stderr, "can't save model to file %s\n", model_file_name);
                        exit(1);
                    }
                    svm_free_and_destroy_model(&model);

                    svm_destroy_param(&param);
                    free(prob.y);
                    free(prob.x);
                    free(x_space);
                    free(line);

#ifdef _OPENCL                                        
                    cout << "data durr: " << oclt->durrData << " buff durr: " << oclt->durrBuff << " durr exec: " << oclt->durrExec;
                    cout << " durr set args: " << oclt->durrSetSrgs << " durr readbuff: " << oclt->durrReadBuff << endl;
#endif
                    
                    return 0;
                }

                void do_cross_validation() {
                    int i;
                    int total_correct = 0;
                    double total_error = 0;
                    double sumv = 0, sumy = 0, sumvv = 0, sumyy = 0, sumvy = 0;
                    double *target = Malloc(double, prob.l);

                    svm_cross_validation(&prob, &param, nr_fold, target);
                    if (param.svm_type == EPSILON_SVR ||
                            param.svm_type == NU_SVR) {
                        for (i = 0; i < prob.l; i++) {
                            double y = prob.y[i];
                            double v = target[i];
                            total_error += (v - y)*(v - y);
                            sumv += v;
                            sumy += y;
                            sumvv += v*v;
                            sumyy += y*y;
                            sumvy += v*y;
                        }
                        printf("Cross Validation Mean squared error = %g\n", total_error / prob.l);
                        printf("Cross Validation Squared correlation coefficient = %g\n",
                                ((prob.l * sumvy - sumv * sumy)*(prob.l * sumvy - sumv * sumy)) /
                                ((prob.l * sumvv - sumv * sumv)*(prob.l * sumyy - sumy * sumy))
                                );
                    } else {
                        for (i = 0; i < prob.l; i++)
                            if (target[i] == prob.y[i])
                                ++total_correct;
                        printf("Cross Validation Accuracy = %g%%\n", 100.0 * total_correct / prob.l);
                    }
                    free(target);
                }                

                // read in a problem (in svmlight format)
                void read_problem(const char *filename) {
                    int elements, max_index, inst_max_index, i, j;
                    FILE *fp = fopen(filename, "r");
                    char *endptr;
                    char *idx, *val, *label;

                    if (fp == NULL) {
                        fprintf(stderr, "can't open input file %s\n", filename);
                        exit(1);
                    }

                    prob.l = 0;
                    elements = 0;

                    max_line_len = 1024;
                    line = Malloc(char, max_line_len);
                    while (readline(fp) != NULL) {
                        char *p = strtok(line, " \t"); // label

                        // features
                        while (1) {
                            p = strtok(NULL, " \t");
                            if (p == NULL || *p == '\n') // check '\n' as ' ' may be after the last feature
                                break;
                            ++elements;
                        }
                        ++elements;
                        ++prob.l;
                    }
                    rewind(fp);

                    prob.y = Malloc(double, prob.l);
                    prob.x = Malloc(struct svm_node *, prob.l);
                    x_space = Malloc(struct svm_node, elements);

                    max_index = 0;
                    j = 0;
                    for (i = 0; i < prob.l; i++) {
                        inst_max_index = -1; // strtol gives 0 if wrong format, and precomputed kernel has <index> start from 0
                        readline(fp);
                        prob.x[i] = &x_space[j];
                        label = strtok(line, " \t\n");
                        if (label == NULL) // empty line
                            exit_input_error(i + 1);

                        prob.y[i] = strtod(label, &endptr);
                        if (endptr == label || *endptr != '\0')
                            exit_input_error(i + 1);

                        while (1) {
                            idx = strtok(NULL, ":");
                            val = strtok(NULL, " \t");

                            if (val == NULL)
                                break;

                            errno = 0;
                            x_space[j].index = (int) strtol(idx, &endptr, 10);
                            if (endptr == idx || errno != 0 || *endptr != '\0' || x_space[j].index <= inst_max_index)
                                exit_input_error(i + 1);
                            else
                                inst_max_index = x_space[j].index;

                            errno = 0;
                            x_space[j].value = strtod(val, &endptr);
                            if (endptr == val || errno != 0 || (*endptr != '\0' && !isspace(*endptr)))
                                exit_input_error(i + 1);

                            ++j;
                        }

                        if (inst_max_index > max_index)
                            max_index = inst_max_index;
                        x_space[j++].index = -1;
                    }

                    if (param.gamma == 0 && max_index > 0)
                        param.gamma = 1.0 / max_index;

                    if (param.kernel_type == PRECOMPUTED)
                        for (i = 0; i < prob.l; i++) {
                            if (prob.x[i][0].index != 0) {
                                fprintf(stderr, "Wrong input format: first column must be 0:sample_serial_number\n");
                                exit(1);
                            }
                            if ((int) prob.x[i][0].value <= 0 || (int) prob.x[i][0].value > max_index) {
                                fprintf(stderr, "Wrong input format: sample_serial_number out of range\n");
                                exit(1);
                            }
                        }

                    fclose(fp);
                }

            }
        }
    }
}
