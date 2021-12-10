#include "plugin.hpp"
#include "LoopBuffer.hpp"


GlitchBuffer::GlitchBuffer(int bufferSize)
	: AudioBuffer(bufferSize)
	, highWaterMarkPointer(0)
{
}


bool GlitchBuffer::atLoopStart() {
	return (readPointer == 0);
}


float GlitchBuffer::loopPosition(int loopSize) {
	return float(readPointer) / loopSize;
}


void GlitchBuffer::latch(int numLoops) {
	latchLoopCounter = numLoops;
}


void GlitchBuffer::restartLoop() {
	if (latchLoopCounter > 0) {
		latched = true;
		latchLoopCounter--;
	}
	else {
		latched = false;
	}
}


float GlitchBuffer::readNextVoltage(int sampleRate, int loopSize, bool reverse, bool forceDucking) {
	loopSize = std::min(loopSize, bufferSize); // Avoid buffer overruns
	readPointer++;
	if (readPointer >= loopSize) {
		restartLoop();
	}
	readPointer = readPointer % loopSize;
	float returnVoltage = 0.f;
	int reverseReadPointer = loopSize - 1 - readPointer;

	if (reverse) {
		returnVoltage = delayBuffer[reverseReadPointer];
	}
	else {
		returnVoltage = delayBuffer[readPointer];
	}

	// Past the end of where we recorded last time just return silence
	if (readPointer > highWaterMarkPointer) {
		returnVoltage = 0.f;
	}

	return returnVoltage;
}


void GlitchBuffer::writeNextVoltage(int sampleRate, int loopSize, float voltage) {
	writePointer = readPointer;

	if (!latched) {
		delayBuffer[writePointer] = voltage;

		// Updates when the loop size grows, truncates when it shrinks
		if (writePointer >= loopSize - 1) {
			highWaterMarkPointer = writePointer;
		}
	}
}


StretchBuffer::StretchBuffer(int bufferSize) : AudioBuffer(bufferSize) {
}


bool StretchBuffer::atLoopStart() {
	return loopStart;
}


float StretchBuffer::loopPosition(int loopSize) {
	return bufferPosition / bufferSize;
}


void StretchBuffer::latch(int numLoops) {
	latchLoopCounter = numLoops;
}


void StretchBuffer::restartLoop() {
	if (latchLoopCounter > 0) {
		latched = true;
		latchLoopCounter--;
	}
	else {
		latched = false;
	}
}


float StretchBuffer::readNextVoltage(int sampleRate, int loopSize, bool reverse, bool forceDucking) {
	float returnVoltage = 0.f;
	float scaleFactor = (float)bufferSize / (float)loopSize;
	bufferPosition += scaleFactor;

	if (int(bufferPosition + 0.5f) >= bufferSize) {
		bufferPosition -= bufferSize;
		loopStart = true;
	}
	else {
		loopStart = false;
	}

	readPointer = int(bufferPosition + 0.5f); // 0.5f is for rounding
	readPointer = std::min(readPointer, bufferSize - 1); // Avoid buffer overrun

	int reverseReadPointer = bufferSize - 1 - readPointer;

	if (reverse) {
		returnVoltage = delayBuffer[reverseReadPointer];
	}
	else {
		returnVoltage = delayBuffer[readPointer];
	}

	return returnVoltage;
}


void StretchBuffer::writeNextVoltage(int sampleRate, int loopSize, float voltage) {
	if ((writePointer < 0) || (writePointer >= bufferSize)) { // Avoid buffer overrun
		writePointer = bufferSize - 1;
	}

	if (!latched) {
		int numWrites = 0;
		if (readPointer >= writePointer) {
			numWrites = readPointer - writePointer;
		}
		else {
			numWrites += bufferSize - 1 - writePointer + readPointer;
		}

		for (int i = 0; i <= numWrites; i++) {
			delayBuffer[(writePointer + i) % (bufferSize - 1)] = voltage;
		}

		writePointer = readPointer;
	}
}

