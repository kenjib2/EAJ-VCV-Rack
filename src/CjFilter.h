// CjFilter.h
// created by Hagen Ueberfuhr
// www.klirrfactory.com
// This class is based on a video by Jacub Ciupinski
// https://www.youtube.com/watch?v=-WhbYLxEuMU
//
// License:
// This source code is provided as is, without warranty.
// You may copy and distribute verbatim copies of this document.
// You may modify and use this source code to create binary code for your own
// purposes, free or commercial.
//
// This class is made for VCV Rack.
// If you want to use it for VST or other music software that uses voltges between -1 and +1 V
// change:
// output = input *.2f;
// to
// output = input;
// and (two times)
// filterOut[i] = output * 5.f;
// to
// filterOut[i] = output;

#pragma once

class CjFilter {

  public:
    float output;
    int counter;
    float feedback[4][16];
    float filterOut[4];
    // This method returns a pointer to the filterOut[4] array
    float doFilter(float input,float cut, float res);
    float damp;

    CjFilter(){
      output = 0.f;
      counter = 0;
      damp = 1.f;
      for (int i = 0; i<4; i++){
        filterOut[i] = 0.f;
        for (int j = 0; j<16; j++){
          feedback[i][j] = 0.f;
        }
      }
    };
  ~CjFilter(){
  };
};

float CjFilter::doFilter(float input,float cut, float res){
  // Normalize the Resonance Input to 0-1.f
  res *= 3.6;
  // Damp Resonanced on hgiher frequencies
  if (cut > 0.5f){
    damp = 1.f + (0.5f-cut);
  }
  else {
    damp = 1.f;
  }
  cut *= cut;
  // Damp Resonance on higher frequencies
  res = (1.f-(cut*0.64f)) * res;

  if (counter >= 8){
    counter = 0;
  }
  output = input *.2f;
  for (int i=0; i<4; i++){
    // Resonance is only fed back into the first filter
    if (i==0){
      output = (output * cut) + (feedback[i][counter+7] * (1.f-cut)) + damp*feedback[3][counter+6] * res * -1.f*cut;
      feedback[i][counter] = output;
      feedback[i][counter+8] = output;
      filterOut[i] = output * 5.f;
    }
    else{
      output = (output * cut) + (feedback[i][counter+7] * (1.f-cut));
      feedback[i][counter] = output;
      feedback[i][counter+8] = output;
      filterOut[i] = output * 5.f;
    }
  }
  counter ++;
  return *filterOut;
}
