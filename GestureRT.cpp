#include "SC_PlugIn.h"
#include "grt/grt.h"

// InterfaceTable contains pointers to functions in the host (server).
static InterfaceTable *ft;

// declare struct to hold unit generator state
struct GestureRT : public Unit {
    // in later examples, we will declare state variables here.
    int inputCount;
    //GRT::RegressionData* trainingData;     
    GRT::LogisticRegression* regression;
    GRT::GestureRecognitionPipeline* pipeline;
    //TODO: set size of vectors
    GRT::VectorFloat* inputVector; 
    GRT::VectorFloat* outputVector;
};

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
    // in later examples, we will initialize state variables here.
    //
    int inputVectorSize = 3;
    int outputVectorSize = 1;


    DefineUnitCmd("GestureRT", "loadDataset", GestureRTLoadDataset);
    DefineUnitCmd("GestureRT", "loadPipeline", GestureRTLoadPipeline);

    unit->inputVector = new(RTAlloc(unit->mWorld, sizeof(GRT::VectorFloat) * inputVectorSize)) GRT::VectorFloat(inputVectorSize);
    unit->outputVector = new(RTAlloc(unit->mWorld, sizeof(GRT::VectorFloat) * outputVectorSize)) GRT::VectorFloat(outputVectorSize);


    if ((unit->inputVector == NULL) || (unit->outputVector == NULL)) {
        SETCALC(ft->fClearUnitOutputs);
        ClearUnitOutputs(unit, 1);

        if(unit->mWorld->mVerbosity > -2) {
            Print("Failed to allocate memory for GestureRT ugen.\n");
        }
        return;
    }

    //Create a new logistic regression module
    //TODO: Load pipeline data from grt file
    ///
    const bool scaleData = true;
    const GRT::Float learningRate = 1.2;
    const GRT::Float minChange = 0.01;
    const GRT::UINT batchSize = 20;
    const GRT::UINT maxNumEpochs = 100;
    unit->regression = new(RTAlloc(unit->mWorld, sizeof(GRT::LogisticRegression))) GRT::LogisticRegression(scaleData,learningRate,minChange,batchSize,maxNumEpochs);
    unit->pipeline = new(RTAlloc(unit->mWorld, sizeof(GRT::GestureRecognitionPipeline))) GRT::GestureRecognitionPipeline();

    unit->pipeline->setRegressifier(*(unit->regression));




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
    RTFree(unit->mWorld, unit->outputVector);
    RTFree(unit->mWorld, unit->regression);
    RTFree(unit->mWorld, unit->pipeline);
    //RTFree(unit->mWorld, unit->trainingData);
}

// the calculation function can have any name, but this is conventional. the first argument must be "unit."
// this function is called every control period (64 samples is typical)
// Don't change the names of the arguments, or the helper macros won't work.
void GestureRT_next_noTrain(GestureRT* unit, int inNumSamples) {
    float* outbuf = OUT(0);
    outbuf[0] = 0.0;
}

void GestureRT_next(GestureRT* unit, int inNumSamples) {

    int inputCount = IN0(0);
    float* outbuf = OUT(0);

    
    //for (int i = 0; i < (int)inputCount; i++) {
    for (int i = 0; i < inputCount; i++) {

        unit->inputVector[0][i] = IN0(i+1);
    }

    if (unit->pipeline->getIsInitialized()) {
        if (unit->pipeline->predict(*(unit->inputVector))) {
            *(unit->outputVector) = (unit->pipeline->getRegressionData());
            outbuf[0] = unit->outputVector[0][0];
        }
    } else {
        outbuf[0] = 0.0;
    }

}

//Load dataset from file
void GestureRTLoadDataset(Unit *gesture, struct sc_msg_iter *args) {
    //std::cout << "GestureRT Version: " << GRTBase::getGRTVersion() << std::endl;
    //TODO: Load data with u_cmd
    GestureRT * unit = (GestureRT*) gesture; 
    const char * path = args->gets();
    GRT::RegressionData trainingData;
    if ( trainingData.loadDatasetFromFile(path) ){
        if ( unit->pipeline->train(trainingData) ) {
            if(unit->mWorld->mVerbosity > -1) {
                Print("Pipeline trained\n");
            }
            SETCALC(GestureRT_next);
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
    //TODO: Load data with u_cmd
    GestureRT * unit = (GestureRT*) gesture; 
    const char * path = args->gets();
    if ( unit->pipeline->load(path) ){
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
