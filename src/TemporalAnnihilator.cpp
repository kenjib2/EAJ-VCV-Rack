#include "plugin.hpp"
#include "CjFilter.h"
#include "LoopBuffer.hpp"
#include "../freeverb/revmodel.hpp"

const int UI_REFRESH_RATE_FACTOR = 50;
const float MIN_TIME = 0.020f;
const int FADE_SAMPLES = 60;
const int PEAK_RELEASE_SAMPLES = 10000;
const int MAX_BUFFER_SECONDS = 2;
const int MAX_SAMPLE_RATE = 768000;
const int MAX_BUFFER_SIZE = MAX_SAMPLE_RATE * MAX_BUFFER_SECONDS;


// To figure out:
// Latch on just a dry signal where sensitivity starts the loop latch rather than the delay? Maybe that's a different effect?
// Add ducking when sensitivity triggers -- need to change sensitivity to some kind of peak analysis or compressor like algorithm
// Pop when latching or drifting in reverse both buffers

/*class AudioBuffer {
	public:
		virtual bool atLoopStart() = 0;
		virtual float loopPosition(int loopSize) = 0;
		virtual float readNextVoltage(int sampleRate, int loopSize, bool reverse, bool forceDucking) = 0;
		virtual void writeNextVoltage(int sampleRate, int loopSize, float voltage) = 0;

		virtual ~AudioBuffer() = default;
};*/

/*class StretchBuffer : public AudioBuffer {
	private:
		float* delayBuffer;
		float bufferPosition;
		int writePointer;
		int readPointer;
		bool loopStart;
		bool fadingIn;

	public:
		StretchBuffer() {
			bufferPosition = 0.f;
			readPointer = 0;
			loopStart = true;
			delayBuffer = new float[MAX_BUFFER_SIZE](); // parens initialize the buffer to all zeroes
		}

		~StretchBuffer() {
			delete[] delayBuffer;
		}

		bool atLoopStart() override {
			return loopStart;
		}

		float loopPosition(int loopSize) override {
			return bufferPosition / MAX_BUFFER_SIZE;
		}

		float readNextVoltage(int sampleRate, int loopSize, bool reverse, bool forceDucking) override {
			float returnVoltage = 0.f;
			float scaleFactor = (float)MAX_BUFFER_SIZE / (float)loopSize;
			bufferPosition += scaleFactor;
			int virtualReadPointer = (int)(bufferPosition / scaleFactor + 0.5f);

			if (int(bufferPosition + 0.5f) >= MAX_BUFFER_SIZE) {
				bufferPosition -= MAX_BUFFER_SIZE;
				loopStart = true;
			}
			else {
				loopStart = false;
			}

			readPointer = int(bufferPosition + 0.5f); // 0.5f is for rounding
			readPointer = std::min(readPointer, MAX_BUFFER_SIZE - 1); // Avoid buffer overrun

			int reverseReadPointer = MAX_BUFFER_SIZE - 1 - readPointer;
			int virtualWritePointer = (int)(writePointer / scaleFactor + 0.5f);
			int reverseVirtualReadPointer = (int)(reverseReadPointer / scaleFactor + 0.5f);
			int relativeReverseVirtualReadPointer = reverseVirtualReadPointer; // For crossfading reverse delays
			if (relativeReverseVirtualReadPointer <= FADE_SAMPLES && virtualWritePointer >= loopSize - FADE_SAMPLES) {
				relativeReverseVirtualReadPointer += loopSize;
//				DEBUG("Relative read %d / %d vs write %d for loop size %d", reverseVirtualReadPointer, relativeReverseVirtualReadPointer, virtualWritePointer, loopSize);
			}
			int relativeVirtualWritePointer = virtualWritePointer; // For crossfading reverse delays
			if (relativeVirtualWritePointer <= FADE_SAMPLES && reverseVirtualReadPointer >= loopSize - FADE_SAMPLES) {
				relativeVirtualWritePointer += loopSize;
//				DEBUG("Relative write %d / %d vs read %d for loop size %d", virtualWritePointer, relativeVirtualWritePointer, reverseVirtualReadPointer, loopSize);
			}

			if (reverse) {
				returnVoltage = delayBuffer[reverseReadPointer];
			}
			else {
				returnVoltage = delayBuffer[readPointer];
			}

			// Ducking for loop latch
			if (forceDucking && virtualReadPointer > loopSize - FADE_SAMPLES) {
				returnVoltage = returnVoltage * (loopSize - virtualReadPointer) / (float)FADE_SAMPLES;
DEBUG("FADEOUT %d", virtualReadPointer);
				fadingIn = true;
			}
			else if (fadingIn && virtualReadPointer < FADE_SAMPLES) {
				returnVoltage = returnVoltage * virtualReadPointer / (float)FADE_SAMPLES;
DEBUG("FADEIN %d", virtualReadPointer);
			} // If we are in reverse mode and the read pointer is approaching the write pointer, crossfade to reading ahead by FADE_SAMPLES * 2 samples.
			else if (reverse && relativeReverseVirtualReadPointer - virtualWritePointer < FADE_SAMPLES * 2 && relativeReverseVirtualReadPointer - virtualWritePointer >= 0) {
				int virtualPeekAheadPointer = relativeReverseVirtualReadPointer - FADE_SAMPLES * 2;
				if (virtualPeekAheadPointer < 0) {
					virtualPeekAheadPointer += loopSize;
				}
				else if (virtualPeekAheadPointer > loopSize - 1) {
					virtualPeekAheadPointer -= loopSize;
				}
				int peekAheadPointer = int(virtualPeekAheadPointer * scaleFactor + 0.5f);
				float peekAheadVoltage = delayBuffer[peekAheadPointer];
				float fadeCoefficient = .0000001f; // The case when reverseReadPointer == writePointer
				if (relativeReverseVirtualReadPointer != virtualWritePointer) {
					fadeCoefficient = (relativeReverseVirtualReadPointer - virtualWritePointer) / ((float)FADE_SAMPLES * 2);
				}
				returnVoltage = returnVoltage * fadeCoefficient + peekAheadVoltage * (1.f - fadeCoefficient);
//DEBUG("Crossfade %f start %d / %d / %d fade %f voltages %f / %f and %f / %f", returnVoltage, virtualPeekAheadPointer, relativeReverseVirtualReadPointer, virtualWritePointer, fadeCoefficient, returnVoltage, returnVoltage * fadeCoefficient, peekAheadVoltage, peekAheadVoltage * (1.f - fadeCoefficient));
			} // If we are in reverse mode we need to fade back to the original pointer.
			else if (reverse && relativeVirtualWritePointer - reverseVirtualReadPointer < FADE_SAMPLES * 2 && relativeVirtualWritePointer - reverseVirtualReadPointer > 0) {
				int virtualPeekAheadPointer = reverseVirtualReadPointer - FADE_SAMPLES * 2;
				if (virtualPeekAheadPointer < 0) {
					virtualPeekAheadPointer += loopSize;
				}
				int peekAheadPointer = int(virtualPeekAheadPointer * scaleFactor + 0.5f);
				float peekAheadVoltage = delayBuffer[peekAheadPointer];
				float fadeCoefficient = (relativeVirtualWritePointer - reverseVirtualReadPointer) / ((float)FADE_SAMPLES * 2); // We don't need to account for divide by zero in this case because writePointer - reverseReadPointer > 0.
				returnVoltage = returnVoltage * fadeCoefficient + peekAheadVoltage * (1.f - fadeCoefficient);
//DEBUG("Crossfade %f stop %d / %d / %d fade %f voltages %f / %f and %f / %f", returnVoltage, virtualPeekAheadPointer, reverseVirtualReadPointer, relativeVirtualWritePointer, fadeCoefficient, returnVoltage, returnVoltage * fadeCoefficient, peekAheadVoltage, peekAheadVoltage * (1.f - fadeCoefficient));
			}
			else {
				fadingIn = false;
			}

			return returnVoltage;
		}

		void writeNextVoltage(int sampleRate, int loopSize, float voltage) override {
			if ((writePointer < 0) || (writePointer >= MAX_BUFFER_SIZE)) { // Avoid buffer overrun
				writePointer = MAX_BUFFER_SIZE - 1;
			}

			int numWrites = 0;
			if (readPointer >= writePointer) {
				numWrites = readPointer - writePointer;
			} else {
				numWrites += MAX_BUFFER_SIZE - 1 - writePointer + readPointer;
			}

			for (int i = 0; i <= numWrites; i++) {
				delayBuffer[(writePointer + i) % (MAX_BUFFER_SIZE - 1)] = voltage;
			}

			writePointer = readPointer;
		}

		void latch(int numLoops) override {
		}
};*/

/*class GlitchBufferOld : public AudioBuffer {
	private:
		float* delayBuffer;
		int writePointer;
		int readPointer;
		int highWaterMarkPointer; // This is the highest buffer position that was written to in the last pass through the loop.
		bool fadingIn;

	public:
		GlitchBufferOld() {
			readPointer = 0;
			highWaterMarkPointer = 0;
			delayBuffer = new float[MAX_BUFFER_SIZE](); // parens initialize the buffer to all zeroes
		}

		~GlitchBufferOld() {
			delete[] delayBuffer;
		}

		bool atLoopStart() override {
			return (readPointer == 0);
		}

		float loopPosition(int loopSize) override {
			return float(readPointer) / loopSize;
		}

		float readNextVoltage(int sampleRate, int loopSize, bool reverse, bool forceDucking) override {
			loopSize = std::min(loopSize, MAX_BUFFER_SIZE); // Avoid buffer overruns
			readPointer++;
			readPointer = readPointer % loopSize;
			float returnVoltage = 0.f;
			int reverseReadPointer = loopSize - 1 - readPointer;
			int relativeReverseReadPointer = reverseReadPointer; // For crossfading reverse delays
			if (relativeReverseReadPointer <= FADE_SAMPLES && writePointer >= loopSize - FADE_SAMPLES) {
				relativeReverseReadPointer += loopSize;
			}
			int relativeWritePointer = writePointer; // For crossfading reverse delays
			if (relativeWritePointer <= FADE_SAMPLES && reverseReadPointer >= loopSize - FADE_SAMPLES) {
				relativeWritePointer += loopSize;
			}

			if (reverse) {
				returnVoltage = delayBuffer[reverseReadPointer];
			}
			else {
				returnVoltage = delayBuffer[readPointer];
			}

			// Past the end of where we recorded last time just return silence
			if (readPointer > highWaterMarkPointer) {
				returnVoltage = 0.f;
			} // If we will need to add silence, then fade out the end of the loop so that it doesn't pop
			else if ((forceDucking || loopSize > highWaterMarkPointer + 1) && readPointer > highWaterMarkPointer - FADE_SAMPLES) {
				returnVoltage = returnVoltage * (highWaterMarkPointer - readPointer) / (float)FADE_SAMPLES;
				fadingIn = true;
			} // If we are shortening the loop or loop latching, then fade out the end of the loop so that it doesn't pop
			else if (( forceDucking || loopSize < highWaterMarkPointer + 1) && readPointer > loopSize - FADE_SAMPLES) {
				returnVoltage = returnVoltage * (loopSize - readPointer) / (float)FADE_SAMPLES;
				fadingIn = true;
			}
			else if (fadingIn && readPointer < FADE_SAMPLES) {
				returnVoltage = returnVoltage * readPointer / (float)FADE_SAMPLES;
			} // If we are in reverse mode and the read pointer is approaching the write pointer, crossfade to reading ahead by FADE_SAMPLES * 2 samples.
			else if (reverse && relativeReverseReadPointer - writePointer < FADE_SAMPLES * 2 && relativeReverseReadPointer - writePointer >= 0) {
				int peekAheadPointer = relativeReverseReadPointer - FADE_SAMPLES * 2;
				if (peekAheadPointer < 0) {
					peekAheadPointer += loopSize;
				}
				else if (peekAheadPointer > loopSize - 1) {
					peekAheadPointer -= loopSize;
				}
				float peekAheadVoltage = delayBuffer[peekAheadPointer];
				float fadeCoefficient = .0000001f; // The case when reverseReadPointer == writePointer
				if (relativeReverseReadPointer != writePointer) {
					fadeCoefficient = (relativeReverseReadPointer - writePointer) / ((float)FADE_SAMPLES * 2);
				}
				returnVoltage = returnVoltage * fadeCoefficient + peekAheadVoltage * (1.f - fadeCoefficient);
			} // If we are in reverse mode we need to fade back to the original pointer.
			else if (reverse && relativeWritePointer - reverseReadPointer < FADE_SAMPLES * 2 && relativeWritePointer - reverseReadPointer > 0) {
				int peekAheadPointer = reverseReadPointer - FADE_SAMPLES * 2;
				if (peekAheadPointer < 0) {
					peekAheadPointer += loopSize;
				}
				float peekAheadVoltage = delayBuffer[peekAheadPointer];
				float fadeCoefficient = (relativeWritePointer - reverseReadPointer) / ((float)FADE_SAMPLES * 2); // We don't need to account for divide by zero in this case because writePointer - reverseReadPointer > 0.
				returnVoltage = returnVoltage * fadeCoefficient + peekAheadVoltage * (1.f - fadeCoefficient);
			}
			else {
				fadingIn = false;
			}

			return returnVoltage;
		}

		void writeNextVoltage(int sampleRate, int loopSize, float voltage) override {
			writePointer = readPointer;

			delayBuffer[writePointer] = voltage;

			if (writePointer >= loopSize - 1) {
				highWaterMarkPointer = writePointer;
			}
//DEBUG("WRITE PTR %d / %d / %d: %f", writePointer, highWaterMarkPointer, loopSize, voltage);
		}

		void latch(int numLoops) override {
		}
};*/

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
		bool triggered;
		bool writeEnabled;
		int loopLatchCount;
		int timeLatchCount;
		AudioBuffer* loopBuffer;
		AudioBuffer* glitchBuffer;
		AudioBuffer* stretchBuffer;
		int timeOffset;
		int refreshCounter;
		BitCrusher* bitCrusher;
		CjFilter* filter;
		revmodel* revModel;
		Saturator* saturator;
		bool forceDucking = false;
		int peakReleaseCount = 0;
		int fadeInCount = 0;
		int fadeOutCount = 0;

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

			configParam(TIME_PARAM, MIN_TIME, 1.f, 0.5f, "");
			configParam(FEEDBACK_PARAM, 0.f, 1.f, 0.f, "");
			configParam(TIME_DRIFT_PARAM, 0.f, 1.f, 0.f, "");
			configParam(TIME_LATCH_PARAM, 0.f, 1.f, 0.f, "");

			configParam(SMOOTH_PARAM, 0.011f, 1.f, 0.f, "");
			configParam(SMEAR_PARAM, 0.f, 1.f, 0.f, "");
			configParam(DRIVE_PARAM, 0.f, 1.f, 0.f, "");
			configParam(CRUSH_PARAM, 0.f, 1.f, 0.f, "");

			configInput(INPUT_INPUT, "");
			configOutput(OUTPUT_OUTPUT, "");
			stretchBuffer = new StretchBuffer(MAX_BUFFER_SIZE);
			glitchBuffer = new GlitchBuffer(MAX_BUFFER_SIZE);
			loopBuffer = glitchBuffer;
			triggered = false;
			writeEnabled = true;
			timeOffset = 0;
			loopLatchCount = 0;
			timeLatchCount = 0;
			refreshCounter = 0;
			peakReleaseCount = 0;
			fadeInCount = 0;
			fadeOutCount = 0;

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
			delete stretchBuffer;
			delete glitchBuffer;
			delete bitCrusher;
			delete filter;
			delete revModel;
			delete saturator;
		}

		void setLights(int loopSize, float timeDrift) {
			lights[LOOP_LIGHT].setBrightness(1.f - 1.f * loopBuffer->loopPosition(loopSize));

			if (triggered) {
				lights[TRIGGER_LIGHT].setBrightness(1.f);
			}
			else {
				lights[TRIGGER_LIGHT].setBrightness(0.f);
			}

			if (loopLatchCount > 0) {
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

			// Selecting the buffer
			if (paramBuffer < 0.000001f) {
				loopBuffer = stretchBuffer;
			}
			else {
				loopBuffer = glitchBuffer;
			}

			// --------------------------- Actions Starting Each Loop ---------------------------
			if (loopBuffer->atLoopStart()) {
				// Reset sensitivity triggering
				if (peakReleaseCount > PEAK_RELEASE_SAMPLES) {
					triggered = false;
					fadeInCount = 0;
				}
				if (triggered == false) {
					writeEnabled = false;
					fadeOutCount = FADE_SAMPLES;
				}

				// Managing latches this cycle
				if (loopLatchCount == 0) {
					forceDucking = false;
					float rand = random::uniform();
					if (rand > (1.f - paramLoopLatchLog)) {
						loopLatchCount = 13 - 8 * rand;
					}
				}
				if (loopLatchCount > 0) {
					forceDucking = true;
					loopLatchCount--;
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
				if (loopLatchCount == 0 && timeLatchCount == 0) {
					float offsetWeighting = random::uniform() * paramTimeDriftLog - paramTimeDriftLog / 2;
					if (offsetWeighting < 0) {
						timeOffset = int((paramTimeInSamples - MIN_TIME * args.sampleRate) * offsetWeighting);
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
				peakReleaseCount = 0;
				triggered = true;
				writeEnabled = true;  // Sensitivity will turn off latch when the threshold is exceeded
			}
			else {
				peakReleaseCount++;
			}

			// --------------------------- Calculate Output Voltage ---------------------------
			int loopSize = clamp(paramTimeInSamples + timeOffset, int(MIN_TIME * args.sampleRate), MAX_BUFFER_SIZE); // We have to calculate loopsize here to include drift
			float glitchVoltage = glitchBuffer->readNextVoltage(args.sampleRate, loopSize, paramDirection < 0.000001f, forceDucking);
			float stretchVoltage = stretchBuffer->readNextVoltage(args.sampleRate, loopSize, paramDirection < 0.000001f, forceDucking);
			float bufferVoltage = 0.f;
			if (paramBuffer < 0.000001f) {
				bufferVoltage = stretchVoltage;
			}
			else {
				bufferVoltage = glitchVoltage;
			}
			float outputVoltage = inputVoltage * paramDry + bufferVoltage * paramWet;
			outputs[OUTPUT_OUTPUT].setVoltage(outputVoltage);

			// --------------------------- Write out to Buffer ---------------------------
			if (loopLatchCount > 0) {
				// If loop has latched, then repeat the buffer exactly (do nothing)
			}
			else if (writeEnabled) {
				// Digital Delay Behavior
				float calculatedInputVoltage = inputVoltage;
				if (fadeInCount < FADE_SAMPLES) {
					calculatedInputVoltage = calculatedInputVoltage * fadeInCount / FADE_SAMPLES;
					fadeInCount++;
					DEBUG("FADE IN: %f", calculatedInputVoltage);
				}
				float newBufferVoltage = applyLoopEffects(calculatedInputVoltage + paramFeedback * bufferVoltage);
				glitchBuffer->writeNextVoltage(args.sampleRate, loopSize, newBufferVoltage);
				stretchBuffer->writeNextVoltage(args.sampleRate, loopSize, newBufferVoltage);
			}
			else {
				float newBufferVoltage = paramFeedback * bufferVoltage;
				if (fadeOutCount > 0) {
					newBufferVoltage += applyLoopEffects(inputVoltage * fadeOutCount / FADE_SAMPLES);
					fadeOutCount--;
					DEBUG("FADE OUT: %f", newBufferVoltage);
				}
				// Buffer decay with no new input
				glitchBuffer->writeNextVoltage(args.sampleRate, loopSize, newBufferVoltage);
				stretchBuffer->writeNextVoltage(args.sampleRate, loopSize, newBufferVoltage);
			}

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