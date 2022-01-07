#include "plugin.hpp"
#include "LoopBuffer.hpp"
#define DEBUG_GLITCH_POP_REMOVAL


// THINGS THAT POP:
// FIXED - SENSITIVITY TRIGGER (FADE)
// FIXED - LOOP LATCH (CROSS)
// REVERSE READ AND WRITE CROSSING (CROSS)
// GLITCH TIME CHANGES (CROSS)
// WHEN CHANGING TIME (Not sure?)
// CHANGING TIME CAN CRASH STUFF!
// Should bufferposition always get +0.5f???


GlitchBuffer::GlitchBuffer(int bufferSize)
	: AudioBuffer(bufferSize)
	, previousLoopSize(0)
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
	return std::min(previousLoopSize - 1, loopSize - 1) - readIndex;
}


int GlitchBuffer::getReverseIndex(int loopSize) {
	return loopSize - 1 - readIndex;
}


float GlitchBuffer::readPeekVoltage(int loopSize) {
	int peekIndex = readIndex;
	peekIndex -= FADE_SAMPLES;
	if (peekIndex < 0) {
		peekIndex += std::min(loopSize, previousLoopSize);
	}

	return loopBuffer[peekIndex];
}


float GlitchBuffer::doRead(int sampleRate, int loopSize, bool reverse) {
	float returnVoltage = 0.f;
	int reverseReadIndex = getReverseIndex(loopSize);
	static bool needLocalPeekReturn = false;
	#ifdef DEBUG_READ_CPP
		static float prevReturnVoltage = 0.f;
	#endif

	if (reverse) {
		returnVoltage = loopBuffer[reverseReadIndex];
	}
	else {
		returnVoltage = loopBuffer[readIndex];

		if (loopSize > previousLoopSize && previousLoopSize - 1 - readIndex >= 0 && previousLoopSize - 1 - readIndex < FADE_SAMPLES) {
			// Current loop is bigger. There will be empty space at the end. Fade out to 0.f voltage.
			returnVoltage = crossFade(returnVoltage, (float)(previousLoopSize - 1 - readIndex) / FADE_SAMPLES, 0.f);
			#ifdef DEBUG_GLITCH_POP_REMOVAL
				DEBUG("ptr %d / %d (%d) Fading out glitch buffer mid loop: %f", readIndex, loopSize, previousLoopSize, returnVoltage);
			#endif
			needLocalPeekReturn = true;
		} else if (loopSize < previousLoopSize && samplesRemaining(loopSize) < FADE_SAMPLES) {
			// Current loop is shorter. Fade out to 0.f voltage.
			returnVoltage = crossFade(returnVoltage, (float)samplesRemaining(loopSize) / FADE_SAMPLES, 0.f);
			#ifdef DEBUG_GLITCH_POP_REMOVAL
				DEBUG("ptr %d / %d (%d) Fading out glitch buffer end: %f", readIndex, loopSize, previousLoopSize, returnVoltage);
			#endif
			needLocalPeekReturn = true;
		}

		// THERE IS ONE RARE GLITCH LEFT -- CHECK FOR ABS(PREVIOUS VOLTAGE - CURRENT) > X AT LOOP RESTART TO FIND IT.
		// SOMETHING IS CRASHING SOMETIMES TOO -- HOPEFULLY RELATED
		if ((readIndex <= FADE_SAMPLES && needLocalPeekReturn)
			|| (readIndex <= FADE_SAMPLES && previousLoopSize != loopSize)
			) {
			if (readIndex == FADE_SAMPLES) {
				needLocalPeekReturn = false;
			}
			returnVoltage = crossFade(returnVoltage, (float)readIndex / FADE_SAMPLES, 0.f);
			#ifdef DEBUG_GLITCH_POP_REMOVAL
				DEBUG("ptr %d / %d (%d) Fading in glitch buffer: %f", readIndex, loopSize, previousLoopSize, returnVoltage);
			#endif
		}
	}

	// Past the end of where we recorded last time just return silence
	if (readIndex > previousLoopSize - 1) {
		returnVoltage = 0.f;
	}

	#ifdef DEBUG_READ_CPP
		if (samplesRead(loopSize) < FADE_SAMPLES + 10) { DEBUG("Index %d / %d read voltage: %f", readIndex, loopSize, returnVoltage); }
		if (loopSize - samplesRead(loopSize) < FADE_SAMPLES + 10) { DEBUG("Index %d / %d read voltage: %f", readIndex, loopSize, returnVoltage); }
		if (std::abs(prevReturnVoltage - returnVoltage > 0.3f)) { DEBUG("ERROR %d loopSize %d prevLoopSize %d read voltage %f prev voltage %f", readIndex, loopSize, previousLoopSize, returnVoltage, prevReturnVoltage ); }
		prevReturnVoltage = returnVoltage;
	#endif

	return returnVoltage;
}


void GlitchBuffer::doWrite(int sampleRate, int loopSize, float voltage) {
	writeIndex = readIndex;

	float writeVoltage = voltage;

	if (loopSize > previousLoopSize && previousLoopSize - 1 - writeIndex >= 0 && previousLoopSize - 1 - writeIndex < FADE_SAMPLES) {
		// Current loop is bigger. Fade out previous loop to 0.f voltage on write.
		writeVoltage = crossFade(writeVoltage, (float)(previousLoopSize - 1 - writeIndex) / FADE_SAMPLES, 0.f);
#ifdef DEBUG_GLITCH_POP_REMOVAL
		DEBUG("ptr %d / %d (%d) Fading out glitch write buffer mid loop: %f", writeIndex, loopSize, previousLoopSize, writeVoltage);
#endif
	}

	// Fade out the write if we are going to clear the buffer
	if (writeIndex > loopSize - 1 - FADE_SAMPLES && previousLoopSize > loopSize) {
		writeVoltage = crossFade(writeVoltage, (float)(loopSize - 1 - writeIndex) / FADE_SAMPLES, 0.f);
#ifdef DEBUG_GLITCH_POP_REMOVAL
		DEBUG("ptr %d / %d (%d) Fading out glitch write buffer: %f", readIndex, loopSize, previousLoopSize, writeVoltage);
#endif
	}

	loopBuffer[writeIndex] = writeVoltage;

	// Clear out the buffer after the end of the current loop
	if (writeIndex == loopSize - 1 && previousLoopSize > loopSize) {
		int startIndex = writeIndex + 1;
		int endIndex = bufferSize - 1;
		std::fill(loopBuffer + startIndex, loopBuffer + endIndex, 0.0f);
#ifdef DEBUG_GLITCH_POP_REMOVAL
		DEBUG("ptr %d / %d (%d) Clearing past loop end.", readIndex, loopSize, previousLoopSize);
		for (int i = 0; i < 20; i++) {
			DEBUG("ptr %d voltage %f", startIndex + i - 10, loopBuffer[startIndex + i - 10]);
		}
#endif
	}
}


void GlitchBuffer::next(int loopSize) {
	readIndex++;
	loopSize = std::min(loopSize, bufferSize); // Avoid buffer overruns
	if (readIndex >= loopSize) {
		previousLoopSize = loopSize;
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

//	return 0.f;
	return loopBuffer[peekIndex];
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
}


