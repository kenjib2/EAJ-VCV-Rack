#pragma once


const int FADE_SAMPLES = 60;


class AudioBuffer {
protected:
	float* loopBuffer;
	int writeIndex;
	int readIndex;
	bool latched;
	int latchLoopCounter;
	bool needCrossFadePeek;
	bool needCrossFadeReturn;
	bool needReadFadeIn;
	bool needReadFadeOut;
	bool needWriteFadeIn;
	bool needWriteFadeOut;
	bool needRewriteFadeIn;
	bool needRewriteFadeOut;
	bool needInputFadeIn;
	bool needInputFadeOut;

	virtual float readPeekVoltage(int loopSize) = 0;
	virtual int getReverseIndex(int loopSize) = 0;
	virtual float doRead(int sampleRate, int loopSize, bool reverse) = 0;
	virtual void doWrite(int sampleRate, int loopSize, float voltage) = 0;


	// 1 to fade out, -1 to fade in
	float getFadeCoefficient(int loopSize, int direction) {
		if (direction >= 0) {
			return (float)samplesRemaining(loopSize) / FADE_SAMPLES;
		}
		else {
			return (float)samplesRead(loopSize) / FADE_SAMPLES;
		}
	}


	// aCoefficient goes from 0.f to 1.f. Use a 0.f voltageOut to fade in/out.
	float crossFade(float voltageA, float aCoefficient, float voltageB) {
		return voltageA * aCoefficient + voltageB * (1.f - aCoefficient);
	}


	// Behavior for latches:
	// 1st latch round -- stops writing to loop buffer -- peek cross fades out
	// 2nd+ latch rounds -- no writing to loop buffer -- peek cross fades in, peek cross fades out
	// last latch round -- no writing to loop buffer -- peek cross fades in, peek cross fades out, rewrite fade out to buffer
	// latch + 1 round -- writing new buffer data, playing old buffer data -- peek cross fades in, write fade in new buffer
	float removePops(float voltageIn, int sampleRate, int loopSize) {
		static bool latchPlusOne = false;
		float returnVoltage = voltageIn;

		// These are all for removing pops during latches
		if (isLatched() && nearEnd(loopSize)) {
			// Cross fade to the peek at the end of every latched loop
			needCrossFadePeek = true;
		}
		if (needCrossFadePeek && samplesRemaining(loopSize) == 0) {
			// Every time we cross fade to the peek, we need to crossfade back to the normal read index
			needCrossFadeReturn = true;
		}
		if (isLatched() && latchLoopCounter == 0 && nearEnd(loopSize)) {
			// Rewrite the buffer during the last loop so it will fade out in the future.
			needRewriteFadeOut = true;
			latchPlusOne = true;
		}
		if (latchPlusOne && samplesRead(loopSize) == 1) {
			// Make sure the next buffer write fades in after the rewrite fade out.
			needWriteFadeIn = true;
			latchPlusOne = false;
		}

		if (needCrossFadePeek && samplesRemaining(loopSize) <= FADE_SAMPLES) {
			// Smear the end of each loop while latched.
			if (samplesRemaining(loopSize) == 0) {
				needCrossFadePeek = false;
			}
			float peekVoltage = readPeekVoltage(loopSize);
			returnVoltage = crossFade(returnVoltage, getFadeCoefficient(loopSize, 1), peekVoltage);
			#ifdef DEBUG_POPS
				DEBUG("Cross fade out %d / %d: %f + %f = %f using coefficient %f", readIndex, loopSize, voltageIn, peekVoltage, returnVoltage, getFadeCoefficient(loopSize, 1));
			#endif
		}

		else if (needCrossFadeReturn && samplesRead(loopSize) <= FADE_SAMPLES) {
			// Unsmear the next beginning of each loop to get the buffer back to readIndex.
			if (samplesRead(loopSize) == FADE_SAMPLES) {
				needCrossFadeReturn = false;
			}
			float peekVoltage = readPeekVoltage(loopSize);
			returnVoltage = crossFade(returnVoltage, getFadeCoefficient(loopSize, -1), peekVoltage);
			#ifdef DEBUG_POPS
				DEBUG("Cross fade back %d / %d: %f + %f = %f using coefficient %f", readIndex, loopSize, voltageIn, peekVoltage, returnVoltage, getFadeCoefficient(loopSize, -1));
			#endif
		}

		if (needReadFadeIn && samplesRead(loopSize) <= FADE_SAMPLES) {
			// Fade in the buffer audio at the beginning of the loop
			if (samplesRead(loopSize) == FADE_SAMPLES) {
				needReadFadeIn = false;
			}
			returnVoltage = crossFade(returnVoltage, getFadeCoefficient(loopSize, -1), 0.f);
			#ifdef DEBUG_POPS
				DEBUG("Read fade in %d / %d: %f + %f = %f using coefficient %f", readIndex, loopSize, voltageIn, 0.f, returnVoltage, getFadeCoefficient(loopSize, -1));
			#endif
		}

		if (needReadFadeOut && samplesRemaining(loopSize) <= FADE_SAMPLES) {
			// Fade out the buffer audio at the end of the loop
			if (samplesRemaining(loopSize) == 0) {
				needReadFadeOut = false;
			}
			returnVoltage = crossFade(returnVoltage, getFadeCoefficient(loopSize, 1), 0.f);
			#ifdef DEBUG_POPS
				DEBUG("Read fade out %d / %d: %f + %f = %f using coefficient %f", readIndex, loopSize, voltageIn, 0.f, returnVoltage, getFadeCoefficient(loopSize, 1));
			#endif
		}

		if (needRewriteFadeIn && samplesRead(loopSize) <= FADE_SAMPLES) {
			// Rewrite the beginning of the buffer to fade in. This will not take effect on the current read data until the next time
			// reading through the loop. It does not alter the current iteration. This is a destructive operation.
			if (samplesRead(loopSize) == FADE_SAMPLES) {
				needRewriteFadeIn = false;
			}
			// Fade in the buffer head if this is the last latch
			float writeVoltage = loopBuffer[readIndex];
			writeVoltage = crossFade(writeVoltage, getFadeCoefficient(loopSize, -1), 0.f);
			#ifdef DEBUG_POPS
				DEBUG("Rewrite fade in %d / %d: %f + %f = %f using coefficient %f", readIndex, loopSize, loopBuffer[readIndex], 0.f, writeVoltage, getFadeCoefficient(loopSize, -1));
			#endif
			doWrite(sampleRate, loopSize, writeVoltage);
		}

		if (needRewriteFadeOut && samplesRemaining(loopSize) <= FADE_SAMPLES) {
			// Rewrite the end of the buffer to fade out. This will not take effect on the current read data until the next time
			// reading through the loop. It does not alter the current iteration. This is a destructive operation.
			if (samplesRemaining(loopSize) == 0) {
				needRewriteFadeOut = false;
			}
			float writeVoltage = loopBuffer[readIndex];
			writeVoltage = crossFade(writeVoltage, getFadeCoefficient(loopSize, 1), 0.f);
			#ifdef DEBUG_POPS
				DEBUG("Rewrite fade out %d / %d: %f + %f = %f using coefficient %f", readIndex, loopSize, loopBuffer[readIndex], 0.f, writeVoltage, getFadeCoefficient(loopSize, 1));
			#endif
			doWrite(sampleRate, loopSize, writeVoltage);
		}

		return returnVoltage;
	}


	void restartLoop() {
		if (latchLoopCounter > 0) {
			latched = true;
			latchLoopCounter--;
		}
		else {
			if (latched == true) {
				#ifdef DEBUG_POPS
					DEBUG("TURNING OFF LATCH");
				#endif
			}
			latched = false;
		}
		#ifdef DEBUG_POPS
			DEBUG("Starting new loop. Latch %d count %d.", isLatched(), latchLoopCounter);
		#endif
	}


public:
	int bufferSize;

	virtual bool atLoopStart() = 0;
	virtual float loopPosition(int loopSize) = 0; // Relative position in loop from 0.f to 1.f
	virtual int samplesRead(int loopSize) = 0;
	virtual int samplesRemaining(int loopSize) = 0;
	virtual void next(int loopSize) = 0;


	AudioBuffer(int bufferSize)
		: writeIndex(0)
		, readIndex(0)
		, latched(false)
		, latchLoopCounter(0)
		, needCrossFadePeek(false)
		, needCrossFadeReturn(false)
		, needReadFadeIn(false)
		, needReadFadeOut(false)
		, needWriteFadeIn(false)
		, needWriteFadeOut(false)
		, needRewriteFadeIn(false)
		, needRewriteFadeOut(false)
		, needInputFadeIn(false)
		, needInputFadeOut(false)
		, bufferSize(bufferSize)
	{
		loopBuffer = new float[bufferSize](); // parens initialize the buffer to all zeroes
	}


	virtual ~AudioBuffer() {
		delete loopBuffer;
	}


	float readNextVoltage(int sampleRate, int loopSize, bool reverse) {
		float returnVoltage = doRead(sampleRate, loopSize, reverse);
		returnVoltage = removePops(returnVoltage, sampleRate, loopSize);
		return returnVoltage;
	}


	void writeNextVoltage(int sampleRate, int loopSize, float voltage) {
		if (!latched) {
			doWrite(sampleRate, loopSize, voltage);
		}
	}


	void latch(int numLoops) {
		latchLoopCounter = numLoops;
	}


	bool isLatched() {
		return latched;
	}


	bool nearEnd(int loopSize) {
		return samplesRemaining(loopSize) < FADE_SAMPLES;
	}


	void startInputFadeIn() {
		needInputFadeIn = true;
	}


	void startInputFadeOut() {
		needInputFadeOut = true;
	}


	// Volumes are between 0.f and 1.f
	float calculateReadVoltage(int sampleRate, int loopSize, float voltageIn, bool reverse, float dryVolume, float wetVolume) {
		float returnVoltage = voltageIn;

		float bufferVoltage;
		if (reverse) {
			bufferVoltage = doRead(sampleRate, loopSize, true);
		}
		else {
			bufferVoltage = doRead(sampleRate, loopSize, false);
		}

		bufferVoltage = removePops(bufferVoltage, sampleRate, loopSize);
		returnVoltage = returnVoltage * dryVolume + bufferVoltage * wetVolume;

		return returnVoltage;
	}
	

	// Feedback is between 0.0f and 1.0f
	float calculateWriteVoltage(int sampleRate, int loopSize, bool direction, float feedback, float voltageIn) {
		static int fadeInCounter = 0;
		static int fadeOutCounter = 0;
		float returnVoltage = voltageIn;

		if (needInputFadeIn) {
			fadeInCounter = FADE_SAMPLES;
			needInputFadeIn = false;
		}
		if (needInputFadeOut && samplesRemaining(loopSize) == FADE_SAMPLES - 1) {
			fadeOutCounter = FADE_SAMPLES;
			needInputFadeOut = false;
		}

		if (fadeInCounter > 0) {
			returnVoltage = crossFade(returnVoltage, (float)(FADE_SAMPLES - fadeInCounter) / FADE_SAMPLES, 0.f);
			fadeInCounter--;
			#ifdef DEBUG_POPS
				DEBUG("Input fade in %d / %d: %f + %f = %f", readIndex, loopSize, voltageIn, 0.f, returnVoltage);
			#endif
		}
		if (fadeOutCounter > 0) {
			returnVoltage = crossFade(returnVoltage, (float)fadeOutCounter / FADE_SAMPLES, 0.f);
			fadeOutCounter--;
			#ifdef DEBUG_POPS
				DEBUG("Input fade out %d / %d counter %d: %f + %f = %f", readIndex, loopSize, fadeOutCounter, voltageIn, 0.f, returnVoltage);
			#endif
		}

		float bufferVoltage = loopBuffer[readIndex];
		returnVoltage = returnVoltage + feedback * bufferVoltage;

		if (needWriteFadeIn) {
			if (samplesRead(loopSize) >= FADE_SAMPLES) {
				needWriteFadeIn = false;
			}
			returnVoltage = crossFade(returnVoltage, getFadeCoefficient(loopSize, -1), 0.f);
			#ifdef DEBUG_POPS
				DEBUG("Write fade in %d / %d: %f + %f = %f using coefficient %f", readIndex, loopSize, voltageIn, 0.f, returnVoltage, getFadeCoefficient(loopSize, -1));
			#endif
		}
		else if (needWriteFadeOut) {
			if (samplesRemaining(loopSize) == 0) {
				needWriteFadeOut = false;
			}
			returnVoltage = crossFade(returnVoltage, getFadeCoefficient(loopSize, 1), 0.f);
			#ifdef DEBUG_POPS
				DEBUG("Write fade out %d / %d: %f + %f = %f using coefficient %f", readIndex, loopSize, voltageIn, 0.f, returnVoltage, getFadeCoefficient(loopSize, -1));
			#endif
		}

		return returnVoltage;
	}


};


class GlitchBuffer : public AudioBuffer {
protected:
	int highWaterMarkIndex; // This is the highest buffer position that was written to in the last pass through the loop.

	float readPeekVoltage(int loopSize) override;
	int getReverseIndex(int loopSize) override;
	float doRead(int sampleRate, int loopSize, bool reverse) override;
	void doWrite(int sampleRate, int loopSize, float voltage) override;

public:
	GlitchBuffer(int bufferSize);

	bool atLoopStart() override;
	float loopPosition(int loopSize) override;
	int samplesRead(int loopSize) override;
	int samplesRemaining(int loopSize) override;
	void next(int loopSize) override;
};


class StretchBuffer : public AudioBuffer {
protected:
	float bufferPosition;
	bool loopStart;
	float previousVoltage;
	float scaleFactor;

	float readPeekVoltage(int loopSize) override;
	int getReverseIndex(int loopSize) override;
	float doRead(int sampleRate, int loopSize, bool reverse) override;
	void doWrite(int sampleRate, int loopSize, float voltage) override;

public:
	StretchBuffer(int bufferSize);

	bool atLoopStart() override;
	float loopPosition(int loopSize) override;
	int samplesRead(int loopSize) override;
	int samplesRemaining(int loopSize) override;
	void next(int loopSize) override;
};

