-- Vertex
/* Modified from PsychToolbox 3, DLS */

/*
 * File: BasicGaborShader.vert.txt
 * Shader for drawing of basic parameterized gabor patches.
 *
 * This is the vertex shader. It takes the attributes (parameters)
 * provided by the Screen('DrawTexture(s)') command, performs some
 * basic calculations on it - the calculations that only need to be
 * done once per gabor patch and that can be reliably carried out
 * at sufficient numeric precision in a vertex shader - then it passes
 * results of computations and other attributes as 'varying' parameters
 * to the fragment shader.
 *
 * (c) 2007 by Mario Kleiner, licensed under MIT license.
 *		 
 */

/* Constants that we need 2*pi and square-root of 2*pi: */
const float twopi     = 2.0 * 3.141592654;
const float sqrtof2pi = 2.5066282746;

/* Conversion factor from degrees to radians: */
const float deg2rad = 3.141592654 / 180.0;

/* Texel position of center of gabor patch: Constant, set from Matlab */
/* once when the shader is created: */
uniform vec2  Center;

/* If disableNorm is set to 1.0, then the normalization term below is    */
/* not applied to the gabors final values. Default is 0.0, ie. apply it. */
uniform float disableNorm;

/* Constant from setup code: Premultiply to contrast value: */
uniform float contrastPreMultiplicator;

uniform float Contrast;
uniform float AngleDeg;
uniform float PhaseDeg;
uniform float Frequency;
uniform float SpaceConstant;

uniform vec4 modulateColor;

out float Angle;
out vec4  baseColor;
out float Phase;
out float FreqTwoPi;
out float Expmultiplier;

/* These come from stim */
in vec3 vertex_position;
in vec2 vertex_texcoord;
out vec2 texcoord;
uniform mat4 projMat;
uniform mat4 modelviewMat;


void main()
{
    /* Apply standard geometric transformations to patch: */
    gl_Position = projMat * modelviewMat * vec4(vertex_position, 1.0);

    /* Don't pass real texture coordinates, but ones corrected for hardware offsets (-0.5,0.5) */
    /* and so that the center of the gabor patch has coordinate (0,0): */
    texcoord  = vertex_texcoord+vec2(-0.5, -0.5);


    /* Convert Angle and Phase from degrees to radians: */
    Angle = deg2rad * (-1.*AngleDeg);
    Phase = deg2rad * PhaseDeg;
    /* Precalc a couple of per-patch constant parameters: */
    FreqTwoPi = Frequency * twopi;
    Expmultiplier = -0.5 / (SpaceConstant * SpaceConstant);

    /* Conditionally apply non-standard normalization term iff disableNorm == 0.0 */
    float mc = disableNorm + (1.0 - disableNorm) * (1.0 / (sqrtof2pi * SpaceConstant));

    /* Premultiply the wanted Contrast to the color: */
    baseColor = modulateColor * mc * Contrast * contrastPreMultiplicator;
}



-- Fragment

/*
 * File: BasicGaborShader.frag.txt
 * Shader for drawing of basic parameterized gabor patches.
 *
 * This is the fragment shader. It gets the per-patch-constant
 * parameters as varyings from the vertex shader and hardware
 * interpolators, then performs per fragment calculations to
 * compute and write the final pixel color.
 *
 * (c) 2007-2016 by Mario Kleiner, licensed under MIT license.
 *
 */

uniform vec4 Offset;
uniform vec2 validModulationRange;

in float Angle;
in vec4  baseColor;
in float Phase;
in float FreqTwoPi;
in float Expmultiplier;

in vec2 texcoord;
out vec4 fragcolor;

void main()
{
    /* Query current output texel position wrt. to Center of Gabor: */
    vec2 pos = texcoord;

    /* Compute (x,y) distance weighting coefficients, based on rotation angle: */
    /* Note that this is a constant for all fragments, but we can not do it in */
    /* the vertex shader, because the vertex shader does not have sufficient   */
    /* numeric precision on some common hardware out there. */
    vec2 coeff = vec2(cos(Angle), sin(Angle)) * FreqTwoPi;

    /* Evaluate sine grating at requested position, angle and phase: */
    float sv = sin(dot(coeff, pos) + Phase);

    /* Compute exponential hull for the gabor: */
    float ev = exp(dot(pos, pos) * Expmultiplier);

    /* Multiply/Modulate base color and alpha with calculated sine/gauss      */
    /* values, add some constant color/alpha Offset, assign as final fragment */
    /* output color: */
    fragcolor = (baseColor * clamp(ev * sv, validModulationRange[0],
    	      validModulationRange[1])) + Offset;
}

-- Uniforms

disableNorm 1.0
contrastPreMultiplicator 1.0
modulateColor ".5 .5 .5 .5"
Contrast .5
AngleDeg 0
PhaseDeg 0
Frequency 7.
SpaceConstant .13
Offset ".5 .5 .5 .5"
validModulationRange "-1.0 1.0"
