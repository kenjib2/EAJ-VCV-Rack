const int FADE_SAMPLES = 60;


class AudioBuffer {
protected:
	float* delayBuffer;
	int writeIndex;
	int readIndex;
	bool latched;
	int latchLoopCounter;

	virtual float getFadeCoefficient() = 0;
	virtual float readPeekVoltage() = 0;

	// amountIn goes from 0.f to 1.f. Use a 0.f voltageOut to fade in/out.
	float crossFade(float voltageIn, float inCoefficient, float voltageOut) {
		return voltageIn * inCoefficient + voltageOut * (1.f - inCoefficient);
	}

	float removePops(float voltageIn) {
		if (isLatched()) {
//			float peekVoltage = readPeekVoltage();
		}

		return voltageIn;
	}

	void restartLoop() {
		if (latchLoopCounter > 0) {
			latched = true;
			latchLoopCounter--;
		}
		else {
			latched = false;
		}
	}

public:
	int bufferSize;

	virtual bool atLoopStart() = 0;
	virtual float loopPosition(int loopSize) = 0; // Relative position in loop from 0.f to 1.f
	virtual int samplesRemaining(int loopSize) = 0;
	virtual int samplesRead() = 0;

	virtual float readNextVoltage(int sampleRate, int loopSize, bool reverse, bool forceDucking) = 0;
	virtual void writeNextVoltage(int sampleRate, int loopSize, float voltage) = 0;
	
	void latch(int numLoops) {
		latchLoopCounter = numLoops;
	}

	bool isLatched() {
		return latched;
	}
	
	AudioBuffer(int bufferSize)
		: writeIndex(0)
		, readIndex(0)
		, latched(false)
		, latchLoopCounter(0)
		, bufferSize(bufferSize)
	{
		delayBuffer = new float[bufferSize](); // parens initialize the buffer to all zeroes
	}

	virtual ~AudioBuffer() {
		delete delayBuffer;
	}
};


class StretchBuffer : public AudioBuffer {
private:
	float bufferPosition;
	bool loopStart;

	float getFadeCoefficient() override;
	float readPeekVoltage() override;

public:
	StretchBuffer(int bufferSize);

	bool atLoopStart() override;
	float loopPosition(int loopSize) override;
	virtual int samplesRemaining(int loopSize) override;
	virtual int samplesRead() override;

	float readNextVoltage(int sampleRate, int loopSize, bool reverse, bool forceDucking) override;
	void writeNextVoltage(int sampleRate, int loopSize, float voltage) override;
};


class GlitchBuffer : public AudioBuffer {
private:
	int highWaterMarkIndex; // This is the highest buffer position that was written to in the last pass through the loop.

	float getFadeCoefficient() override;
	float readPeekVoltage() override;

public:
	GlitchBuffer(int bufferSize);

	bool atLoopStart() override;
	float loopPosition(int loopSize) override;
	virtual int samplesRemaining(int loopSize) override;
	virtual int samplesRead() override;

	float readNextVoltage(int sampleRate, int loopSize, bool reverse, bool forceDucking) override;
	void writeNextVoltage(int sampleRate, int loopSize, float voltage) override;
};

