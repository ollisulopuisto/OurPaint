/*
* Our Paint: A light weight GPU powered painting program.
* Copyright (C) 2022-2023 Wu Yiming
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "ourpaint.h"

const char OUR_SHADER_VERSION_430[]="#version 430\n"
"#define WORKGROUP_SIZE 32\n";
const char OUR_SHADER_VERSION_320ES[]="#version 320 es\n"
"#define OUR_GLES\n#define WORKGROUP_SIZE 16\n";

const char OUR_SHADER_COMMON[]=R"(
#ifdef OUR_GLES

vec4 cunpack(uint d){
    return vec4(float(d&0xFFu)/255.,float((d>>8u)&0xFFu)/255.,float((d>>16u)&0xFFu)/255.,float((d>>24u)&0xFFu)/255.);
}
uvec4 cpack(vec4 c){
    uint v= uint(uint(c.r*255.) | (uint(c.g*255.)<<8u) | (uint(c.b*255.)<<16u) | (uint(c.a*255.)<<24u));
    return uvec4(v,v,v,v); 
}

#define OurImageLoad(img, p) \
    (cunpack(imageLoad(img,p).x))
#define OurImageStore(img, p, color) \
    imageStore(img,p,cpack(color))

#else

#define OurImageLoad(img, p) \
    (vec4(imageLoad(img,p))/65535.)
#define OurImageStore(img, p, color) \
    imageStore(img,p,uvec4(vec4(color)*65535.))

#endif
)";

const char OUR_CANVAS_SHADER[]=R"(
layout(local_size_x = WORKGROUP_SIZE, local_size_y = WORKGROUP_SIZE, local_size_z = 1) in;

#ifdef OUR_GLES
precision highp uimage2D;
precision highp float;
precision highp int;
layout(r32ui, binding = 0) uniform uimage2D img;
layout(r32ui, binding = 1) coherent uniform uimage2D smudge_buckets;
#define OUR_FLT_EPS (1.0/255.0f)
#else
layout(rgba16ui, binding = 0) uniform uimage2D img;
layout(rgba16ui, binding = 1) coherent uniform uimage2D smudge_buckets;
#define OUR_FLT_EPS (1e-4)
#endif

uniform int uCanvasType;
uniform int uCanvasRandom;
uniform float uCanvasFactor;
uniform ivec2 uImageOffset;
uniform ivec2 uBrushCorner;
uniform vec2 uBrushCenter;
uniform float uBrushSize;
uniform float uBrushHardness;
uniform float uBrushSmudge;
uniform float uBrushSlender;
uniform float uBrushAngle;
uniform vec2 uBrushDirection;
uniform float uBrushForce;
uniform float uBrushGunkyness;
uniform float uBrushRecentness;
uniform vec4 uBrushColor;
uniform vec4 uBackgroundColor;
uniform int uBrushErasing;
uniform int uBrushMix;

#ifdef OUR_GLES
uniform int uBrushRoutineSelectionES;
uniform int uMixRoutineSelectionES;
#endif

#ifdef OUR_CANVAS_MODE_PIGMENT
#with OUR_PIGMENT_COMMON
layout(std140) uniform BrushPigmentBlock{
    PigmentData p;
}uBrushPigment;
#endif

#with OUR_SHADER_COMMON

const vec4 p1_22=vec4(1.0/2.2,1.0/2.2,1.0/2.2,1.0/2.2);
const vec4 p22=vec4(2.2,2.2,2.2,2.2);
const float WGM_EPSILON=0.001f;
const float T_MATRIX_SMALL[30] = float[30](0.026595621243689,0.049779426257903,0.022449850859496,-0.218453689278271
,-0.256894883201278,0.445881722194840,0.772365886289756,0.194498761382537
,0.014038157587820,0.007687264480513

,-0.032601672674412,-0.061021043498478,-0.052490001018404
,0.206659098273522,0.572496335158169,0.317837248815438,-0.021216624031211
,-0.019387668756117,-0.001521339050858,-0.000835181622534

,0.339475473216284,0.635401374177222,0.771520797089589,0.113222640692379
,-0.055251113343776,-0.048222578468680,-0.012966666339586
,-0.001523814504223,-0.000094718948810,-0.000051604594741);

const float spectral_r_small[10] = float[10](0.009281362787953,0.009732627042016,0.011254252737167,0.015105578649573
,0.024797924177217,0.083622585502406,0.977865045723212,1.000000000000000
,0.999961046144372,0.999999992756822);
const float spectral_g_small[10] = float[10](0.002854127435775,0.003917589679914,0.012132151699187,0.748259205918013
,1.000000000000000,0.865695937531795,0.037477469241101,0.022816789725717
,0.021747419446456,0.021384940572308);
const float spectral_b_small[10] = float[10](0.537052150373386,0.546646402401469,0.575501819073983,0.258778829633924
,0.041709923751716,0.012662638828324,0.007485593127390,0.006766900622462
,0.006699764779016,0.006676219883241);
void rgb_to_spectral (vec3 rgb, out float spectral_[10]) {
    float offset = 1.0 - WGM_EPSILON;
    float r = rgb.r * offset + WGM_EPSILON;
    float g = rgb.g * offset + WGM_EPSILON;
    float b = rgb.b * offset + WGM_EPSILON;
    float spec_r[10] = float[10](0.,0.,0.,0.,0.,0.,0.,0.,0.,0.); for (int i=0; i < 10; i++) {spec_r[i] = spectral_r_small[i] * r;}
    float spec_g[10] = float[10](0.,0.,0.,0.,0.,0.,0.,0.,0.,0.); for (int i=0; i < 10; i++) {spec_g[i] = spectral_g_small[i] * g;}
    float spec_b[10] = float[10](0.,0.,0.,0.,0.,0.,0.,0.,0.,0.); for (int i=0; i < 10; i++) {spec_b[i] = spectral_b_small[i] * b;}
    for (int i=0; i<10; i++) {spectral_[i] = spec_r[i] + spec_g[i] + spec_b[i];}
}
vec3 spectral_to_rgb (float spectral[10]) {
    float offset = 1.0 - WGM_EPSILON;
    // We need this tmp. array to allow auto vectorization. <-- How about on GPU?
    float tmp[3] = float[3](0.,0.,0.);
    for (int i=0; i<10; i++) {
        tmp[0] += T_MATRIX_SMALL[i] * spectral[i];
        tmp[1] += T_MATRIX_SMALL[10+i] * spectral[i];
        tmp[2] += T_MATRIX_SMALL[20+i] * spectral[i];
    }
    vec3 rgb_;
    for (int i=0; i<3; i++) {rgb_[i] = clamp((tmp[i] - WGM_EPSILON) / offset, 0.0f, 1.0f);}
    return rgb_;
}

vec2 hash( vec2 p ){
	p = vec2( dot(p,vec2(127.1,311.7)), dot(p,vec2(269.5,183.3)) );
	return -1.0 + 2.0*fract(sin(p)*43758.5453123);
}
float rand(vec2 co){
    return fract(sin(dot(co, vec2(12.9898, 78.233))) * 43758.5453);
}
float noise(in vec2 p){ // from iq
    const float K1 = 0.366025404; // (sqrt(3)-1)/2;
    const float K2 = 0.211324865; // (3-sqrt(3))/6;
	vec2  i = floor( p + (p.x+p.y)*K1 );
    vec2  a = p - i + (i.x+i.y)*K2;
    float m = step(a.y,a.x);
    vec2  o = vec2(m,1.0-m);
    vec2  b = a - o + K2;
	vec2  c = a - 1.0 + 2.0*K2;
    vec3  h = max( 0.5-vec3(dot(a,a), dot(b,b), dot(c,c) ), 0.0 );
	vec3  n = h*h*h*h*vec3( dot(a,hash(i+0.0)), dot(b,hash(i+o)), dot(c,hash(i+1.0)));
    return dot( n, vec3(70.0) );
}

#define HEIGHT_STRAND(x,y) abs(fract(x)-.5)<.48? \
    (.4+.2*sin(3.14*(y+ceil(x))))* \
        ((max(abs(sin(3.14*x*2.)+0.2),abs(sin(3.14*x*2.)-0.2))+2.*abs(sin(3.14*x)))/2.+0.5):0.1
#define PATTERN_CANVAS(x,y) \
    (max(HEIGHT_STRAND((x),(y)),HEIGHT_STRAND(-(y),(x))))

float HEIGHT_CANVAS(float x,float y){
    if(uCanvasType == 1){
        return PATTERN_CANVAS(x,y);
    }else if(uCanvasType == 2){
        vec2 uv=vec2(x,y); float f; uv*=0.1; // from iq
		f = 0.2*noise( uv ); uv*=5.;
		f += 0.6*noise( uv ); uv*=3.;
		f += 0.5*noise( uv );
	    f = 0.55 + 0.55*f;
        return pow(f,0.5);
    }
    return 1.;
}
float SampleCanvas(vec2 U, vec2 dir,float rfac, float force, float gunky){
    if(uCanvasType==0 || abs(gunky)<1.e-2){ return rfac; }
    U+=vec2(uImageOffset); U/=20.3; U.x=U.x+rand(U)/10.; U.y=U.y+rand(U)/10.;

    mat2 m = mat2(1.6,1.2,-1.2,1.6); vec2 _uv=U; _uv.x+=float(uCanvasRandom%65535)/174.41; _uv.y+=float(uCanvasRandom%65535)/439.87; _uv/=500.;
    U.x+=noise(_uv)*2.1; _uv = m*_uv; U.x+=noise(_uv)*0.71;
    _uv.y+=365.404;
    U.y+=noise(_uv)*1.9; _uv = m*_uv; U.y+=noise(_uv)*0.83;
    
    float d=0.1;
    float h=HEIGHT_CANVAS(U.x,U.y);
    float hr=HEIGHT_CANVAS(U.x+d,U.y);
    float hu=HEIGHT_CANVAS(U.x,U.y+d);
    vec3 vx=normalize(vec3(d,0,hr)-vec3(0,0,h)),vy=normalize(vec3(0,d,hu)-vec3(0,0,h)),vz=cross(vx,vy);
    float useforce=force*rfac;
    float scrape=dot(normalize(vz),vec3(-normalize(dir).xy,0))*mix(0.3,1.,useforce);
    float top=h-(1.-pow(useforce,1.5)*2.); float tophard=smoothstep(0.4,0.6,top);
    float fac=(gunky>=0.)?mix(mix(1.,top,gunky),tophard,gunky):mix(1.,1.-h,-gunky*0.8);
    fac=max(fac,scrape*clamp(gunky,0.,1.));
    fac=clamp(fac,0.,1.);
    fac*=rfac;
    return mix(rfac,fac,uCanvasFactor);
}

#ifndef OUR_GLES
subroutine vec4 MixRoutines(vec4 a, vec4 b, float fac_a);
#endif

#ifndef OUR_GLES
subroutine(MixRoutines)
#endif
vec4 DoMixNormal(vec4 a, vec4 b, float fac_a){
    return mix(a,b,1.0f-fac_a);
}

#ifndef OUR_GLES
subroutine(MixRoutines)
#endif
vec4 DoMixSpectral(vec4 a, vec4 b, float fac_a){
    vec4 result = vec4(0,0,0,0);
    result.a=mix(a.a,b.a,1.0f-fac_a);
    float spec_a[10] = float[10](0.,0.,0.,0.,0.,0.,0.,0.,0.,0.); rgb_to_spectral(a.rgb, spec_a);
    float spec_b[10] = float[10](0.,0.,0.,0.,0.,0.,0.,0.,0.,0.); rgb_to_spectral(b.rgb, spec_b);
    float spectralmix[10] = float[10](0.,0.,0.,0.,0.,0.,0.,0.,0.,0.);
    for (int i=0; i < 10; i++) { spectralmix[i] = pow(spec_a[i], fac_a) * pow(spec_b[i], 1.0f-fac_a); }
    result.rgb=spectral_to_rgb(spectralmix);
    return result;
}

#ifdef OUR_GLES
vec4 uMixRoutineSelection(vec4 a, vec4 b, float fac_a){
    if(uMixRoutineSelectionES==0){ return DoMixNormal(a,b,fac_a); }
    else{ return DoMixSpectral(a,b,fac_a); }
}
#else
subroutine uniform MixRoutines uMixRoutineSelection;
#endif

vec4 spectral_mix(vec4 a, vec4 b, float fac_a){
    return uMixRoutineSelection(a,b,fac_a);
}
vec4 spectral_mix_always(vec4 colora, vec4 colorb, float fac){
#ifndef OUR_STRAIGHT_ALPHA
    vec4 ca=(colora.a==0.0f)?colora:vec4(colora.rgb/colora.a,colora.a);
    vec4 cb=(colorb.a==0.0f)?colorb:vec4(colorb.rgb/colorb.a,colorb.a);
#else
    vec4 ca=colora; vec4 cb=colorb;
#endif
    float af=colora.a*(1.0f-fac);
    float aa=af/(af+fac*colorb.a+0.000001);
    vec4 result=spectral_mix(ca,cb,aa);
    result.a=mix(colora.a,colorb.a,fac);
#ifndef OUR_STRAIGHT_ALPHA
    result = vec4(result.rgb*result.a,result.a);
#endif
    return result;
}
float atan2(in float y, in float x){
    bool s = (abs(x) > abs(y)); return mix(3.1415926535/2.0 - atan(x,y), atan(y,x), s);
}
vec2 rotate(vec2 v, float angle) {
    float s = sin(angle); float c = cos(angle);
    return mat2(c,-s,s,c) * v;
}
float brightness(vec4 color) {
    return color.r*0.2126+color.b*0.7152+color.g*0.0722;
}
vec4 mix_over(vec4 colora, vec4 colorb){
#ifndef OUR_STRAIGHT_ALPHA
    vec4 a=(colora.a==0.0f)?colora:vec4(colora.rgb/colora.a,colora.a);
    vec4 b=(colorb.a==0.0f)?colorb:vec4(colorb.rgb/colorb.a,colorb.a);
#else
    vec4 a=colora; vec4 b=colorb;
#endif
    vec4 m=vec4(0,0,0,0); float aa=colora.a/(colora.a+(1.0f-colora.a)*colorb.a+OUR_FLT_EPS);
    m=spectral_mix(a,b,aa);
    m.a=colora.a+colorb.a*(1.0f-colora.a);
#ifndef OUR_STRAIGHT_ALPHA
    m=vec4(m.rgb*m.a,m.a);
#endif
    return m;
}
int dab(float d, vec2 fpx, vec4 color, float size, float hardness, float smudge, vec4 smudge_color, vec4 last_color, out vec4 final){
    vec4 cc=color;
    float fac=1.0f-pow(d/size,1.0f+1.0f/(1.0f-hardness+OUR_FLT_EPS));
    float canvas=SampleCanvas(fpx,uBrushDirection,fac,uBrushForce,uBrushGunkyness);
    cc.a=color.a*canvas*(1.0f-smudge);
#ifndef OUR_STRAIGHT_ALPHA
    cc.rgb=cc.rgb*cc.a;
#endif
    float erasing=float(uBrushErasing);
    cc=cc*(1.0f-erasing);

    // this looks better than the one commented out below
    vec4 c2=spectral_mix_always(last_color,smudge_color,smudge*fac*color.a*canvas);
    c2=mix_over(cc,c2);
    //vec4 c2=mix_over(cc,last_color);
    //c2=spectral_mix_always(c2,smudge_color,smudge*fac*color.a*canvas);

    c2=spectral_mix_always(c2,c2*(1.0f-fac*color.a),erasing*canvas);
    final=c2;
    return 1;
}

#ifndef saturate
#define saturate(v) clamp(v, 0., 1.)
#endif
const float HCV_EPSILON = 1e-10;
const float HCY_EPSILON = 1e-10;
vec3 hue_to_rgb(float hue){
    float R = abs(hue * 6. - 3.) - 1.;
    float G = 2. - abs(hue * 6. - 2.);
    float B = 2. - abs(hue * 6. - 4.);
    return saturate(vec3(R,G,B));
}
vec3 hcy_to_rgb(vec3 hcy){
    const vec3 HCYwts = vec3(0.299, 0.587, 0.114);
    vec3 RGB = hue_to_rgb(hcy.x);
    float Z = dot(RGB, HCYwts);
    if (hcy.z < Z) { hcy.y *= hcy.z / Z; }
    else if (Z < 1.) { hcy.y *= (1. - hcy.z) / (1. - Z); }
    return (RGB - Z) * hcy.y + hcy.z;
}
vec3 rgb_to_hcv(vec3 rgb){
    // Based on work by Sam Hocevar and Emil Persson
    vec4 P = (rgb.g < rgb.b) ? vec4(rgb.bg, -1.0, 2.0/3.0) : vec4(rgb.gb, 0.0, -1.0/3.0);
    vec4 Q = (rgb.r < P.x) ? vec4(P.xyw, rgb.r) : vec4(rgb.r, P.yzx);
    float C = Q.x - min(Q.w, Q.y);
    float H = abs((Q.w - Q.y) / (6. * C + HCV_EPSILON) + Q.z);
    return vec3(H, C, Q.x);
}
vec3 rgb_to_hcy(vec3 rgb){
    const vec3 HCYwts = vec3(0.299, 0.587, 0.114);
    // Corrected by David Schaeffer
    vec3 HCV = rgb_to_hcv(rgb);
    float Y = dot(rgb, HCYwts);
    float Z = dot(hue_to_rgb(HCV.x), HCYwts);
    if (Y < Z) { HCV.y *= Z / (HCY_EPSILON + Y); }
    else { HCV.y *= (1. - Z) / (HCY_EPSILON + 1. - Y); }
    return vec3(HCV.x, HCV.y, Y);
}

#ifndef OUR_GLES
subroutine void BrushRoutines();
#endif

#ifdef OUR_CANVAS_MODE_RGB

#ifndef OUR_GLES
subroutine(BrushRoutines)
#endif
void DoDabs(){
    ivec2 px = ivec2(gl_GlobalInvocationID.xy)+uBrushCorner;
    if(px.x<0||px.y<0||px.x>1024||px.y>1024) return;
    vec2 fpx=vec2(px),origfpx=fpx;
    fpx=uBrushCenter+rotate(fpx-uBrushCenter,uBrushAngle);
    fpx.x=uBrushCenter.x+(fpx.x-uBrushCenter.x)*(1.+uBrushSlender);
    float dd=distance(fpx,uBrushCenter); if(dd>uBrushSize) return;
    vec4 dabc=OurImageLoad(img, px);
    vec4 smudgec=pow(spectral_mix_always(pow(OurImageLoad(smudge_buckets,ivec2(1,0)),p1_22),pow(OurImageLoad(smudge_buckets,ivec2(0,0)),p1_22),uBrushRecentness),p22);
    vec4 final_color;
    dab(dd,origfpx,uBrushColor,uBrushSize,uBrushHardness,uBrushSmudge,smudgec,dabc,final_color);
    if(final_color.a>0.){
        if(uBrushMix==0){ dabc=final_color; }
        else if(uBrushMix==1){ dabc.rgb=final_color.rgb/final_color.a*dabc.a;}
        else if(uBrushMix==2){ vec3 xyz=rgb_to_hcy(dabc.rgb); xyz.xy=rgb_to_hcy(final_color.rgb).xy; dabc.rgb=hcy_to_rgb(xyz); }
        else if(uBrushMix==3){ dabc.rgb=dabc.rgb+final_color.rgb*0.01;dabc.a=dabc.a*0.99+final_color.a*0.01; }
        OurImageStore(img, px, dabc);
    }
}

#ifndef OUR_GLES
subroutine(BrushRoutines)
#endif
void DoSample(){
    ivec2 p=ivec2(gl_GlobalInvocationID.xy);
    int DoSample=1; vec4 color;
    if(p.y==0){
        vec2 sp=round(vec2(sin(float(p.x)),cos(float(p.x)))*uBrushSize);
        ivec2 px=ivec2(sp)+uBrushCorner; if(px.x<0||px.y<0||px.x>=1024||px.y>=1024){ DoSample=0; }
        if(DoSample!=0){
            ivec2 b=uBrushCorner; if(b.x>=0&&b.y>=0&&b.x<1024&&b.y<1024){ OurImageStore(smudge_buckets,ivec2(128+WORKGROUP_SIZE,0),OurImageLoad(img, b)); }
            color=OurImageLoad(img, px);
            OurImageStore(smudge_buckets,ivec2(p.x+128,0),color);
        }
    }else{DoSample=0;}
    memoryBarrier();barrier(); if(DoSample==0) return;
    if(uBrushErasing==0 || p.x!=0) return;
    color=vec4(0.,0.,0.,0.); for(int i=0;i<WORKGROUP_SIZE;i++){ color=color+OurImageLoad(smudge_buckets, ivec2(i+128,0)); }
    color=spectral_mix_always(color/vec4(WORKGROUP_SIZE),OurImageLoad(smudge_buckets, ivec2(128+WORKGROUP_SIZE,0)),0.6*(1.0f-uBrushColor.a)); vec4 oldcolor=OurImageLoad(smudge_buckets, ivec2(0,0));
    OurImageStore(smudge_buckets,ivec2(1,0),uBrushErasing==2?color:oldcolor);
    OurImageStore(smudge_buckets,ivec2(0,0),color);
}

#endif // canvas mode rgb
)"
R"(
#ifdef OUR_CANVAS_MODE_PIGMENT //========================================================================================

int dab_pigment(float d, vec2 fpx, PigmentData color, float size, float hardness,
                float smudge, PigmentData smudge_color, PigmentData last_color, out PigmentData final){
    PigmentData cc=(uBrushErasing!=0)?PIGMENT_BLANK:color;
    float erasing=float(uBrushErasing);
    float fac=1.0f-safepow(d/size,1.0f+1.0f/(1.0f-hardness+OUR_FLT_EPS));
    float canvas=SampleCanvas(fpx,uBrushDirection,fac,uBrushForce,uBrushGunkyness);

    if(uBrushErasing!=0){
        PigmentData smudged_color=PigmentMix(last_color,smudge_color,smudge*fac*canvas);
        final=PigmentMix(smudged_color,PIGMENT_BLANK,erasing*canvas*fac);
    }else{
        float usefac=canvas*fac*(1.-smudge);
        cc.a[15]=color.a[15]*usefac;
        cc.r[15]=color.r[15]*usefac;
        PigmentData smudged_color=PigmentMix(last_color,smudge_color,smudge*fac*canvas);
        PigmentData added_color=PigmentOver(cc,smudged_color);
        final=added_color;
    }
    return 1;
}

#ifndef OUR_GLES
subroutine(BrushRoutines)
#endif
void DoDabs(){
    ivec2 px = ivec2(gl_GlobalInvocationID.xy)*2+uBrushCorner; px/=2; px*=2;

    if(px.x<0||px.y<0||px.x>=1024||px.y>=1024) return; vec2 fpx=vec2(px),origfpx=fpx;
    fpx=uBrushCenter+rotate(fpx-uBrushCenter,uBrushAngle);
    fpx.x=uBrushCenter.x+(fpx.x-uBrushCenter.x)*(1.+uBrushSlender);
    float dd=distance(fpx,uBrushCenter); if(dd>uBrushSize) return;

    PigmentData dabc;   GetImgPixel(img, px, dabc);
    PigmentData sm_old; ivec2 oldvec=ivec2(2,0); GetImgPixel(smudge_buckets,oldvec,sm_old);
    PigmentData sm_new; ivec2 newvec=ivec2(0,0); GetImgPixel(smudge_buckets,newvec,sm_new);
    PigmentData smudgec=PigmentMix(sm_old,sm_new,uBrushRecentness);
    PigmentData final_color;
    dab_pigment(dd,origfpx,uBrushPigment.p,uBrushSize,uBrushHardness,uBrushSmudge,smudgec,dabc,final_color);
    if(final_color.a[15]>0. || final_color.r[15]>0.){
        WriteImgPixel(img, px, final_color);
    }
}

#ifndef OUR_GLES
subroutine(BrushRoutines)
#endif
void DoSample(){
    ivec2 p=ivec2(gl_GlobalInvocationID.xy);
    int DoSample=1; ivec2 corner=ivec2(uBrushCenter);
    if(p.y==0){
        vec2 sp=round(vec2(sin(float(p.x)),cos(float(p.x)))*(uBrushSize+2.));
        ivec2 px=ivec2(sp)+corner; px/=2; px*=2; if(px.x<0||px.y<0||px.x>=1024||px.y>=1024){ DoSample=0; }
        if(DoSample!=0){
            PigmentData dabc; GetImgPixel(img, px, dabc);
            WriteImgPixel(smudge_buckets,ivec2(p.x*2+128,0),dabc);
        }
    }else{DoSample=0;}
    memoryBarrier();barrier(); if(DoSample==0) return;
    if(uBrushErasing==0 || p.x!=0) return;
    PigmentData color=PIGMENT_BLANK; for(int i=0;i<WORKGROUP_SIZE;i++){
        PigmentData dabc; GetImgPixel(smudge_buckets, ivec2(i*2+128,0), dabc); color=PigmentMix(color,dabc,1.0/(float(i)+1.));
    }
    PigmentData oldcolor; GetImgPixel(smudge_buckets, ivec2(0,0), oldcolor);
    //PigmentMultiply(color,2./WORKGROUP_SIZE);
    WriteImgPixel(smudge_buckets,ivec2(2,0),uBrushErasing==2?color:oldcolor);
    WriteImgPixel(smudge_buckets,ivec2(0,0),color);
}

#endif // canvas mode pigment

#ifdef OUR_GLES
void uBrushRoutineSelection(){
    if(uBrushRoutineSelectionES==0){ DoDabs(); }
    else{ DoSample(); }
}
#else
subroutine uniform BrushRoutines uBrushRoutineSelection;
#endif

void main() {
    uBrushRoutineSelection();
}
)";

const char OUR_COMPOSITION_SHADER[] = R"(
layout(local_size_x = WORKGROUP_SIZE, local_size_y = WORKGROUP_SIZE, local_size_z = 1) in;
#ifdef OUR_GLES
precision highp uimage2D;
precision highp float;
precision highp int;
layout(r32ui, binding = 0) uniform uimage2D top;
layout(r32ui, binding = 1) uniform uimage2D bottom;
#else
layout(rgba16ui, binding = 0) uniform uimage2D top;
layout(rgba16ui, binding = 1) uniform uimage2D bottom;
#endif
uniform int uBlendMode;
uniform float uAlphaTop;
uniform float uAlphaBottom;

#with OUR_SHADER_COMMON

vec4 mix_over(vec4 colora, vec4 colorb){
#ifdef OUR_STRAIGHT_ALPHA
    colora=vec4(colora.rgb*colora.a,colora.a);
    colorb=vec4(colorb.rgb*colorb.a,colorb.a);
#endif
    colora=colora*uAlphaTop/uAlphaBottom;
    vec4 c; c.a=colora.a+colorb.a*(1.0f-colora.a);
    c.rgb=(colora.rgb+colorb.rgb*(1.0f-colora.a));
#ifdef OUR_STRAIGHT_ALPHA
    c=(c.a!=0.)?vec4(c.rgb/c.a,c.a):vec4(0.,0.,0.,0.);
#endif
    return c;
}

vec4 add_over(vec4 colora, vec4 colorb){
#ifdef OUR_STRAIGHT_ALPHA
    colora=vec4(colora.rgb*colora.a,colora.a);
    colorb=vec4(colorb.rgb*colorb.a,colorb.a);
#endif
    colora=colora*uAlphaTop/uAlphaBottom;
    vec4 result=colora+colorb; result.a=clamp(result.a,0.,1.);
#ifdef OUR_STRAIGHT_ALPHA
    result=result.a!=0.?vec4(result.rgb/result.a,result.a):vec4(0.,0.,0.,0.);
#endif
    return result;
}
void main() {
    ivec2 px=ivec2(gl_GlobalInvocationID.xy);
    vec4 c1=OurImageLoad(top,px); vec4 c2=OurImageLoad(bottom,px);
    vec4 c=(uBlendMode==0)?mix_over(c1,c2):add_over(c1,c2);
    OurImageStore(bottom,px,c);
    OurImageStore(top,px,vec4(1.));
}
)";

const char OUR_PIGMENT_COMMON[]=R"(
#define POW_EPS (1.0e-9)
#define USE_SAFE_POW 1

#if USE_SAFE_POW
float safepow(float a, float b){ a=clamp(a,POW_EPS,1.-POW_EPS); //b=clamp(b,POW_EPS,1.-POW_EPS);
    return pow(a,b);
}
#else
#define safepow pow
#endif

#define OUR_SPECTRAL_SLICES 14

struct PigmentData{ float r[16]; float a[16]; };

const PigmentData PIGMENT_BLANK=
    PigmentData(float[16](0.,0.,0.,0.,0.,0.,0.,0.,0.,0.,0.,0.,0.,0.,0.,0.),float[16](0.,0.,0.,0.,0.,0.,0.,0.,0.,0.,0.,0.,0.,0.,0.,0.));
const PigmentData PIGMENT_WHITE=
    PigmentData(float[16](1.,1.,1.,1.,1.,1.,1.,1.,1.,1.,1.,1.,1.,1.,1.,1.),float[16](0.,0.,0.,0.,0.,0.,0.,0.,0.,0.,0.,0.,0.,0.,0.,0.));
const PigmentData PIGMENT_BLACK=
    PigmentData(float[16](0.,0.,0.,0.,0.,0.,0.,0.,0.,0.,0.,0.,0.,0.,0.,1.),float[16](0.,0.,0.,0.,0.,0.,0.,0.,0.,0.,0.,0.,0.,0.,0.,0.));

#ifdef OUR_GLES

#define PixType uint
#define PREC_FIX (0.0/15.)
#define fetchpix(tex,uv,level) texelFetch(tex,uv,level).x
#define loadpix(tex,uv) imageLoad(tex,uv).x
#define packpix(c) uvec4(c)
#define l8f(a) (float((uint(a)&0x0fu)>>0)/15.)
#define h8f(a) (float((uint(a)&0xf0u)>>4)/15.)
#define lh16f(a)  (float(a)/255.)
#define fl16(l,h) (clamp((uint((l+PREC_FIX)*15.)),0u,15u)|(clamp((uint((h+PREC_FIX)*15.)),0u,15u)<<4))
#define fl16w(a)  (uint(a*255.))
uvec4 pixunpack(PixType c_){
    return uvec4((uint(c_)&0xffu),(uint(c_>>8)&0xffu),(uint(c_>>16)&0xffu),(uint(c_>>24)&0xffu));
}
PixType pixpack(uvec4 c){
    return uint(((c[0])&0xffu)|(((c[1])&0xffu)<<8)|(((c[2])&0xffu)<<16)|(((c[3])&0xffu)<<24));
}

void setRL(PixType c_, inout PigmentData p){
    uvec4 c=pixunpack(c_);
    p.r[0]=l8f(c[0]); p.r[1]=h8f(c[0]); p.r[2]=l8f(c[1]); p.r[3]=h8f(c[1]);
    p.r[4]=l8f(c[2]); p.r[5]=h8f(c[2]); p.r[6]=l8f(c[3]); p.r[7]=h8f(c[3]);
}
void setRH(PixType c_, inout PigmentData p){
    uvec4 c=pixunpack(c_);
    p.r[8]= l8f(c[0]); p.r[9] =h8f(c[0]); p.r[10]=l8f(c[1]); p.r[11]=h8f(c[1]);
    p.r[12]=l8f(c[2]); p.r[13]=h8f(c[2]); p.r[14]=0.; p.r[15]=lh16f(c[3]); //p.r[14]=l8f(c[3]); p.r[15]=h8f(c[3]);
}
void setAL(PixType c_, inout PigmentData p){
    uvec4 c=pixunpack(c_);
    p.a[0]=l8f(c[0]); p.a[1]=h8f(c[0]); p.a[2]=l8f(c[1]); p.a[3]=h8f(c[1]);
    p.a[4]=l8f(c[2]); p.a[5]=h8f(c[2]); p.a[6]=l8f(c[3]); p.a[7]=h8f(c[3]);
}
void setAH(PixType c_, inout PigmentData p){
    uvec4 c=pixunpack(c_);
    p.a[8]= l8f(c[0]); p.a[9] =h8f(c[0]); p.a[10]=l8f(c[1]); p.a[11]=h8f(c[1]);
    p.a[12]=l8f(c[2]); p.a[13]=h8f(c[2]); p.a[14]=0.; p.a[15]=lh16f(c[3]); //p.a[14]=l8f(c[3]); p.a[15]=h8f(c[3]);
}
PixType getRL(PigmentData p){ uvec4 c;
    c[0]=fl16(p.r[0],p.r[1]); c[1]=fl16(p.r[2],p.r[3]);
    c[2]=fl16(p.r[4],p.r[5]); c[3]=fl16(p.r[6],p.r[7]); return pixpack(c);
}
PixType getRH(PigmentData p){ uvec4 c;
    c[0]=fl16(p.r[8],p.r[9]); c[1]=fl16(p.r[10],p.r[11]);
    c[2]=fl16(p.r[12],p.r[13]); c[3]=fl16w(p.r[15]); //c[3]=fl16(p.r[14],p.r[15]);
    return pixpack(c);
}
PixType getAL(PigmentData p){ uvec4 c;
    c[0]=fl16(p.a[0],p.a[1]); c[1]=fl16(p.a[2],p.a[3]);
    c[2]=fl16(p.a[4],p.a[5]); c[3]=fl16(p.a[6],p.a[7]); return pixpack(c);
}
PixType getAH(PigmentData p){ uvec4 c;
    c[0]=fl16(p.a[8],p.a[9]); c[1]=fl16(p.a[10],p.a[11]);
    c[2]=fl16(p.a[12],p.a[13]); c[3]=fl16w(p.a[15]); //c[3]=fl16(p.a[14],p.a[15]);
    return pixpack(c);
}

PixType PixelAvg2(PixType a_, PixType b_){
    uvec4 a=pixunpack(a_); uvec4 b=pixunpack(b_);
    uvec4 r;
    r[0]=(((a[0]&0xffu)+(b[0]&0xffu))/2u)|((((a[0]&0xff00u)+(b[0]&0xff00u))/2u)&0xff00u);
    r[1]=(((a[1]&0xffu)+(b[1]&0xffu))/2u)|((((a[1]&0xff00u)+(b[1]&0xff00u))/2u)&0xff00u);
    r[2]=(((a[2]&0xffu)+(b[2]&0xffu))/2u)|((((a[2]&0xff00u)+(b[2]&0xff00u))/2u)&0xff00u);
    r[3]=(((a[3]&0xffu)+(b[3]&0xffu))/2u)|((((a[3]&0xff00u)+(b[3]&0xff00u))/2u)&0xff00u);
    return pixpack(r);
}
PixType PixelAvg2H(PixType a_, PixType b_){
    uvec4 a=pixunpack(a_); uvec4 b=pixunpack(b_);
    uvec4 r;
    r[0]=(((a[0]&0xffu)+(b[0]&0xffu))/2u)|((((a[0]&0xff00u)+(b[0]&0xff00u))/2u)&0xff00u);
    r[1]=(((a[1]&0xffu)+(b[1]&0xffu))/2u)|((((a[1]&0xff00u)+(b[1]&0xff00u))/2u)&0xff00u);
    r[2]=(((a[2]&0xffu)+(b[2]&0xffu))/2u)|((((a[2]&0xff00u)+(b[2]&0xff00u))/2u)&0xff00u);
    r[3]=(a[3]+b[3])/2u;
    return pixpack(r);
}
PixType PixelAvg4(PixType a_, PixType b_, PixType c_, PixType d_){
    uvec4 a=pixunpack(a_); uvec4 b=pixunpack(b_);
    uvec4 c=pixunpack(c_); uvec4 d=pixunpack(d_);
    uvec4 r;
    r[0]=(((a[0]&0xffu)+(b[0]&0xffu)+(c[0]&0xffu)+(d[0]&0xffu))/4u)|((((a[0]&0xff00u)+(b[0]&0xff00u)+(c[0]&0xff00u)+(d[0]&0xff00u))/4u)&0xff00u);
    r[1]=(((a[1]&0xffu)+(b[1]&0xffu)+(c[1]&0xffu)+(d[1]&0xffu))/4u)|((((a[1]&0xff00u)+(b[1]&0xff00u)+(c[1]&0xff00u)+(d[1]&0xff00u))/4u)&0xff00u);
    r[2]=(((a[2]&0xffu)+(b[2]&0xffu)+(c[2]&0xffu)+(d[2]&0xffu))/4u)|((((a[2]&0xff00u)+(b[2]&0xff00u)+(c[2]&0xff00u)+(d[2]&0xff00u))/4u)&0xff00u);
    r[3]=(((a[3]&0xffu)+(b[3]&0xffu)+(c[3]&0xffu)+(d[3]&0xffu))/4u)|((((a[3]&0xff00u)+(b[3]&0xff00u)+(c[3]&0xff00u)+(d[3]&0xff00u))/4u)&0xff00u);
    return pixpack(r);
}
PixType PixelAvg4H(PixType a_, PixType b_, PixType c_, PixType d_){
    uvec4 a=pixunpack(a_); uvec4 b=pixunpack(b_);
    uvec4 c=pixunpack(c_); uvec4 d=pixunpack(d_);
    uvec4 r;
    r[0]=(((a[0]&0xffu)+(b[0]&0xffu)+(c[0]&0xffu)+(d[0]&0xffu))/4u)|((((a[0]&0xff00u)+(b[0]&0xff00u)+(c[0]&0xff00u)+(d[0]&0xff00u))/4u)&0xff00u);
    r[1]=(((a[1]&0xffu)+(b[1]&0xffu)+(c[1]&0xffu)+(d[1]&0xffu))/4u)|((((a[1]&0xff00u)+(b[1]&0xff00u)+(c[1]&0xff00u)+(d[1]&0xff00u))/4u)&0xff00u);
    r[2]=(((a[2]&0xffu)+(b[2]&0xffu)+(c[2]&0xffu)+(d[2]&0xffu))/4u)|((((a[2]&0xff00u)+(b[2]&0xff00u)+(c[2]&0xff00u)+(d[2]&0xff00u))/4u)&0xff00u);
    r[3]=(a[3]+b[3]+c[3]+d[3])/4u;
    return pixpack(r);
}

#else // gles / desktop gl

#define PixType uvec4
#define PREC_FIX (0.25/255.)
#define fetchpix texelFetch
#define packpix(c) c
#define loadpix imageLoad
#define l8f(a) (float(((a)&0x00ffu)>>0)/255.)
#define h8f(a) (float(((a)&0xff00u)>>8)/255.)
#define lh16f(a)  (float(a)/65535.)
#define fl16(l,h) (clamp((uint((l+PREC_FIX)*255.)),0u,255u)|(clamp((uint((h+PREC_FIX)*255.)),0u,255u)<<8))
#define fl16w(a)  (uint(a*65535.))

void setRL(uvec4 c, inout PigmentData p){
    p.r[0]=l8f(c[0]); p.r[1]=h8f(c[0]); p.r[2]=l8f(c[1]); p.r[3]=h8f(c[1]);
    p.r[4]=l8f(c[2]); p.r[5]=h8f(c[2]); p.r[6]=l8f(c[3]); p.r[7]=h8f(c[3]);
}
void setRH(uvec4 c, inout PigmentData p){
    p.r[8]= l8f(c[0]); p.r[9] =h8f(c[0]); p.r[10]=l8f(c[1]); p.r[11]=h8f(c[1]);
    p.r[12]=l8f(c[2]); p.r[13]=h8f(c[2]); p.r[14]=0.; p.r[15]=lh16f(c[3]); //p.r[14]=l8f(c[3]); p.r[15]=h8f(c[3]);
}
void setAL(uvec4 c, inout PigmentData p){
    p.a[0]=l8f(c[0]); p.a[1]=h8f(c[0]); p.a[2]=l8f(c[1]); p.a[3]=h8f(c[1]);
    p.a[4]=l8f(c[2]); p.a[5]=h8f(c[2]); p.a[6]=l8f(c[3]); p.a[7]=h8f(c[3]);
}
void setAH(uvec4 c, inout PigmentData p){
    p.a[8]= l8f(c[0]); p.a[9] =h8f(c[0]); p.a[10]=l8f(c[1]); p.a[11]=h8f(c[1]);
    p.a[12]=l8f(c[2]); p.a[13]=h8f(c[2]); p.a[14]=0.; p.a[15]=lh16f(c[3]); //p.a[14]=l8f(c[3]); p.a[15]=h8f(c[3]);
}
uvec4 getRL(PigmentData p){ uvec4 c;
    c[0]=fl16(p.r[0],p.r[1]); c[1]=fl16(p.r[2],p.r[3]);
    c[2]=fl16(p.r[4],p.r[5]); c[3]=fl16(p.r[6],p.r[7]); return c;
}
uvec4 getRH(PigmentData p){ uvec4 c;
    c[0]=fl16(p.r[8],p.r[9]); c[1]=fl16(p.r[10],p.r[11]);
    c[2]=fl16(p.r[12],p.r[13]); c[3]=fl16w(p.r[15]); //c[3]=fl16(p.r[14],p.r[15]);
    return c;
}
uvec4 getAL(PigmentData p){ uvec4 c;
    c[0]=fl16(p.a[0],p.a[1]); c[1]=fl16(p.a[2],p.a[3]);
    c[2]=fl16(p.a[4],p.a[5]); c[3]=fl16(p.a[6],p.a[7]); return c;
}
uvec4 getAH(PigmentData p){ uvec4 c;
    c[0]=fl16(p.a[8],p.a[9]); c[1]=fl16(p.a[10],p.a[11]);
    c[2]=fl16(p.a[12],p.a[13]); c[3]=fl16w(p.a[15]); //c[3]=fl16(p.a[14],p.a[15]);
    return c;
}

PixType PixelAvg2(PixType a, PixType b){
    PixType r;
    r[0]=(((a[0]&0xffu)+(b[0]&0xffu))/2u)|((((a[0]&0xff00u)+(b[0]&0xff00u))/2u)&0xff00u);
    r[1]=(((a[1]&0xffu)+(b[1]&0xffu))/2u)|((((a[1]&0xff00u)+(b[1]&0xff00u))/2u)&0xff00u);
    r[2]=(((a[2]&0xffu)+(b[2]&0xffu))/2u)|((((a[2]&0xff00u)+(b[2]&0xff00u))/2u)&0xff00u);
    r[3]=(((a[3]&0xffu)+(b[3]&0xffu))/2u)|((((a[3]&0xff00u)+(b[3]&0xff00u))/2u)&0xff00u);
    return r;
}
PixType PixelAvg2H(PixType a, PixType b){
    PixType r;
    r[0]=(((a[0]&0xffu)+(b[0]&0xffu))/2u)|((((a[0]&0xff00u)+(b[0]&0xff00u))/2u)&0xff00u);
    r[1]=(((a[1]&0xffu)+(b[1]&0xffu))/2u)|((((a[1]&0xff00u)+(b[1]&0xff00u))/2u)&0xff00u);
    r[2]=(((a[2]&0xffu)+(b[2]&0xffu))/2u)|((((a[2]&0xff00u)+(b[2]&0xff00u))/2u)&0xff00u);
    r[3]=(a[3]+b[3])/2u;
    return r;
}
PixType PixelAvg4(PixType a, PixType b, PixType c, PixType d){
    uvec4 r;
    r[0]=(((a[0]&0xffu)+(b[0]&0xffu)+(c[0]&0xffu)+(d[0]&0xffu))/4u)|((((a[0]&0xff00u)+(b[0]&0xff00u)+(c[0]&0xff00u)+(d[0]&0xff00u))/4u)&0xff00u);
    r[1]=(((a[1]&0xffu)+(b[1]&0xffu)+(c[1]&0xffu)+(d[1]&0xffu))/4u)|((((a[1]&0xff00u)+(b[1]&0xff00u)+(c[1]&0xff00u)+(d[1]&0xff00u))/4u)&0xff00u);
    r[2]=(((a[2]&0xffu)+(b[2]&0xffu)+(c[2]&0xffu)+(d[2]&0xffu))/4u)|((((a[2]&0xff00u)+(b[2]&0xff00u)+(c[2]&0xff00u)+(d[2]&0xff00u))/4u)&0xff00u);
    r[3]=(((a[3]&0xffu)+(b[3]&0xffu)+(c[3]&0xffu)+(d[3]&0xffu))/4u)|((((a[3]&0xff00u)+(b[3]&0xff00u)+(c[3]&0xff00u)+(d[3]&0xff00u))/4u)&0xff00u);
    return r;
}
PixType PixelAvg4H(PixType a, PixType b, PixType c, PixType d){
    uvec4 r;
    r[0]=(((a[0]&0xffu)+(b[0]&0xffu)+(c[0]&0xffu)+(d[0]&0xffu))/4u)|((((a[0]&0xff00u)+(b[0]&0xff00u)+(c[0]&0xff00u)+(d[0]&0xff00u))/4u)&0xff00u);
    r[1]=(((a[1]&0xffu)+(b[1]&0xffu)+(c[1]&0xffu)+(d[1]&0xffu))/4u)|((((a[1]&0xff00u)+(b[1]&0xff00u)+(c[1]&0xff00u)+(d[1]&0xff00u))/4u)&0xff00u);
    r[2]=(((a[2]&0xffu)+(b[2]&0xffu)+(c[2]&0xffu)+(d[2]&0xffu))/4u)|((((a[2]&0xff00u)+(b[2]&0xff00u)+(c[2]&0xff00u)+(d[2]&0xff00u))/4u)&0xff00u);
    r[3]=(a[3]+b[3]+c[3]+d[3])/4u;
    return r;
}

#endif // desktop gl

PixType GetSubPixelH2(highp usampler2D tex, ivec2 uv, int offset){
    if(uv.x>=textureSize(tex,0).x-offset) return fetchpix(tex,ivec2(uv.x-offset,uv.y),0);
    if(uv.x<=offset) return fetchpix(tex,ivec2(uv.x+offset,uv.y),0);
    PixType a=fetchpix(tex,ivec2(uv.x-offset,uv.y),0);
    PixType b=fetchpix(tex,ivec2(uv.x+offset,uv.y),0);
    return PixelAvg2(a,b);
}
PixType GetSubPixelH2H(highp usampler2D tex, ivec2 uv, int offset){
    if(uv.x>=textureSize(tex,0).x-offset) return fetchpix(tex,ivec2(uv.x-offset,uv.y),0);
    if(uv.x<=offset) return fetchpix(tex,ivec2(uv.x+offset,uv.y),0);
    PixType a=fetchpix(tex,ivec2(uv.x-offset,uv.y),0);
    PixType b=fetchpix(tex,ivec2(uv.x+offset,uv.y),0);
    return PixelAvg2H(a,b);
}
PixType GetSubPixelV2(highp usampler2D tex, ivec2 uv, int offset){
    if(uv.y>=textureSize(tex,0).y-offset) return fetchpix(tex,ivec2(uv.x,uv.y-offset),0);
    if(uv.y<=offset) return fetchpix(tex,ivec2(uv.x,uv.y+offset),0);
    PixType a=fetchpix(tex,ivec2(uv.x,uv.y-offset),0);
    PixType b=fetchpix(tex,ivec2(uv.x,uv.y+offset),0);
    return PixelAvg2(a,b);
}
PixType GetSubPixelV2H(highp usampler2D tex, ivec2 uv, int offset){
    if(uv.y>=textureSize(tex,0).y-offset) return fetchpix(tex,ivec2(uv.x,uv.y-offset),0);
    if(uv.y<=offset) return fetchpix(tex,ivec2(uv.x,uv.y+offset),0);
    PixType a=fetchpix(tex,ivec2(uv.x,uv.y-offset),0);
    PixType b=fetchpix(tex,ivec2(uv.x,uv.y+offset),0);
    return PixelAvg2H(a,b);
}
PixType GetSubPixelX4(highp usampler2D tex, ivec2 uv, int offset){
    if(uv.x>=textureSize(tex,0).x-offset) return GetSubPixelV2(tex,ivec2(uv.x-offset,uv.y),offset);
    if(uv.y>=textureSize(tex,0).y-offset) return GetSubPixelH2(tex,ivec2(uv.x,uv.y-offset),offset);
    if(uv.x<=offset) return GetSubPixelV2(tex,ivec2(uv.x+offset,uv.y),offset);
    if(uv.y<=offset) return GetSubPixelH2(tex,ivec2(uv.x,uv.y+offset),offset);
    PixType a=fetchpix(tex,ivec2(uv.x-offset,uv.y-offset),0);
    PixType b=fetchpix(tex,ivec2(uv.x-offset,uv.y+offset),0);
    PixType c=fetchpix(tex,ivec2(uv.x+offset,uv.y-offset),0);
    PixType d=fetchpix(tex,ivec2(uv.x+offset,uv.y+offset),0);
    return PixelAvg4(a,b,c,d);
}
PixType GetSubPixelX4H(highp usampler2D tex, ivec2 uv, int offset){
    if(uv.x>=textureSize(tex,0).x-offset) return GetSubPixelV2(tex,ivec2(uv.x-offset,uv.y),offset);
    if(uv.y>=textureSize(tex,0).y-offset) return GetSubPixelH2(tex,ivec2(uv.x,uv.y-offset),offset);
    if(uv.x<=offset) return GetSubPixelV2(tex,ivec2(uv.x+offset,uv.y),offset);
    if(uv.y<=offset) return GetSubPixelH2(tex,ivec2(uv.x,uv.y+offset),offset);
    PixType a=fetchpix(tex,ivec2(uv.x-offset,uv.y-offset),0);
    PixType b=fetchpix(tex,ivec2(uv.x-offset,uv.y+offset),0);
    PixType c=fetchpix(tex,ivec2(uv.x+offset,uv.y-offset),0);
    PixType d=fetchpix(tex,ivec2(uv.x+offset,uv.y+offset),0);
    return PixelAvg4H(a,b,c,d);
}
PigmentData GetPixelDebayer(highp usampler2D tex, ivec2 uv, int offset){
    PixType c[4]; int s=(uv.x%2)*2+uv.y%2;
    if(s==0){
        c[0]=fetchpix(tex,uv,0); 
        c[1]=GetSubPixelV2H(tex,uv,offset);
        c[2]=GetSubPixelH2(tex,uv,offset);
        c[3]=GetSubPixelX4H(tex,uv,offset);
    }else if(s==1){
        c[0]=GetSubPixelV2(tex,uv,offset);
        c[1]=fetchpix(tex,uv,0); 
        c[2]=GetSubPixelX4(tex,uv,offset);
        c[3]=GetSubPixelH2H(tex,uv,offset);
    }else if(s==2){
        c[0]=GetSubPixelH2(tex,uv,offset);
        c[1]=GetSubPixelX4H(tex,uv,offset);
        c[2]=fetchpix(tex,uv,0);
        c[3]=GetSubPixelV2H(tex,uv,offset);
    }else{
        c[0]=GetSubPixelX4(tex,uv,offset);
        c[1]=GetSubPixelH2H(tex,uv,offset);
        c[2]=GetSubPixelV2(tex,uv,offset);
        c[3]=fetchpix(tex,uv,0);
    }
    PigmentData p;
    setRL(c[0],p); setRH(c[1],p); setAL(c[2],p); setAH(c[3],p);
    return p;
}
const uvec4 DB[4]=uvec4[4](uvec4(0,1,2,3),uvec4(1,0,3,2),uvec4(2,3,0,1),uvec4(3,2,1,0));
PigmentData GetPixelQuick(highp usampler2D tex, ivec2 uv){
    PixType c[4];
    c[0]=fetchpix(tex,uv,0); 
    c[1]=fetchpix(tex,ivec2(uv.x,uv.y+1),0);
    c[2]=fetchpix(tex,ivec2(uv.x+1,uv.y),0);
    c[3]=fetchpix(tex,ivec2(uv.x+1,uv.y+1),0);
    int s=uv.x%2*2+uv.y%2;
    PigmentData p;
    setRL(c[DB[s][0]],p); setRH(c[DB[s][1]],p); setAL(c[DB[s][2]],p); setAH(c[DB[s][3]],p);
    return p;
}
PigmentData GetPixel(highp usampler2D tex, ivec2 uv){
    PixType c0=fetchpix(tex,uv,0); 
    PixType c1=fetchpix(tex,ivec2(uv.x,uv.y+1),0);
    PixType c2=fetchpix(tex,ivec2(uv.x+1,uv.y),0);
    PixType c3=fetchpix(tex,ivec2(uv.x+1,uv.y+1),0);
    PigmentData p;
    setRL(c0,p); setRH(c1,p); setAL(c2,p); setAH(c3,p);
    return p;
}
PixType PackPixel(PigmentData p, int choose){
    switch(choose){
        case 0: return getRL(p); case 1: return getRH(p);
        case 2: return getAL(p); case 3: return getAH(p);
        default: return PixType(0u);
    }
}
void PigmentMixSlices(float a[16], inout float b[16], float factor){
    if(factor==1.) return; if(factor==0.){ for(int i=0;i<16;i++){b[i]=a[i];} return; }
    float fac=(1.0f-factor)*a[15]; float fac1=factor*b[15]; if(fac+fac1==0.){ return; }
    float scale=1.0/(fac+fac1); b[15]=mix(a[15],b[15],factor); fac*=scale; fac1*=scale;
    for(int i=0;i<OUR_SPECTRAL_SLICES;i++){
        b[i]=safepow(a[i],fac)*safepow(b[i],fac1);
    }
}
void PigmentOverSlices(float a[16], inout float b[16]){
    float fac=a[15]; float fac1=(1.0f-fac)*b[15]; if(fac==0.) return;
    float scale=1.0/(fac+fac1); b[15]=fac1+fac; fac*=scale; fac1*=scale;
    for(int i=0;i<OUR_SPECTRAL_SLICES;i++){
        b[i]=safepow(a[i],fac)*safepow(b[i],fac1);
    }
}
void PigmentMultiplySlices(float a[16], inout float b[16], float factor){
    float fac=a[15]*factor; float fac1=b[15]; if(fac==0.) return;
    if(fac1==0.0f){ for(int i=0;i<OUR_SPECTRAL_SLICES;i++){ b[i]=a[i]; } b[15]=fac; }
    b[15]=1.0f-(1.0-fac1)*(1.0f-fac);
    for(int i=0;i<OUR_SPECTRAL_SLICES;i++){
        float pre=1.-(1.0f-b[i])*fac1; float mult=pre*(1.-(1.0f-a[i])*fac);
        b[i]=1.0f-(1.-mult)/b[15];
    }
}
PigmentData PigmentMix(PigmentData p0, PigmentData p1, float factor){
    PigmentData result=p1;
    PigmentMixSlices(p0.a,result.a,factor);
    PigmentMixSlices(p0.r,result.r,factor);
    return result;
}
PigmentData PigmentOver(PigmentData p0, PigmentData p1){
    PigmentData result=p1; float mfac=1.0;//p0.r[15];
    float rfac=p0.r[15]; result.a[15]=mix(result.a[15],0.,rfac*rfac);
    PigmentOverSlices(p0.r,result.r);
    PigmentMultiplySlices(p0.a,result.a,mfac);

    return result;
}
void PigmentAdd(inout PigmentData p, PigmentData on_top){
    for(int i=0;i<16;i++){ p.r[i]+=on_top.r[i]; p.a[i]+=on_top.a[i]; }
}
void PigmentMultiply(inout PigmentData p, float a){
    for(int i=0;i<15;i++){ p.r[i]*=a; p.a[i]*=a; }
}
PigmentData PigmentInterpolate(PigmentData p0, PigmentData p1, float fac){
    PigmentData a; for(int i=0;i<16;i++){ a.r[i]=mix(p0.r[i],p1.r[i],fac); a.a[i]=mix(p0.a[i],p1.a[i],fac); } return a;
}

vec3 XYZ2sRGB(vec3 xyz){
	mat3 mat=mat3(vec3(3.2404542,-1.5371385,-0.4985314),
				  vec3(-0.9692660,1.8760108,0.0415560),
				  vec3(0.0556434,-0.2040259,1.0572252));
	return xyz*mat;
}

float srgb_transfer_function(float a){
	return .0031308f >= a ? 12.92f * a : 1.055f * pow(a, .4166666666666667f) - .055f;
}
vec3 to_log_srgb(vec3 color){
	return vec3(srgb_transfer_function(color.r),srgb_transfer_function(color.g),srgb_transfer_function(color.b));
}

float PigmentCMF[3][14]=float[3][14](
float[14](0.0343533436363636,0.220925140909091,0.328355822727273,0.2018815,0.0360974655,0.0285879281818182,0.215876535454545,0.525338609090909,0.906198259090909,1.13085586363636,0.895278031818182,0.435115186363636,0.138809882272727,0.0324976972727273),
float[14](0.00359930259090909,0.0236005122727273,0.0565472954545455,0.114833071818182,0.236568031818182,0.535090640909091,0.876579286363636,0.992233536363636,0.923666477272727,0.708120895454545,0.419073681818182,0.178679336363636,0.0541232845454545,0.0124627878181818),
float[14](0.171746535909091,1.15671911363636,1.84186645454545,1.32759531363636,0.488183445454546,0.12631411,0.0225265765,0.00293351760909091,0.000351412640909091,4.70501886363636E-05,3.51041136363636E-06,0.,0.,0.)
);
const float PigmentCMFNormalize=5.13517814086364; 
vec3 Spectral2XYZ(float spec[OUR_SPECTRAL_SLICES]){
    vec3 xyz=vec3(0.,0.,0.);
    for(int i=0;i<OUR_SPECTRAL_SLICES;i++){
        xyz[0]+=spec[i]*PigmentCMF[0][i];
        xyz[1]+=spec[i]*PigmentCMF[1][i];
        xyz[2]+=spec[i]*PigmentCMF[2][i];
    }
    vec3 XYZ;
    XYZ[0]=xyz[0]/PigmentCMFNormalize;
    XYZ[1]=xyz[1]/PigmentCMFNormalize;
    XYZ[2]=xyz[2]/PigmentCMFNormalize;
    return XYZ; 
}

vec3 PigmentToRGB(PigmentData pd, PigmentData light){
    float slices[OUR_SPECTRAL_SLICES];
    for(int i=0;i<OUR_SPECTRAL_SLICES;i++){
        float absfac=1.0f-(1.0f-pd.a[i])*pd.a[15]; if(absfac<0.)absfac=0.; slices[i]=pd.r[i]*absfac;
        slices[i]*=light.r[i];
    }
    vec3 xyz=Spectral2XYZ(slices); vec3 rgb=XYZ2sRGB(xyz); return rgb;
}

#define GetImgPixel(tex, uv, p) \
{ \
    PixType c0=loadpix(tex,uv); \
    PixType c1=loadpix(tex,ivec2(uv.x,uv.y+1)); \
    PixType c2=loadpix(tex,ivec2(uv.x+1,uv.y)); \
    PixType c3=loadpix(tex,ivec2(uv.x+1,uv.y+1)); \
    setRL(c0,p); setRH(c1,p); setAL(c2,p); setAH(c3,p); \
}

#define WriteImgPixel(tex, uv, p) \
{ \
    PixType c0=getRL(p); PixType c1=getRH(p); PixType c2=getAL(p); PixType c3=getAH(p); \
    imageStore(tex,uv,packpix(c0)); \
    imageStore(tex,ivec2(uv.x,uv.y+1),packpix(c1)); \
    imageStore(tex,ivec2(uv.x+1,uv.y),packpix(c2)); \
    imageStore(tex,ivec2(uv.x+1,uv.y+1),packpix(c3)); \
}

)";

const char OUR_PIGMENT_TEXTURE_MIX_SHADER[]=R"(
#ifndef OUR_GLES
#extension GL_ARB_shading_language_420pack : enable // uniform sampler binding
#endif
precision highp float;
precision highp int;
layout (binding=2) uniform highp usampler2D TexColorUI0;
layout (binding=5) uniform highp usampler2D TexColorUI1;
uniform float MixingTop;

in vec2 fUV;

layout(location = 0) out uvec4 outColor;

#with OUR_PIGMENT_COMMON

void main(){
    ivec2 iuv=ivec2(ivec2(fUV*512.)*2);
    ivec2 iuvscr=ivec2(gl_FragCoord.xy); int xof=iuvscr.x%2; int yof=iuvscr.y%2; iuvscr.x-=xof; iuvscr.y-=yof;

    PigmentData p0 = GetPixel(TexColorUI0,iuv);
    PigmentData p1 = GetPixel(TexColorUI1,iuvscr);
    p0.r[15]*=MixingTop; p0.a[15]*=MixingTop;
    PigmentData result = PigmentOver(p0,p1);

    int choose = xof*2+yof;
    uvec4 pixel = packpix(PackPixel(result,choose));
    outColor=pixel;
}
)";

const char OUR_PIGMENT_COMPOSITION_SHADER[] = R"(
layout(local_size_x = WORKGROUP_SIZE, local_size_y = WORKGROUP_SIZE, local_size_z = 1) in;
#ifdef OUR_GLES
precision highp uimage2D;
precision highp float;
precision highp int;
layout(r32ui, binding = 0) uniform uimage2D top;
layout(r32ui, binding = 1) uniform uimage2D bottom;
#else
layout(rgba16ui, binding = 0) uniform uimage2D top;
layout(rgba16ui, binding = 1) uniform uimage2D bottom;
#endif
uniform float uAlphaTop;
uniform float uAlphaBottom;

#with OUR_PIGMENT_COMMON

void main() {
    ivec2 px=ivec2(gl_GlobalInvocationID.xy)*2;

    PigmentData p0=PIGMENT_BLANK; GetImgPixel(top, px, p0);
    PigmentData p1=PIGMENT_BLANK; GetImgPixel(bottom, px, p1);

    float afac=uAlphaTop/uAlphaBottom;
    if(afac==0.){ return; }

    p0.r[15]*=afac; p0.a[15]*=afac;

    PigmentData result=PigmentOver(p0,p1);

    WriteImgPixel(bottom,px,result);
}
)";

const char OUR_PIGMENT_TEXTURE_DISPLAY_SHADER[]=R"(
#ifndef OUR_GLES
#extension GL_ARB_shading_language_420pack : enable // uniform sampler binding
#endif
precision highp float;
precision highp int;
layout (binding=2) uniform highp usampler2D TexColorUI;
uniform float texture_scale;
uniform int display_mode;
uniform ivec2 frag_offset;

in vec2 fUV;

layout(location = 0) out vec4 outColor;

#with OUR_PIGMENT_COMMON

layout(std140) uniform CanvasPigmentBlock{
    PigmentData light;
    PigmentData paper;
}uCanvasPigment;

void main(){
    ivec2 iuv=ivec2(gl_FragCoord.xy)+frag_offset;
    //ivec2(fUV*vec2(display_size)); //int xof=iuv.x%2; int yof=iuv.y%2; iuv.x-=xof; iuv.y-=yof;

    int offset=int(texture_scale/2.)*2+1;
    PigmentData p0;
    if(display_mode==0){
        p0=GetPixelQuick(TexColorUI,iuv);
    }else if(display_mode==1){
        p0=GetPixelDebayer(TexColorUI,iuv,offset);
    }else if(display_mode==2){
        p0=GetPixelQuick(TexColorUI,iuv*2);
    }else if(display_mode==3){
                    p0=GetPixelDebayer(TexColorUI,iuv*2+ivec2(0,0),offset);
        PigmentData p1=GetPixelDebayer(TexColorUI,iuv*2+ivec2(0,1),offset);
        PigmentData p2=GetPixelDebayer(TexColorUI,iuv*2+ivec2(1,1),offset);
        PigmentData p3=GetPixelDebayer(TexColorUI,iuv*2+ivec2(1,0),offset);
        p0=PigmentMix(PigmentMix(p0,p1,0.5),PigmentMix(p2,p3,0.5),0.5);
    }

    PigmentData final = PigmentOver(p0,uCanvasPigment.paper);
    vec3 pixel = to_log_srgb(PigmentToRGB(final,uCanvasPigment.light));
    outColor=vec4(pixel,1.0);
}
)";
