#include "plugin.hpp"
#include "LoopBuffer.hpp"


// THINGS THAT POP:
// SENSITIVITY TRIGGER (FADE -- is that just a crossfade against 0?)
// LOOP LATCH (CROSS)
// REVERSE READ AND WRITE CROSSING (CROSS)
// GLITCH TIME CHANGES (CROSS)


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


int GlitchBuffer::samplesRemaining(int loopSize) {
	return std::min(highWaterMarkIndex, loopSize - 1) - readIndex;
}


int GlitchBuffer::samplesRead() {
	return readIndex + 1;
}


float GlitchBuffer::getFadeCoefficient() {
	return 0.f;
}


float GlitchBuffer::readPeekVoltage() {
//	int peekIndex = readIndex;

	return 0.f;
}


float GlitchBuffer::readNextVoltage(int sampleRate, int loopSize, bool reverse, bool forceDucking) {
	loopSize = std::min(loopSize, bufferSize); // Avoid buffer overruns
	readIndex++;
	if (readIndex >= loopSize) {
		restartLoop();
	}
	readIndex = readIndex % loopSize;
	float returnVoltage = 0.f;
	int reverseReadIndex = loopSize - 1 - readIndex;

	if (reverse) {
		returnVoltage = delayBuffer[reverseReadIndex];
	}
	else {
		returnVoltage = delayBuffer[readIndex];
	}

	// Past the end of where we recorded last time just return silence
	if (readIndex > highWaterMarkIndex) {
		returnVoltage = 0.f;
	}

	return removePops(returnVoltage);
}


void GlitchBuffer::writeNextVoltage(int sampleRate, int loopSize, float voltage) {
	writeIndex = readIndex;

	if (!latched) {
		delayBuffer[writeIndex] = voltage;

		// Updates when the loop size grows, truncates when it shrinks
		if (writeIndex >= loopSize - 1) {
			highWaterMarkIndex = writeIndex;
		}
	}
}


StretchBuffer::StretchBuffer(int bufferSize)
	: AudioBuffer(bufferSize)
{
}


bool StretchBuffer::atLoopStart() {
	return loopStart;
}


float StretchBuffer::loopPosition(int loopSize) {
	return bufferPosition / bufferSize;
}


int StretchBuffer::samplesRemaining(int loopSize) {
	return 0;
}


int StretchBuffer::samplesRead() {
	return 0;
}


float StretchBuffer::getFadeCoefficient() {
	return 0.f;
}


float StretchBuffer::readPeekVoltage() {
	return 0.f;
}


float StretchBuffer::readNextVoltage(int sampleRate, int loopSize, bool reverse, bool forceDucking) {
	float returnVoltage = 0.f;
	float scaleFactor = (float)bufferSize / (float)loopSize;
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

	int reverseReadIndex = bufferSize - 1 - readIndex;

	if (reverse) {
		returnVoltage = delayBuffer[reverseReadIndex];
	}
	else {
		returnVoltage = delayBuffer[readIndex];
	}

	return removePops(returnVoltage);
}


void StretchBuffer::writeNextVoltage(int sampleRate, int loopSize, float voltage) {
	if ((writeIndex < 0) || (writeIndex >= bufferSize)) { // Avoid buffer overrun
		writeIndex = bufferSize - 1;
	}

	if (!latched) {
		int numWrites = 0;
		if (readIndex >= writeIndex) {
			numWrites = readIndex - writeIndex;
		}
		else {
			numWrites += bufferSize - 1 - writeIndex + readIndex;
		}

		for (int i = 0; i <= numWrites; i++) {
			delayBuffer[(writeIndex + i) % (bufferSize - 1)] = voltage;
		}

		writeIndex = readIndex;
	}
}

