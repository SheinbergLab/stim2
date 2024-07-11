-- Vertex

/*
 * File: BasicGaussBlobShader.vert.txt
 * Shader for drawing of basic parameterized gaussian blob patches.
 *
 * This is the vertex shader. It takes the attributes (parameters)
 * provided by the Screen('DrawTexture(s)') command, performs some
 * basic calculations on it - the calculations that only need to be
 * done once per gauss blob patch and that can be reliably carried out
 * at sufficient numeric precision in a vertex shader - then it passes
 * results of computations and other attributes as 'varying' parameters
 * to the fragment shader.
 *
 * (c) 2014 by Mario Kleiner, licensed under MIT license.
 */

/* Constants that we need: square-root of 2*pi: */
const float sqrtof2pi = 2.5066282746;

/* Conversion factor from degrees to radians: */
const float deg2rad = 3.141592654 / 180.0;

/* Texel position of center of blob patch: Constant, set */
/* once when the shader is created: */
//uniform vec2 Center;

/* If disableNorm is set to 1.0, then the normalization term below is    */
/* not applied to the blobs final values. Default is 0.0, ie. apply it. */
uniform float disableNorm;

/* Constant from setup code: Premultiply to contrast value: */
uniform float contrastPreMultiplicator;
uniform float Contrast;
uniform float AngleDeg;
uniform float SpaceConstant;
uniform float Gamma;

/* Attributes passed from Screen(): See the ProceduralShadingAPI.m file for infos: */
//attribute vec4 sizeAngleFilterMode;
uniform vec4 modulateColor;
//attribute vec4 auxParameters0;

/* Information passed to the fragment shader: Attributes and precalculated per patch constants: */
varying vec4 baseColor;
varying float Expmultiplier;
varying float Angle;
varying float GammaSquared;

/* These come from stim */
in vec3 vertex_position;
in vec2 vertex_texcoord;
out vec2 texcoord;
uniform mat4 projMat;
uniform mat4 modelviewMat;

void main()
{
    /* Apply standard geometric transformations to patch: */
    // gl_Position = ftransform();
    gl_Position = projMat * modelviewMat * vec4(vertex_position, 1.0);

    /* Don't pass real texture coordinates, but ones corrected for hardware offsets (-0.5,0.5) */
    /* and so that the center of the gabor patch has coordinate (0,0): */
    // gl_TexCoord[0] = gl_MultiTexCoord0 - vec4(Center, 0.0, 0.0) + vec4(-0.5, 0.5, 0.0, 0.0);
    texcoord  = vertex_texcoord+vec2(-0.5, -0.5);
    
    /* Contrast value is stored in auxParameters0[0]: */
    //    float Contrast = auxParameters0[0];
    // Now a uniform

    /* Convert Angle from degrees to radians: */
    //    Angle = deg2rad * sizeAngleFilterMode.z;
    Angle = deg2rad * AngleDeg;

    /* Precalc a couple of per-patch constant parameters: */
    //    float SpaceConstant = auxParameters0[1];
    Expmultiplier = -0.5 / (SpaceConstant * SpaceConstant);

    /* The spatial aspect ratio Gamma is stored in the 3rd aux parameter. */
    //    float Gamma = auxParameters0[2];
    GammaSquared = Gamma * Gamma;

    /* Conditionally apply non-standard normalization term iff disableNorm == 0.0 */
    float mc = disableNorm + (1.0 - disableNorm) * (1.0 / (sqrtof2pi * SpaceConstant));

    /* Premultiply the wanted Contrast to the color: */
    baseColor = modulateColor * mc * Contrast * contrastPreMultiplicator;
}


-- Fragment

/*
 * File: BasicGaussBlobShader.frag.txt
 * Shader for drawing of basic parameterized Gaussian blob patches.
 *
 * This is the fragment shader. It gets the per-patch-constant
 * parameters as varyings from the vertex shader and hardware
 * interpolators, then performs per fragment calculations to
 * compute and write the final pixel color.
 *
 * (c) 2014 by Mario Kleiner, licensed under MIT license.
 *
 */

uniform vec4 Offset;
varying vec4  baseColor;
varying float Expmultiplier;
varying float Angle;
varying float GammaSquared;

in vec2 texcoord;

void main()
{
    /* Compute sine and cosine coefficients, based on rotation angle: */
    /* Note that this is a constant for all fragments, but we can not do it in */
    /* the vertex shader, because the vertex shader does not have sufficient   */
    /* numeric precision on some common hardware out there. */
    float st = sin(Angle);
    float ct = cos(Angle);

    /* Query current output texel position wrt. to Center of Gabor: */
    //    vec2 pos = gl_TexCoord[0].xy;
    vec2 pos = texcoord;

    /* Compute x' and y' terms: */
    float xdash = dot(vec2( ct, st), pos);
    float ydash = dot(vec2(-st, ct), pos);

    /* Compute exponential hull for the gauss blob: */
    float ev = exp(((xdash * xdash)  + (GammaSquared * ydash * ydash)) * Expmultiplier);

    /* Multiply/Modulate base color and alpha with calculated gauss blob */
    /* values, add some constant color/alpha Offset, assign as final fragment */
    /* output color: */
    gl_FragColor = (baseColor * ev) + Offset;
}


-- Uniforms

disableNorm 1.
contrastPreMultiplicator 1.0
Offset "0. 0. 0. 0."
modulateColor ".2 .2 1. 1."
Contrast .5
Gamma 1
AngleDeg 0.0
SpaceConstant .2
