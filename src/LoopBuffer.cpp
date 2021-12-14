#include "plugin.hpp"
#include "LoopBuffer.hpp"


// THINGS THAT POP:
// NEED UPDATE TO USE BOOLEAN - SENSITIVITY TRIGGER (FADE)
// FIXED - LOOP LATCH (CROSS)
// REVERSE READ AND WRITE CROSSING (CROSS)
// GLITCH TIME CHANGES (CROSS)
// WHEN CHANGING TIME (Not sure?)
// CHANGING TIME CAN CRASH STUFF!
// Should bufferposition always get +0.5f???


GlitchBuffer::GlitchBuffer(int bufferSize)
	: AudioBuffer(bufferSize)
	, highWaterMarkIndex(0)
{
}


bool GlitchBuffer::atLoopStart() {
	return (readIndex == 0);
}


float GlitchBuffer::loopPosition(int loopSize) {
	return float(readIndex) / loopSize;
}


int GlitchBuffer::samplesRead(int loopSize) {
	return readIndex + 1;
}


int GlitchBuffer::samplesRemaining(int loopSize) {
	return std::min(highWaterMarkIndex, loopSize - 1) - readIndex;
}


int GlitchBuffer::getReverseIndex(int loopSize) {
	return loopSize - 1 - readIndex;
}


float GlitchBuffer::readPeekVoltage(int loopSize) {
	int peekIndex = readIndex;
	peekIndex -= FADE_SAMPLES;
	if (peekIndex < 0) {
		peekIndex += std::min(loopSize, highWaterMarkIndex + 1);
	}

	return loopBuffer[peekIndex];
}


float GlitchBuffer::doRead(int sampleRate, int loopSize, bool reverse) {
	float returnVoltage = 0.f;
	int reverseReadIndex = getReverseIndex(loopSize);

	if (reverse) {
		returnVoltage = loopBuffer[reverseReadIndex];
	}
	else {
		returnVoltage = loopBuffer[readIndex];
	}

	// Past the end of where we recorded last time just return silence
	if (readIndex > highWaterMarkIndex) {
		returnVoltage = 0.f;
	}

	return returnVoltage;
}


void GlitchBuffer::doWrite(int sampleRate, int loopSize, float voltage) {
	writeIndex = readIndex;

	loopBuffer[writeIndex] = voltage;

	// Updates when the loop size grows, truncates when it shrinks
	if (writeIndex >= loopSize - 1) {
		highWaterMarkIndex = writeIndex;
	}
//if (samplesRead(loopSize) < FADE_SAMPLES + 10) { DEBUG("samples read %d writeIndex %d loopSize %d write voltage: %f", samplesRead(loopSize), writeIndex, loopSize, voltage); }
//if (loopSize - samplesRead(loopSize) < FADE_SAMPLES + 10) { DEBUG("samples read %d writeIndex %d loopSize %d write voltage: %f", samplesRead(loopSize), writeIndex, loopSize, voltage); }
}


void GlitchBuffer::next(int loopSize) {
	readIndex++;
	loopSize = std::min(loopSize, bufferSize); // Avoid buffer overruns
	if (readIndex >= loopSize) {
		restartLoop();
		readIndex = readIndex % loopSize;
	}
}


// -----------------------------------------------------------------------------------------------


StretchBuffer::StretchBuffer(int bufferSize)
	: AudioBuffer(bufferSize)
	, bufferPosition(0.f)
	, loopStart(true)
	, previousVoltage(0.f)
	, scaleFactor(1.0f)
{
}


bool StretchBuffer::atLoopStart() {
	return loopStart;
}


float StretchBuffer::loopPosition(int loopSize) {
	return bufferPosition / bufferSize;
}


int StretchBuffer::samplesRead(int loopSize) {
	float scaledPosition = bufferPosition / scaleFactor;
	return int(scaledPosition + std::numeric_limits<float>::epsilon()) + 1;
}


int StretchBuffer::samplesRemaining(int loopSize) {
//	float scaledRemaining = ((float)bufferSize - bufferPosition) / scaleFactor;
//DEBUG("scaledRemaining %f = (bufferSize %d - bufferPosition %f) / scaleFactor %f", scaledRemaining, bufferSize, bufferPosition, scaleFactor);
//	return int(scaledRemaining + std::numeric_limits<float>::epsilon());
	return (loopSize - samplesRead(loopSize));
}


float StretchBuffer::readPeekVoltage(int loopSize) {
	int peekIndex = readIndex;
	int fadeScaled = (int)((float)FADE_SAMPLES * scaleFactor + std::numeric_limits<float>::epsilon());
	peekIndex -= fadeScaled;
	if (peekIndex < 0) {
		peekIndex += bufferSize;
	}

	return 0.f;
//	return loopBuffer[peekIndex];
}


int StretchBuffer::getReverseIndex(int loopSize) {
	return bufferSize - 1 - readIndex;
}


float StretchBuffer::doRead(int sampleRate, int loopSize, bool reverse) {
	float returnVoltage = 0.f;

	int reverseReadIndex = getReverseIndex(loopSize);
	if (reverse) {
		returnVoltage = loopBuffer[reverseReadIndex];
	}
	else {
		returnVoltage = loopBuffer[readIndex];
	}

//	DEBUG("samplesRead %d readIndex %d loopSize %d", samplesRead(loopSize), readIndex, loopSize);
//	DEBUG("samplesRemaining %d readIndex %d loopSize %d", samplesRemaining(loopSize), readIndex, loopSize);
//if (samplesRead(loopSize) < FADE_SAMPLES + 10) { DEBUG("Index %d / %d read voltage: %f samples read %d fade coefficient %f", readIndex, loopSize, returnVoltage, samplesRead(loopSize), getFadeCoefficient(loopSize, -1)); }
//if (loopSize - samplesRead(loopSize) < FADE_SAMPLES + 10) { DEBUG("Index %d / %d read voltage: %f samples read %d fade coefficient %f", readIndex, loopSize, returnVoltage, samplesRead(loopSize), getFadeCoefficient(loopSize, 1)); }
//DEBUG("Index %d / %d read voltage: %f samples read %d", readIndex, bufferSize, returnVoltage, samplesRead(loopSize));

	return returnVoltage;
}


void StretchBuffer::doWrite(int sampleRate, int loopSize, float voltage) {
	static int lastWriteIndex = 0;

	if ((writeIndex < 0) || (writeIndex >= bufferSize)) { // Avoid buffer overrun
DEBUG("ERROR IN DOWRITE");
		writeIndex = 0;
	}

	if (writeIndex == readIndex) {
		writeIndex = lastWriteIndex;
	}

	int numWrites = 0;
	if (readIndex >= writeIndex) {
		numWrites = readIndex - writeIndex;
	}

	if (numWrites > 0) {
		for (int i = 0; i < numWrites; i++) {
			float interpolatedVoltage = crossFade(previousVoltage, (numWrites - i - 1) / numWrites, voltage);
			loopBuffer[(writeIndex + i) % (bufferSize)] = interpolatedVoltage;
		}
	}

//if (samplesRead(loopSize) < FADE_SAMPLES + 10) { DEBUG("Index %d / %d write voltage: %f samples read %d fade coefficient %f", readIndex, loopSize, voltage, samplesRead(loopSize), getFadeCoefficient(loopSize, -1)); }
//if (loopSize - samplesRead(loopSize) < FADE_SAMPLES + 10) { DEBUG("Index %d / %d write voltage: %f samples read %d fade coefficient %f", readIndex, loopSize, voltage, samplesRead(loopSize), getFadeCoefficient(loopSize, 1)); }
//DEBUG("Index %d / %d write voltage: %f samples read %d", readIndex, bufferSize, voltage, samplesRead(loopSize));
	previousVoltage = voltage;
	lastWriteIndex = writeIndex;
	writeIndex = readIndex;
}


void StretchBuffer::next(int loopSize) {
	scaleFactor = (float)bufferSize / (float)loopSize;
	bufferPosition += scaleFactor;

	if (int(bufferPosition + 0.5f) >= bufferSize) {
		bufferPosition -= bufferSize;
		loopStart = true;
		restartLoop();
	}
	else {
		loopStart = false;
	}

	readIndex = int(bufferPosition + 0.5f); // 0.5f is for rounding
	readIndex = std::min(readIndex, bufferSize - 1); // Avoid buffer overrun
//DEBUG("Next called: bufferPosition %f / readIndex %d / bufferSize %d read %d remaining %d loopSize %d", bufferPosition, readIndex, bufferSize, samplesRead(loopSize), samplesRemaining(loopSize), loopSize);
}


