#include "plugin.hpp"


struct MyModule : Module {
	enum WaveType {
		SINE,
		SAWTOOTH,
		SQUARE
	};
	enum ParamId {
		PITCH_PARAM,
		PARAMS_LEN
	};
	enum InputId {
		PITCH_INPUT,
		INPUTS_LEN
	};
	enum OutputId {
		WAVE_OUTPUT,
		OUTPUTS_LEN
	};
	enum LightId {
		BLINK_LIGHT,
		LIGHTS_LEN
	};

	float phase = 0.f;
	float blinkPhase = 0.f;
	WaveType waveType = SQUARE;

	MyModule() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configParam(PITCH_PARAM, 0.f, 1.f, 0.f, "");
		configInput(PITCH_INPUT, "");
		configOutput(WAVE_OUTPUT, "");
	}

	void process(const ProcessArgs& args) override {
		// Compute the frequency from the pitch parameter and input
		float pitch = params[PITCH_PARAM].getValue();
		pitch += inputs[PITCH_INPUT].getVoltage();
		pitch = clamp(pitch, -4.f, 4.f);
		// The default pitch is C4 = 261.6256f
		float freq = dsp::FREQ_C4 * std::pow(2.f, pitch);

		// Accumulate the phase
		phase += freq * args.sampleTime;
		if (phase >= 0.5f)
			phase -= 1.f;

		// Compute the sine output
		float voltage = 0.f;
		switch (waveType) {
			case SINE:
				voltage = std::sin(2.f * M_PI * phase);
				break;
			case SAWTOOTH:
				voltage = phase;
				break;
			case SQUARE:
				if (phase < 0.f) {
					voltage = -1.f;
				}
				else {
					voltage = 1.f;
				}
				break;
			default:
				voltage = 0.f;
		}
		// Audio signals are typically +/-5V
		// https://vcvrack.com/manual/VoltageStandards
		outputs[WAVE_OUTPUT].setVoltage(5.f * voltage);

		// Blink light at 1Hz
		blinkPhase += args.sampleTime;
		if (blinkPhase >= 1.f)
			blinkPhase -= 1.f;
		lights[BLINK_LIGHT].setBrightness(blinkPhase < 0.5f ? 1.f : 0.f);
	}
};


struct MyModuleWidget : ModuleWidget {
	MyModuleWidget(MyModule* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/MyModule.svg")));

		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(15.24, 46.063)), module, MyModule::PITCH_PARAM));

		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(15.24, 77.478)), module, MyModule::PITCH_INPUT));

		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(15.24, 108.713)), module, MyModule::WAVE_OUTPUT));

		addChild(createLightCentered<MediumLight<RedLight>>(mm2px(Vec(15.24, 25.81)), module, MyModule::BLINK_LIGHT));
	}
};


Model* modelMyModule = createModel<MyModule, MyModuleWidget>("MyModule");