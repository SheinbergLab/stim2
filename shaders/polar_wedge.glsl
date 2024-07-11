-- Vertex

in vec3 vertex_position;
in vec2 vertex_texcoord;

out vec2 texcoord;

uniform mat4 projMat;
uniform mat4 modelviewMat;

void main(void)
{
  texcoord  = vertex_texcoord;
  gl_Position = projMat * modelviewMat * vec4(vertex_position, 1.0);
}


-- Fragment

in vec2 texcoord;
out vec4 fragcolor;

// 'time' contains seconds since the program was linked.
uniform float time;
uniform float StimSize;
uniform float NSpokes;
uniform float NRings;
uniform float HoleSize;
uniform float CycleTime;
uniform float WedgeSize;

float pi  = 3.14159;
float pi2 = 6.28319;

vec4 getColor(int id);

void main()
{
  // Discard 50ms out of every 250 to incr. onset-related neural activity
  if (mod(time * 1000., 250.) < 50.) discard;               

  // s and t are x and y coordinates, stimulus is size 1 by 1
  float dx = texcoord.s-.5;
  float dy = texcoord.t-.5;

  // distance from the center
  float d = sqrt(dx*dx+dy*dy);

  // if the point is inside the hole, discard it
  if (d < HoleSize) discard;
  // if (d > 0.5) discard;
  
  // Use angle to decide: "in wedge?" and "which spoke"
  // atan returns angle from -pi/2 to pi/2, so shift 0 to 2pi
  float angle = atan(dy/dx);
  if (dx < 0.0 ) {
    angle = angle + pi;
  }
  else {
    if (dy > 0.0) {
      angle = angle;			}
    else {
      angle = angle + pi2;	}
  }
  
  // Keep current angle < 2*pi, angle will never be > 2*pi
  float startAng   = mod(pi2 * time / CycleTime - 3. * pi / 4., pi2);
  float endAng     = mod((pi2 * time / CycleTime - 3. * pi / 4. + pi2 * WedgeSize), pi2);
  // Account for discontinuity at angle = 0/2pi/4pi/etc.
  float isCont     = sign((startAng - endAng));
  float startCheck = sign(angle - startAng);
  float endCheck   = sign(angle - endAng);

  if (isCont * startCheck * endCheck < 0.0) discard;          // if outside wedge, discard
  
  // Calculate cycleID
  // Use radius (d) to decide which ring the point is in
  float fovBrain      = 0.05 * 17.3 / 0.75;
  float totalBrain    = log(StimSize / 2. + fovBrain);
  float brainPerCycle = totalBrain / NRings;		      // same brain distance for each cycle
  float curBrainCoord = log(d * StimSize + fovBrain);         // change d to brain coordinates
  float cycleID       = floor(curBrainCoord / brainPerCycle); // determine which cycle
  
  // Shift spokeID by one offset odds/evens, mod to make sure IDs are sequential
  int spokeID = int(mod(floor(angle / (pi2 / NSpokes) + cycleID), NSpokes));
  
  // Combine into 1 ID, moves from center out
  int checkID = int(float(spokeID) + (cycleID * NSpokes));
  
  // Get "random" color to return
  fragcolor = getColor(checkID);
}

/// Take an id corresponding to a box and return its color and intensity
vec4 getColor(int id)
{
  float alpha = 1.;
  // Change the color at 4Hz
  int nCol = int(floor(time * 4.)) + 1;
  
  // Use id to get some "random" number
  float idt  = float(id * nCol);
  float rand = cos(idt) * idt * 23. + sin(idt / 7.) + 32. * idt + 19.;
  // Split the "random" number into 3 colors
  float r = mod(rand, 509.) / 509.;
  float g = mod(rand, 523.) / 523.;
  float b = mod(rand, 499.) / 499.;
  
  // Normalize so the colors look bright
  vec3 color = vec3(r, g, b);
  color = normalize(color);
  
  // Scale color with polarity to preserve the checkerboard pattern
  // Swap polarity at 4 Hz
  
  // Use 0.8 of the polarity so there are no black or white checks
  float polarity = (mod(float(nCol + id), 2.0) - 0.5) * 0.7 + 0.5;
  color = color * polarity;
  
  // Return color for this pixel
  vec4 checkColor = vec4 (color, alpha);
  return checkColor;
}


-- Uniforms

NRings 15.0
WedgeSize .25
CycleTime 32.
NSpokes 24.
StimSize 12.
HoleSize 0.
