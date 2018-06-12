#include <pthread.h>
#include "SC_Lock.h"
#include "SC_PlugIn.h"
#include "GRT.h"

#define INTERVAL 250

enum tasks {
    TASK_STOP = 0,
    TASK_IDLE = 1,
    TASK_REGRESSION = 2,
    TASK_CLASSIFICATION = 3,
    TASK_LOADPIPELINE = 4,
    TASK_LOADDATASET = 5,
    TASK_INIT = 6
};

// InterfaceTable contains pointers to functions in the host (server).
static InterfaceTable *ft;

// declare struct to hold unit generator state
struct GestureRT : public Unit {
    // in later examples, we will declare state variables here.
    int inputCount;
    int currentTask;
    int grtTask = TASK_REGRESSION;
    float output;
    pthread_t grtThread;
    //GRT::RegressionData* trainingData;     
    GRT::GestureRecognitionPipeline* pipeline;
    GRT::VectorFloat* inputVector; 
};

//Threading stuff
void *grt_update_func(void *param) {

    GestureRT * unit = (GestureRT*) param; 

    while (unit->currentTask > TASK_STOP) {

        switch (unit->currentTask) {
            case TASK_IDLE: //idle
                break;

            case TASK_REGRESSION: //regression
                if (unit->pipeline->predict(*(unit->inputVector))) {
                    unit->output = unit->pipeline->getRegressionData()[0];
                }
                break;

            case TASK_CLASSIFICATION: //classification
                if (unit->pipeline->predict(*(unit->inputVector))) {
                    unit->output = unit->pipeline->getPredictedClassLabel();
                }

                break;
            case TASK_LOADPIPELINE: //loadPipeline

                break;
            case TASK_LOADDATASET: //loadDataset

                break;

            case TASK_INIT: //init
                break;
        };

        unit->currentTask = TASK_IDLE;
        //Do stuff

        std::this_thread::sleep_for( std::chrono::duration<float, std::milli>(INTERVAL)  );
    }
    //We need to return something
    return NULL;
}

// older plugins wrap these function declarations in 'extern "C" { ... }'
// no need to do that these days.
static void GestureRT_next(GestureRT* unit, int inNumSamples);
static void GestureRT_next_noTrain(GestureRT* unit, int inNumSamples);
static void GestureRT_Ctor(GestureRT* unit);

void GestureRTLoadPipeline (Unit *unit, struct sc_msg_iter *args); 
void GestureRTLoadDataset (Unit *unit, struct sc_msg_iter *args); 

// the constructor function is called when a Synth containing this ugen is played.
// it MUST be named "PluginName_Ctor", and the argument must be "unit."
void GestureRT_Ctor(GestureRT* unit) {

    //Set input vector size to size of input array
    int inputVectorSize = IN0(0);

    unit->currentTask = 1;

    pthread_create(&(unit->grtThread), NULL, grt_update_func, unit);


    DefineUnitCmd("GestureRT", "loadDataset", GestureRTLoadDataset);
    DefineUnitCmd("GestureRT", "loadPipeline", GestureRTLoadPipeline);

    unit->inputVector = new(RTAlloc(unit->mWorld, sizeof(GRT::VectorFloat) * inputVectorSize)) GRT::VectorFloat(inputVectorSize);
    unit->pipeline = new(RTAlloc(unit->mWorld, sizeof(GRT::GestureRecognitionPipeline))) GRT::GestureRecognitionPipeline();


    unit->output = 0.0f;

    if ((unit->inputVector == NULL) || (unit->pipeline == NULL)) {
        SETCALC(ft->fClearUnitOutputs);
        ClearUnitOutputs(unit, 1);

        if(unit->mWorld->mVerbosity > -2) {
            Print("Failed to allocate memory for GestureRT ugen.\n");
        }
        return;
    }


    // Set calc function to put out dummy zeroes until pipeline is trained.
    SETCALC(GestureRT_next_noTrain);
    // calculate one sample of output.
    // if you don't do this, downstream ugens might access garbage memory in their Ctor functions.
    GestureRT_next_noTrain(unit, 1);
}

// this must be named PluginName_Dtor.
void GestureRT_Dtor(GestureRT* unit) {
    // Free the memory.
    unit->currentTask = TASK_STOP;
    pthread_join(unit->grtThread, NULL);
    RTFree(unit->mWorld, unit->inputVector);
    RTFree(unit->mWorld, unit->pipeline);
    //RTFree(unit->mWorld, unit->trainingData);
}

// the calculation function can have any name, but this is conventional. the first argument must be "unit."
// this function is called every control period (64 samples is typical)
// Don't change the names of the arguments, or the helper macros won't work.
void GestureRT_next_noTrain(GestureRT* unit, int inNumSamples) {
    OUT(0)[0] = unit->output;
}

void GestureRT_next(GestureRT* unit, int inNumSamples) {

    int inputCount = IN0(0);
    bool changed = false;
    float tmp;
    
    //for (int i = 0; i < (int)inputCount; i++) {
    for (int i = 0; i < inputCount; i++) {

        tmp = IN0(i+1);

        if (tmp != unit->inputVector[0][i]) {
            unit->inputVector[0][i] = tmp; 
            unit->currentTask = unit->grtTask;
            break;
        }
    }

    OUT(0)[0] = unit->output;

}

//Load dataset from file
void GestureRTLoadDataset(Unit *gesture, struct sc_msg_iter *args) {
    //std::cout << "GestureRT Version: " << GRTBase::getGRTVersion() << std::endl;
    //TODO: Load data with u_cmd
    GestureRT * unit = (GestureRT*) gesture; 
    const char * path = args->gets();
    GRT::RegressionData trainingData;
    //FIXME not realtime safe
    if ( trainingData.loadDatasetFromFile(path) ){
        if ( unit->pipeline->train(trainingData) ) {
            if(unit->mWorld->mVerbosity > -1) {
                Print("Pipeline trained\n");
            }
        }
    } else {
        if(unit->mWorld->mVerbosity > -2) {
            Print("WARNING: Failed to load training data from file");
        }
    }

}

//Load dataset from file
void GestureRTLoadPipeline(Unit *gesture, struct sc_msg_iter *args) {
    //std::cout << "GestureRT Version: " << GRTBase::getGRTVersion() << std::endl;
    GestureRT * unit = (GestureRT*) gesture; 
    const char * path = args->gets();
    //FIXME: not realtime safe.
    if ( unit->pipeline->load(path) ){
        if (unit->pipeline->getIsPipelineInRegressionMode()) {

            unit->grtTask = TASK_REGRESSION;
        } else if (unit->pipeline->getIsPipelineInClassificationMode()) {
            unit->grtTask = TASK_CLASSIFICATION;
        }

        SETCALC(GestureRT_next);
    } else {
        if(unit->mWorld->mVerbosity > -2) {
            Print("WARNING: Failed to load pipeline from file");
        }
    }

}





// the entry point is called by the host when the plug-in is loaded
PluginLoad(GestureRTUGens) {
    // InterfaceTable *inTable implicitly given as argument to the load function
    ft = inTable; // store pointer to InterfaceTable


    // DefineSimpleUnit is one of four macros defining different kinds of ugens.
    // In later examples involving memory allocation, we'll see DefineDtorUnit.
    // You can disable aliasing by using DefineSimpleCantAliasUnit and DefineDtorCantAliasUnit.
    DefineDtorUnit(GestureRT);
}
