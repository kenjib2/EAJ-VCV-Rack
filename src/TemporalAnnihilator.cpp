#include "plugin.hpp"
#include "CjFilter.h"
#include "LoopBuffer.hpp"
#include "../freeverb/revmodel.hpp"

const int UI_REFRESH_RATE_FACTOR = 50;
const float MIN_TIME_PARAM = 0.020f;
const int PEAK_RELEASE_SAMPLES = 10000;
const int MAX_BUFFER_SECONDS = 2;
const int MAX_SAMPLE_RATE = 768000;
const int MAX_BUFFER_SIZE = MAX_SAMPLE_RATE * MAX_BUFFER_SECONDS;


// To figure out:
// Latch on just a dry signal where sensitivity starts the loop latch rather than the delay? Maybe that's a different effect?
// Change sensitivity to some kind of peak analysis or compressor like algorithm
class BitCrusher {
	private:
		float lastDownsample;
		float downsampleAccrued;

	public:
		BitCrusher() {
			lastDownsample = 0;
			downsampleAccrued = 0;
		}

		float process(float voltage, float bits, float downsample) {
			float crushedVoltage = voltage;

			if (downsample > 0.00001f) {
				downsampleAccrued += 1.f;
				if (downsampleAccrued > downsample) {
					downsampleAccrued -= downsample;
					lastDownsample = voltage;
				}
				else {
					crushedVoltage = lastDownsample;
				}
			}

			if (bits > 0.00001f) {
				crushedVoltage = ((crushedVoltage + 5.f) * 6553.5);
				float factor = powf(2.f, 16.f - bits);
				crushedVoltage = crushedVoltage - (fmod(crushedVoltage, factor));
				crushedVoltage = crushedVoltage / 6553.5 - 5.f;
			}

			return crushedVoltage;
		}
};

// From https://www.youtube.com/watch?v=iNCR5flSuDs
class Saturator {
	public:
		Saturator() {
		}

		float process(float voltage, float drive) {
			float saturatedVoltage = voltage * drive;

			saturatedVoltage = (2.f / M_PI) * atan(saturatedVoltage);

			return saturatedVoltage;
		}
};

struct TemporalAnnihilator : Module {
	private:
		bool sensitivityTriggered;
		int timeLatchCount;
		AudioBuffer* loopBuffer;
		AudioBuffer* glitchBuffer;
//		AudioBuffer* stretchBuffer;
		int timeOffset;
		int refreshCounter;
		BitCrusher* bitCrusher;
		CjFilter* filter;
		revmodel* revModel;
		Saturator* saturator;
		bool forceDucking = false;
		int peakReleaseCount = 0;

	public:
		enum ParamId {
			BUFFER_PARAM,
			DIRECTION_PARAM,
			DRY_PARAM,
			WET_PARAM,
			SENSITIVITY_PARAM,
			LOOP_LATCH_PARAM,
			TIME_PARAM,
			FEEDBACK_PARAM,
			TIME_DRIFT_PARAM,
			TIME_LATCH_PARAM,
			SMOOTH_PARAM,
			SMEAR_PARAM,
			DRIVE_PARAM,
			CRUSH_PARAM,
			PARAMS_LEN
		};
		enum InputId {
			INPUT_INPUT,
			INPUTS_LEN
		};
		enum OutputId {
			OUTPUT_OUTPUT,
			OUTPUTS_LEN
		};
		enum LightId {
			LOOP_LIGHT,
			TRIGGER_LIGHT,
			LLATCH_LIGHT,
			TLATCH_LIGHT,
			LIGHTS_LEN
		};

		TemporalAnnihilator() {
			config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);

			configParam(BUFFER_PARAM, 0.f, 1.f, 1.f, "");
			configParam(DIRECTION_PARAM, 0.f, 1.f, 1.f, "");

			configParam(DRY_PARAM, 0.f, 1.f, 1.f, "");
			configParam(WET_PARAM, 0.f, 1.f, 0.5f, "");
			configParam(SENSITIVITY_PARAM, 0.f, 5.f, 5.f, "");
			configParam(LOOP_LATCH_PARAM, 0.f, 1.f, 0.f, "");

			configParam(TIME_PARAM, MIN_TIME_PARAM, 1.f, 0.5f, "");
			configParam(FEEDBACK_PARAM, 0.f, 1.f, 0.f, "");
			configParam(TIME_DRIFT_PARAM, 0.f, 1.f, 0.f, "");
			configParam(TIME_LATCH_PARAM, 0.f, 1.f, 0.f, "");

			configParam(SMOOTH_PARAM, 0.f, 0.989f, 0.f, "");
			configParam(SMEAR_PARAM, 0.f, 1.f, 0.f, "");
			configParam(DRIVE_PARAM, 0.f, 1.f, 0.f, "");
			configParam(CRUSH_PARAM, 0.f, 1.f, 0.f, "");

			configInput(INPUT_INPUT, "");
			configOutput(OUTPUT_OUTPUT, "");
//			stretchBuffer = new StretchBuffer(MAX_BUFFER_SIZE);
			glitchBuffer = new GlitchBuffer(MAX_BUFFER_SIZE);
//			loopBuffer = stretchBuffer;
			loopBuffer = glitchBuffer;
			sensitivityTriggered = false;
			timeOffset = 0;
			timeLatchCount = 0;
			refreshCounter = 0;
			peakReleaseCount = 0;

			bitCrusher = new BitCrusher();
			filter = new CjFilter();
			revModel = new revmodel();
			float gSampleRate = APP->engine->getSampleRate();
			revModel->init(gSampleRate);
			revModel->setdamp(0.50);
			revModel->setwidth(0.0);
			revModel->setroomsize(0.20);
			saturator = new Saturator();

			random::init();
		}

		~TemporalAnnihilator() {
//			delete stretchBuffer;
			delete glitchBuffer;
			delete bitCrusher;
			delete filter;
			delete revModel;
			delete saturator;
		}

		void setLights(int loopSize, float timeDrift) {
			lights[LOOP_LIGHT].setBrightness(1.f - 1.f * loopBuffer->loopPosition(loopSize));

			if (sensitivityTriggered) {
				lights[TRIGGER_LIGHT].setBrightness(1.f);
			}
			else {
				lights[TRIGGER_LIGHT].setBrightness(0.f);
			}

			if (loopBuffer->isLatched()) {
				lights[LLATCH_LIGHT].setBrightness(1.f);
			}
			else {
				lights[LLATCH_LIGHT].setBrightness(0.f);
			}

			if (timeLatchCount > 0 && timeDrift > 0.0001f) {
				lights[TLATCH_LIGHT].setBrightness(1.f);
			}
			else {
				lights[TLATCH_LIGHT].setBrightness(0.f);
			}
		}

		float applyLoopEffects(float voltage) {
			float paramCrush = params[CRUSH_PARAM].getValue();
			float paramCrushLog = paramCrush * paramCrush * 4; // We want a logorithmic response rate
			float paramSmooth = params[SMOOTH_PARAM].getValue();
			float paramSmear = params[SMEAR_PARAM].getValue();
			float paramDrive = params[DRIVE_PARAM].getValue();

			float effectVoltage = voltage;

			if (paramCrush > 0.001f) {
				effectVoltage = bitCrusher->process(voltage, clamp(15.f - paramCrushLog, 0.f, 15.f), paramCrushLog * 16);
			}

			if (paramDrive > 0.001f) {
				effectVoltage = saturator->process(effectVoltage, 1.f + paramDrive * 3);
			}

			if (paramSmooth > 0.001f) {
				effectVoltage = filter->doFilter(effectVoltage, 1.f - paramSmooth, 0.f);
			}

			if (paramSmear > 0.001f) {
				float outL = 0.f;
				float outR = 0.f;
				revModel->process(effectVoltage, outL, outR);
				effectVoltage = outL * (paramSmear * 0.6f) + effectVoltage * (1.f - (paramSmear * 0.6f)); // We scale down smear because too high generates a lot of feedback
			}
			
			return effectVoltage;
		}

		void onSampleRateChange() override {
			float sampleRate = APP->engine->getSampleRate();

			revModel->init(sampleRate);

			revModel->setdamp(0.20);
			revModel->setroomsize(0.25);

		};

		void process(const ProcessArgs& args) override {
			// --------------------------- Get Parameters ---------------------------
			float paramBuffer = params[BUFFER_PARAM].getValue();
			float paramDirection = params[DIRECTION_PARAM].getValue();

			float paramDry = params[DRY_PARAM].getValue();
			float paramWet = params[WET_PARAM].getValue();
			float paramSensitivity = params[SENSITIVITY_PARAM].getValue();
			float paramLoopLatch = params[LOOP_LATCH_PARAM].getValue();
			float paramLoopLatchLog = paramLoopLatch * paramLoopLatch; // We want a logorithmic response rate

			float paramTime = params[TIME_PARAM].getValue();
			float paramTimeInSeconds = MAX_BUFFER_SECONDS * paramTime;
			int paramTimeInSamples = int(paramTimeInSeconds * args.sampleRate);
			float paramFeedback = params[FEEDBACK_PARAM].getValue();
			float paramTimeDrift = params[TIME_DRIFT_PARAM].getValue();
			float paramTimeDriftLog = paramTimeDrift * paramTimeDrift; // We want a logorithmic response rate
			float paramTimeLatch = params[TIME_LATCH_PARAM].getValue();
			float paramTimeLatchLog = paramTimeLatch * paramTimeLatch; // We want a logorithmic response rate

			static bool sensitivityShuttingDown = false;

			// Selecting the buffer
			if (paramBuffer < 0.000001f) {
//				loopBuffer = stretchBuffer;
			}
			else {
				loopBuffer = glitchBuffer;
			}
			int loopSize = clamp(paramTimeInSamples + timeOffset, int(MIN_TIME_PARAM * args.sampleRate), MAX_BUFFER_SIZE); // We have to calculate loopsize here to include drift

			// --------------------------- Actions Starting Each Loop ---------------------------
			if (loopBuffer->samplesRemaining(loopSize) == FADE_SAMPLES && peakReleaseCount > PEAK_RELEASE_SAMPLES && sensitivityTriggered) {
				// Check if sensitivity isn't triggered for the next loop FADE_SAMPLES before we start the loop
				// so we can fade out the 
				loopBuffer->startInputFadeOut();
				sensitivityShuttingDown = true;
			}
			else if (loopBuffer->atLoopStart()) {
				// Reset sensitivity triggering
				if (sensitivityShuttingDown) {
					sensitivityShuttingDown = false;
					sensitivityTriggered = false;
DEBUG("Turning off sensitivity");
				}

				// Managing latches this cycle -- don't latch if we aren't triggered
				if (!loopBuffer->isLatched() && sensitivityTriggered) {
					float rand = random::uniform();
					if (rand > (1.f - paramLoopLatchLog)) {
						loopBuffer->latch(13 - 8 * rand);
					}
				}

				if (timeLatchCount == 0) {
					float rand = random::uniform();
					if (rand > (1.f - paramTimeLatchLog)) {
						timeLatchCount = 13 - 8 * rand;
					}
				}
				if (timeLatchCount > 0) {
					timeLatchCount--;
				}

				// Setting the offsets for drift
				// Don't drift when we are loop latched or time latched
				if (loopBuffer->isLatched() && timeLatchCount == 0) {
					float offsetWeighting = random::uniform() * paramTimeDriftLog - paramTimeDriftLog / 2;
					if (offsetWeighting < 0) {
						timeOffset = int((paramTimeInSamples - MIN_TIME_PARAM * args.sampleRate) * offsetWeighting);
					}
					else {
						timeOffset = int((MAX_BUFFER_SIZE - paramTimeInSamples) * offsetWeighting);
						timeOffset = int((paramTimeInSamples * 4) * offsetWeighting);
					}
				}
			}

			// --------------------------- Check for loop sensitivity trigger ---------------------------
			float inputVoltage = inputs[INPUT_INPUT].getVoltage();
			if (abs(inputVoltage) > 5.f - paramSensitivity) {
				if (sensitivityTriggered == false) {
					loopBuffer->startInputFadeIn();
				}
				peakReleaseCount = 0;
				sensitivityTriggered = true;  // Sensitivity will turn off latch when the threshold is exceeded
			}
			else {
				peakReleaseCount++;
			}

			// --------------------------- Calculate Output Voltage ---------------------------
			float outputVoltage = loopBuffer->calculateReadVoltage(args.sampleRate, loopSize, inputVoltage, paramDirection < 0.000001f, paramDry, paramWet);
			outputs[OUTPUT_OUTPUT].setVoltage(outputVoltage);
if (glitchBuffer->samplesRead(loopSize) < FADE_SAMPLES + 10) { DEBUG("samples read %d loopSize %d read voltage: %f sensitivitytriggered: %d", glitchBuffer->samplesRead(loopSize), loopSize, outputVoltage, sensitivityTriggered); }
if (loopSize - glitchBuffer->samplesRead(loopSize) < FADE_SAMPLES + 10) { DEBUG("samples read %d loopSize %d read voltage: %f sensitivitytriggered: %d", glitchBuffer->samplesRead(loopSize), loopSize, outputVoltage, sensitivityTriggered); }

			// --------------------------- Write out to Buffer ---------------------------
		if (!sensitivityTriggered) {
			inputVoltage = 0.f;
		}
		// Add new input and buffer decay
//DEBUG("write enabled1 Ptr %d voltage %f", loopBuffer->samplesRead(loopSize), inputVoltage);
		float glitchInputVoltage = glitchBuffer->calculateWriteVoltage(args.sampleRate, loopSize, paramDirection < 0.000001f, paramFeedback, inputVoltage);
//		float stretchInputVoltage = stretchBuffer->calculateWriteVoltage(args.sampleRate, loopSize, paramDirection < 0.000001f, paramFeedback, inputVoltage);
//DEBUG("write enabled2 Ptr %d voltage %f", loopBuffer->samplesRead(loopSize), inputVoltage);
		float newGlitchBufferVoltage = applyLoopEffects(glitchInputVoltage);
		//float newStretchBufferVoltage = applyLoopEffects(stretchInputVoltage);
//DEBUG("write enabled3 Ptr %d voltage %f", loopBuffer->samplesRead(loopSize), newGlitchBufferVoltage);
		glitchBuffer->writeNextVoltage(args.sampleRate, loopSize, newGlitchBufferVoltage);
//				stretchBuffer->writeNextVoltage(args.sampleRate, loopSize, newStretchBufferVoltage);

			// --------------------------- Advance the Buffer ---------------------------
			glitchBuffer->next(loopSize);
//			stretchBuffer->next(loopSize);

			// --------------------------- UI Refresh ---------------------------
			// Don't refresh the UI with every cycle. It is a waste of CPU
			if (++refreshCounter >= UI_REFRESH_RATE_FACTOR) {
				refreshCounter = 0;
				setLights(loopSize, paramTimeDrift);
			}
		}
};



struct TemporalAnnihilatorWidget : ModuleWidget {
	TemporalAnnihilatorWidget(TemporalAnnihilator* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/TemporalAnnihilator.svg")));

		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		addParam(createParamCentered<CKSS>(mm2px(Vec(18.944, 42.359)), module, TemporalAnnihilator::BUFFER_PARAM));
		addParam(createParamCentered<CKSS>(mm2px(Vec(41.169, 42.359)), module, TemporalAnnihilator::DIRECTION_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(10.478, 58.234)), module, TemporalAnnihilator::DRY_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(23.707, 58.234)), module, TemporalAnnihilator::WET_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(36.936, 58.234)), module, TemporalAnnihilator::SENSITIVITY_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(50.165, 58.234)), module, TemporalAnnihilator::LOOP_LATCH_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(10.478, 75.697)), module, TemporalAnnihilator::TIME_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(23.707, 75.697)), module, TemporalAnnihilator::FEEDBACK_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(36.936, 75.697)), module, TemporalAnnihilator::TIME_DRIFT_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(50.165, 75.697)), module, TemporalAnnihilator::TIME_LATCH_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(10.478, 93.159)), module, TemporalAnnihilator::SMOOTH_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(23.707, 93.159)), module, TemporalAnnihilator::SMEAR_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(36.936, 93.159)), module, TemporalAnnihilator::DRIVE_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(50.165, 93.159)), module, TemporalAnnihilator::CRUSH_PARAM));

		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(39.582, 111.888)), module, TemporalAnnihilator::INPUT_INPUT));

		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(51.753, 111.888)), module, TemporalAnnihilator::OUTPUT_OUTPUT));

		addChild(createLightCentered<MediumLight<BlueLight>>(mm2px(Vec(9.948, 21.192)), module, TemporalAnnihilator::LOOP_LIGHT));
		addChild(createLightCentered<MediumLight<GreenLight>>(mm2px(Vec(23.178, 21.192)), module, TemporalAnnihilator::TRIGGER_LIGHT));
		addChild(createLightCentered<MediumLight<RedLight>>(mm2px(Vec(36.407, 21.192)), module, TemporalAnnihilator::LLATCH_LIGHT));
		addChild(createLightCentered<MediumLight<YellowLight>>(mm2px(Vec(49.636, 21.192)), module, TemporalAnnihilator::TLATCH_LIGHT));
	}
};

Model* modelTemporalAnnihilator = createModel<TemporalAnnihilator, TemporalAnnihilatorWidget>("TemporalAnnihilator");