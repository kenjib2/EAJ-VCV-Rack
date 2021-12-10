class AudioBuffer {
protected:
	float* delayBuffer;
	int writePointer;
	int readPointer;
	bool latched;
	int latchLoopCounter;

public:
	int bufferSize;

	virtual bool atLoopStart() = 0;
	virtual float loopPosition(int loopSize) = 0;
	virtual float readNextVoltage(int sampleRate, int loopSize, bool reverse, bool forceDucking) = 0;
	virtual void writeNextVoltage(int sampleRate, int loopSize, float voltage) = 0;
	virtual void latch(int numLoops) = 0;
	
	bool isLatched() {
		return latched;
	}
	
	AudioBuffer(int bufferSize)
		: writePointer(0)
		, readPointer(0)
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

	void restartLoop();

public:
	StretchBuffer(int bufferSize);

	bool atLoopStart() override;
	// Relative position in loop from 0.f to 1.f
	float loopPosition(int loopSize) override;

	float readNextVoltage(int sampleRate, int loopSize, bool reverse, bool forceDucking) override;
	void writeNextVoltage(int sampleRate, int loopSize, float voltage) override;

	void latch(int numLoops) override;
};


class GlitchBuffer : public AudioBuffer {
private:
	int highWaterMarkPointer; // This is the highest buffer position that was written to in the last pass through the loop.

	void restartLoop();

public:
	GlitchBuffer(int bufferSize);

	bool atLoopStart() override;
	// Relative position in loop from 0.f to 1.f
	float loopPosition(int loopSize) override;

	float readNextVoltage(int sampleRate, int loopSize, bool reverse, bool forceDucking) override;
	void writeNextVoltage(int sampleRate, int loopSize, float voltage) override;

	void latch(int numLoops) override;
};

