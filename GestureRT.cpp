#include <pthread.h>
#include "SC_Lock.h"
#include "SC_PlugIn.h"
#include "SC_SyncCondition.h"
#include "GRT.h"

#include <boost/lockfree/spsc_queue.hpp>

#define INTERVAL 2.5

enum tasks {
    taskQuit = 0,
    taskIdle = 1,
    taskRegression = 2,
    taskClassification = 3,
    taskLoadPipeline = 4,
    taskLoadDataset = 5,
    taskInit = 6
};

// InterfaceTable contains pointers to functions in the host (server).
static InterfaceTable *ft;


// declare struct to hold unit generator state
struct GestureRT : public Unit {
    // in later examples, we will declare state variables here.
    int inputCount;
    int currentTask;
    int grtTask = taskRegression;
    char filePath[PATH_MAX];
    float output;
    //GRT::RegressionData* trainingData;     
    GRT::GestureRecognitionPipeline* pipeline;
    GRT::VectorFloat* inputVector; 
};

pthread_t grtThread;
std::atomic<bool> threadRunning;
SC_SyncCondition grtSync;
boost::lockfree::spsc_queue<GestureRT*, boost::lockfree::capacity<256> > grtFifo;

// older plugins wrap these function declarations in 'extern "C" { ... }'
// no need to do that these days.
static void GestureRT_next(GestureRT* unit, int inNumSamples);
static void GestureRT_next_noTrain(GestureRT* unit, int inNumSamples);
static void GestureRT_Ctor(GestureRT* unit);

void GestureRTLoadPipeline (Unit *unit, struct sc_msg_iter *args); 
void GestureRTLoadDataset (Unit *unit, struct sc_msg_iter *args); 

//Threading stuff
void *grt_update_func(void *param) {

    while (threadRunning.load()) {

        grtSync.WaitEach();

        GestureRT* unit;

        bool popSucceeded = grtFifo.pop(unit);

        if (popSucceeded) {

            switch (unit->currentTask) {

                case taskIdle: //idle
                    break;

                case taskRegression: //regression
                    if (unit->inputVector != NULL) {
                        if (unit->pipeline->predict(*(unit->inputVector))) {
                            unit->output = unit->pipeline->getRegressionData()[0];
                        }
                    }
                    break;

                case taskClassification: //classification
                    if (unit->inputVector != NULL) {
                        if (unit->pipeline->predict(*(unit->inputVector))) {
                            unit->output = unit->pipeline->getPredictedClassLabel();
                        }
                    }

                    break;

                case taskLoadPipeline: //loadPipeline

                    if ( unit->pipeline->load(unit->filePath) ){
                        if (unit->pipeline->getIsPipelineInRegressionMode()) {
                            unit->grtTask = taskRegression;
                        } else if (unit->pipeline->getIsPipelineInClassificationMode()) {
                            unit->grtTask = taskClassification;
                        }

                        SETCALC(GestureRT_next);
                    } else {
                        if(unit->mWorld->mVerbosity > -2) {
                            Print("WARNING: Failed to load pipeline from file");
                        }
                    }


                    break;

                case taskLoadDataset: //loadDataset
                    {
                        GRT::RegressionData trainingData;

                        //FIXME not realtime safe
                        if (unit->pipeline != NULL) {
                            if ( trainingData.loadDatasetFromFile(unit->filePath) ){
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

                        break;
                    }

                case taskInit:

                    break;

                case taskQuit:
                    break;

            };

            unit->currentTask = taskIdle;
            //Do stuff

        }

    }

    //We need to return something
    return NULL;
}


// the constructor function is called when a Synth containing this ugen is played.
// it MUST be named "PluginName_Ctor", and the argument must be "unit."
void GestureRT_Ctor(GestureRT* unit) {

    //Set input vector size to size of input array
    int inputVectorSize = IN0(0);

    unit->currentTask = taskIdle;

    DefineUnitCmd("GestureRT", "loadDataset", GestureRTLoadDataset);
    DefineUnitCmd("GestureRT", "loadPipeline", GestureRTLoadPipeline);

    unit->inputVector = new(RTAlloc(unit->mWorld, sizeof(GRT::VectorFloat) * inputVectorSize)) GRT::VectorFloat(inputVectorSize);
    unit->pipeline = new(RTAlloc(unit->mWorld, sizeof(GRT::GestureRecognitionPipeline))) GRT::GestureRecognitionPipeline();

    unit->output = 0.0f;

    if (unit->inputVector == NULL) {
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
    RTFree(unit->mWorld, unit->inputVector);
    unit->currentTask = taskQuit;
    if (grtFifo.push(unit)) {
        grtSync.Signal();
    };
    if (unit->pipeline != NULL) {
        RTFree(unit->mWorld, unit->pipeline);
    }
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
            changed = true;
        }
    }

    if (changed) {
        unit->currentTask = unit->grtTask;
        if (grtFifo.push(unit)) {
            grtSync.Signal();
        };
    };

    OUT(0)[0] = unit->output;

}

//Load dataset from file
void GestureRTLoadDataset(Unit *gesture, struct sc_msg_iter *args) {
    //TODO: Load data with u_cmd
    GestureRT * unit = (GestureRT*) gesture; 

	strncpy(unit->filePath, args->gets(), PATH_MAX);

    unit->currentTask = taskLoadDataset;
    if (grtFifo.push(unit)) {
        grtSync.Signal();
    };


}

//Load dataset from file
void GestureRTLoadPipeline(Unit *gesture, struct sc_msg_iter *args) {
    GestureRT * unit = (GestureRT*) gesture; 

    if (unit->pipeline != NULL) {
        strncpy(unit->filePath, args->gets(), PATH_MAX);

        unit->currentTask = taskLoadPipeline;
        if (grtFifo.push(unit)) {
            grtSync.Signal();
        };
    }
    

}





// the entry point is called by the host when the plug-in is loaded
PluginLoad(GestureRTUGens) {
    // InterfaceTable *inTable implicitly given as argument to the load function
    ft = inTable; // store pointer to InterfaceTable

    threadRunning.store(true);
    pthread_create(&grtThread, NULL, grt_update_func, NULL);


    // DefineSimpleUnit is one of four macros defining different kinds of ugens.
    // In later examples involving memory allocation, we'll see DefineDtorUnit.
    // You can disable aliasing by using DefineSimpleCantAliasUnit and DefineDtorCantAliasUnit.
    DefineDtorUnit(GestureRT);
}

C_LINKAGE SC_API_EXPORT void unload(InterfaceTable *inTable)
{
    //FIXME stop thread loop
    threadRunning.store(false);
    //RTFree(unit->mWorld, unit->pipeline);
    //0.1.wait;
    grtSync.Signal();       
    pthread_join(grtThread, NULL);
}
