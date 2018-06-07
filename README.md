# GestureRt

A SuperCollider UGen which uses predefined pipelines and datasets, which can be generated in the GRT UI application. See http://www.nickgillian.com/wiki/pmwiki.php/GRT/GestureRecognitionToolkit for more info about GRT.


The workflow:

1. Get sensor data into the GRT UI application (downloadable from https://github.com/nickgillian/grt/releases, current version 0.2.4)
2. Set up and train the model, save as pipeline
3. Make a SynthDef including this UGen, and add it to the global SynthDescLib
4. Play synth
5. Use the loadPipeline class method to load the pipeline file saved in (2)

```SuperCollider
(
SynthDef(\gestureTest, {
        var in = { LFNoise2.kr(1).range(0,1) } ! 3;
        var g = GestureRT.kr(in);
        g.poll;

        //Assuming output range is 0-1, change frequency of oscillator
        Out.ar(0, SinOsc.ar(g.linexp(0, 1, 220, 440)) * 0.1.dup);
}).add;

~synth = Synth(\gestureTest); //Outputs zeroes
)

//Load pipeline from file
GestureRT.loadPipeline("pipeline.txt", ~synth)


//Stop
~synth.free;
```
