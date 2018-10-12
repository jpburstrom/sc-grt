GestureRT : UGen {


    *kr { |inputs|
        ^this.multiNew('control', inputs.size.max(1), *inputs);
    }

    *loadPipeline { |path, synth, server, synthdef|
        server = server ?? { Server.default };
		(synthdef ?? { SynthDescLib.global[synth.defName.asSymbol].def }).children.do {|val,i|
            if(val.class == this) {
                server.sendMsg(\u_cmd, synth.nodeID, i, "loadPipeline", path)
            };
        }
    }

    *loadDataset { |path, synth, server, synthdef|
        server = server ?? { Server.default };
        (synthdef ?? { SynthDescLib.global[synth.defName.asSymbol].def }).children.do {|val,i|
            if(val.class == this) {
                server.sendMsg(\u_cmd, synth.nodeID, i, "loadDataset", path)
            };
        }
    }

    checkInputs {
        [0, 1].do { |i|
            (inputs[i].rate != 'control').if {
                ^"input % is not control rate".format(i).throw;
            };
        };
        ^this.checkValidInputs;
    }
}