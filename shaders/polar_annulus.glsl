// polar_annulus.glsl
// simple fragment shader
#ifdef GL_ES
precision mediump float;
precision mediump int;
#endif
#define PROCESSING_TEXTURE_SHADER

// #extension GL_EXT_gpu_shader4 : enable
// 'time' contains seconds since the program was linked.
uniform float time;
uniform vec2 resolution;

uniform float t;
uniform float StimSize;
uniform float NSpokes;
uniform float NRings;
uniform float HoleSize;
uniform float CycleTime;
uniform float WedgeSize;
uniform float Direction;
uniform float DutyCycle;

uniform sampler2D textureSampler;
uniform vec2 texOffset;
varying vec4 vertColor;
varying vec4 vertTexCoord;

const float pi  = 3.14159;
const float pi2 = 6.28319;

vec4 getColor(int id);

void main()
{
  // Discard 50ms out of every 250 to incr. onset-related neural activity
  if (mod(t * 1000., 250.) < 50.) discard;

  // s and t are x and y coordinates, stimulus is size 1 by 1
  float dx = vertTexCoord.s - .5;
  float dy = vertTexCoord.t - .5;
  // distance from the center
  float d = sqrt(dx * dx + dy * dy);
  
  // if the point is inside the hole, discard it
  if (d < HoleSize) discard;
  //if (d > 0.5) discard;
  
  // Create Annulus
  float curTime = mod(t, CycleTime);
  // To make annulus contract, reverse time
  if (Direction < 0.) {
    curTime = CycleTime - curTime;
  }
  
  float fovBrain     = 0.05 * 17.3 / 0.75;
  float totalBrain   = log(StimSize / 2.0 + fovBrain);
  float brainPerStep = DutyCycle / 100. * totalBrain;
  float totalDist    = totalBrain;
  float rate         = totalDist / CycleTime;
  float stepStart    = rate * curTime;
  float stepEnd      = rate * curTime + brainPerStep;
  float innerEccen   = exp(stepStart) - fovBrain;
  float innerR       = innerEccen / StimSize;
  float outerEccen   = exp(stepEnd) - fovBrain;
  float outerR       = outerEccen / StimSize;
  
  if (d < innerR) discard;
  if (d > outerR) discard;
  
  // Calculate cycleID
  float brainPerCycle = totalBrain / NRings;   	      // same brain distance for each cycle
  float curBrainCoord = log(d * StimSize + fovBrain);	      // change d to brain coordinates
  float cycleID       = floor(curBrainCoord / brainPerCycle); // determine which cycle
  
  // Use angle to decide: "which spoke"
  // atan returns angle from -pi/2 to pi/2, so shift 0 to 2pi
  float angle = atan(dy/dx);
  if (dx < 0.0 ) {
    angle = angle + pi;
  }
  else {
    if (dy > 0.0) {
      angle = angle;			
    }
    else {
      angle = angle + pi2;	
    }
  }
  // Shift spokeID by one offset odds/evens, mod to make sure IDs are sequential
  int spokeID = int(mod(floor(angle / (pi2 / NSpokes) + cycleID), NSpokes));
  
  // Combine into 1 ID, moves from center out
  int checkID = int(float(spokeID) + (cycleID * NSpokes));
  
  // Get "random" color to return
  gl_FragColor = getColor(checkID);
}

/// Take an id corresponding to a box and return its color and intensity
vec4 getColor(int id)
{
  float alpha = 1.;
  // Change the color at 4Hz
  int nCol = int(floor(t * 4.)) + 1;
  
  // Use id to get some "random" number
  float idt = float(id * nCol);
  float rand = cos(idt) * idt * 23. + sin(idt / 7.) + 32. * idt + 19.;
  // Split the "random" number into 3 colors
  float r = mod(rand, 509.) / 509.;
  float g = mod(rand, 522.) / 523.;
  float b = mod(rand, 499.) / 499.;
  
  // Normalize so the colors look bright
  vec3 color = vec3(r, g, b);
  color = normalize(color);
  
  // Scale color with polarity to preserve the checkerboard pattern
  // Swap polarity at 4 Hz
  
  // Use 0.8 of the polarity so there are no black or white checks
  float polarity = (mod(float(nCol + id),2.0) - 0.5) * 0.7 + 0.5;
  color = color * polarity;
  
  // Return color for this pixel
  vec4 checkColor = vec4 (color, alpha);
  return checkColor;
}
