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

const char OUR_CANVAS_SHADER[]=R"(#version 430
layout(local_size_x = 32, local_size_y = 32, local_size_z = 1) in;
layout(rgba16, binding = 0) uniform image2D img;
layout(rgba16, binding = 1) coherent uniform image2D smudge_buckets;
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
uniform int uBrushLock;
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
    float spec_r[10] = float[10](0,0,0,0,0,0,0,0,0,0); for (int i=0; i < 10; i++) {spec_r[i] = spectral_r_small[i] * r;}
    float spec_g[10] = float[10](0,0,0,0,0,0,0,0,0,0); for (int i=0; i < 10; i++) {spec_g[i] = spectral_g_small[i] * g;}
    float spec_b[10] = float[10](0,0,0,0,0,0,0,0,0,0); for (int i=0; i < 10; i++) {spec_b[i] = spectral_b_small[i] * b;}
    for (int i=0; i<10; i++) {spectral_[i] = spec_r[i] + spec_g[i] + spec_b[i];}
}
vec3 spectral_to_rgb (float spectral[10]) {
    float offset = 1.0 - WGM_EPSILON;
    // We need this tmp. array to allow auto vectorization. <-- How about on GPU?
    float tmp[3] = float[3](0,0,0);
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
    if(uCanvasType==0 || abs(gunky-0.)<1.e-2){ return rfac; }
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
    fac=max(fac,scrape*clamp(gunky,0,1));
    fac=clamp(fac,0.,1.);
    fac*=rfac;
    return mix(rfac,fac,uCanvasFactor);
}

subroutine vec4 MixRoutines(vec4 a, vec4 b, float fac_a);
subroutine(MixRoutines) vec4 DoMixNormal(vec4 a, vec4 b, float fac_a){
    return mix(a,b,1-fac_a);
}
subroutine(MixRoutines) vec4 DoMixSpectral(vec4 a, vec4 b, float fac_a){
    vec4 result = vec4(0,0,0,0);
    result.a=mix(a.a,b.a,1-fac_a);
    float spec_a[10] = {0,0,0,0,0,0,0,0,0,0}; rgb_to_spectral(a.rgb, spec_a);
    float spec_b[10] = {0,0,0,0,0,0,0,0,0,0}; rgb_to_spectral(b.rgb, spec_b);
    float spectralmix[10] = {0,0,0,0,0,0,0,0,0,0};
    for (int i=0; i < 10; i++) { spectralmix[i] = pow(spec_a[i], fac_a) * pow(spec_b[i], 1-fac_a); }
    result.rgb=spectral_to_rgb(spectralmix);
    return result;
}
subroutine uniform MixRoutines uMixRoutineSelection;
vec4 spectral_mix(vec4 a, vec4 b, float fac_a){
    return uMixRoutineSelection(a,b,fac_a);
}
vec4 spectral_mix_unpre(vec4 colora, vec4 colorb, float fac){
    vec4 ca=(colora.a==0)?colora:vec4(colora.rgb/colora.a,colora.a);
    vec4 cb=(colorb.a==0)?colorb:vec4(colorb.rgb/colorb.a,colorb.a);
    float af=colora.a*(1-fac);
    float aa=af/(af+fac*colorb.a+0.000001);
    vec4 result=spectral_mix(ca,cb,aa);
    result.a=mix(colora.a,colorb.a,fac);
    return vec4(result.rgb*result.a,result.a);
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
    vec4 a=(colora.a==0)?colora:vec4(colora.rgb/colora.a,colora.a);
    vec4 b=(colorb.a==0)?colorb:vec4(colorb.rgb/colorb.a,colorb.a);
    vec4 m=vec4(0,0,0,0); float aa=colora.a/(colora.a+(1-colora.a)*colorb.a+0.0001);
    m=spectral_mix(a,b,aa);
    m.a=colora.a+colorb.a*(1-colora.a);
    m=vec4(m.rgb*m.a,m.a);
    return m;
}
int dab(float d, vec2 fpx, vec4 color, float size, float hardness, float smudge, vec4 smudge_color, vec4 last_color, out vec4 final){
    vec4 cc=color;
    float fac=1-pow(d/size,1+1/(1-hardness+1e-4));
    float canvas=SampleCanvas(fpx,uBrushDirection,fac,uBrushForce,uBrushGunkyness);
    cc.a=color.a*canvas*(1-smudge); cc.rgb=cc.rgb*cc.a;
    float erasing=float(uBrushErasing);
    cc=cc*(1-erasing);

    // this looks better than the one commented out below
    vec4 c2=spectral_mix_unpre(last_color,smudge_color,smudge*fac*color.a*canvas);
    c2=mix_over(cc,c2);
    //vec4 c2=mix_over(cc,last_color);
    //c2=spectral_mix_unpre(c2,smudge_color,smudge*fac*color.a*canvas);

    c2=spectral_mix_unpre(c2,c2*(1-fac*color.a),erasing*canvas);
    final=c2;
    return 1;
}

#ifndef saturate
#define saturate(v) clamp(v, 0, 1)
#endif
const float HCV_EPSILON = 1e-10;
const float HCY_EPSILON = 1e-10;
vec3 hue_to_rgb(float hue){
    float R = abs(hue * 6 - 3) - 1;
    float G = 2 - abs(hue * 6 - 2);
    float B = 2 - abs(hue * 6 - 4);
    return saturate(vec3(R,G,B));
}
vec3 hcy_to_rgb(vec3 hcy){
    const vec3 HCYwts = vec3(0.299, 0.587, 0.114);
    vec3 RGB = hue_to_rgb(hcy.x);
    float Z = dot(RGB, HCYwts);
    if (hcy.z < Z) { hcy.y *= hcy.z / Z; }
    else if (Z < 1) { hcy.y *= (1 - hcy.z) / (1 - Z); }
    return (RGB - Z) * hcy.y + hcy.z;
}
vec3 rgb_to_hcv(vec3 rgb){
    // Based on work by Sam Hocevar and Emil Persson
    vec4 P = (rgb.g < rgb.b) ? vec4(rgb.bg, -1.0, 2.0/3.0) : vec4(rgb.gb, 0.0, -1.0/3.0);
    vec4 Q = (rgb.r < P.x) ? vec4(P.xyw, rgb.r) : vec4(rgb.r, P.yzx);
    float C = Q.x - min(Q.w, Q.y);
    float H = abs((Q.w - Q.y) / (6 * C + HCV_EPSILON) + Q.z);
    return vec3(H, C, Q.x);
}
vec3 rgb_to_hcy(vec3 rgb){
    const vec3 HCYwts = vec3(0.299, 0.587, 0.114);
    // Corrected by David Schaeffer
    vec3 HCV = rgb_to_hcv(rgb);
    float Y = dot(rgb, HCYwts);
    float Z = dot(hue_to_rgb(HCV.x), HCYwts);
    if (Y < Z) { HCV.y *= Z / (HCY_EPSILON + Y); }
    else { HCV.y *= (1 - Z) / (HCY_EPSILON + 1 - Y); }
    return vec3(HCV.x, HCV.y, Y);
}


subroutine void BrushRoutines();
subroutine(BrushRoutines) void DoDabs(){
    ivec2 px = ivec2(gl_GlobalInvocationID.xy)+uBrushCorner;
    if(px.x<0||px.y<0||px.x>1024||px.y>1024) return;
    vec2 fpx=vec2(px),origfpx=fpx;
    fpx=uBrushCenter+rotate(fpx-uBrushCenter,uBrushAngle);
    fpx.x=uBrushCenter.x+(fpx.x-uBrushCenter.x)*(1+uBrushSlender);
    float dd=distance(fpx,uBrushCenter); if(dd>uBrushSize) return;
    vec4 dabc=imageLoad(img, px);
    vec4 smudgec=pow(spectral_mix_unpre(pow(imageLoad(smudge_buckets,ivec2(1,0)),p1_22),pow(imageLoad(smudge_buckets,ivec2(0,0)),p1_22),uBrushRecentness),p22);
    vec4 final_color;
    dab(dd,origfpx,uBrushColor,uBrushSize,uBrushHardness,uBrushSmudge,smudgec,dabc,final_color);
    if(final_color.a>0){
        if(uBrushLock==0){ dabc=final_color; }
        else if(uBrushLock==1){ dabc.rgb=final_color.rgb/final_color.a*dabc.a;}
        else if(uBrushLock==2){ vec3 xyz=rgb_to_hcy(dabc.rgb); xyz.xy=rgb_to_hcy(final_color.rgb).xy; dabc.rgb=hcy_to_rgb(xyz); }
        imageStore(img, px, dabc);
    }
}
subroutine(BrushRoutines) void DoSample(){
    ivec2 p=ivec2(gl_GlobalInvocationID.xy);
    int DoSample=1; vec4 color;
    if(p.y==0){
        vec2 sp=round(vec2(sin(float(p.x)),cos(float(p.x)))*uBrushSize);
        ivec2 px=ivec2(sp)+uBrushCorner; if(px.x<0||px.y<0||px.x>=1024||px.y>=1024){ DoSample=0; }
        if(DoSample!=0){
            ivec2 b=uBrushCorner; if(b.x>=0&&b.y>=0&&b.x<1024&&b.y<1024){ imageStore(smudge_buckets,ivec2(128+32,0),imageLoad(img, b)); }
            color=imageLoad(img, px);
            imageStore(smudge_buckets,ivec2(p.x+128,0),color);
        }
    }else{DoSample=0;}
    memoryBarrier();barrier(); if(DoSample==0) return;
    if(uBrushErasing==0 || p.x!=0) return;
    color=vec4(0,0,0,0); for(int i=0;i<32;i++){ color=color+imageLoad(smudge_buckets, ivec2(i+128,0)); }
    color=spectral_mix_unpre(color/32,imageLoad(smudge_buckets, ivec2(128+32,0)),0.6*(1-uBrushColor.a)); vec4 oldcolor=imageLoad(smudge_buckets, ivec2(0,0));
    imageStore(smudge_buckets,ivec2(1,0),uBrushErasing==2?color:oldcolor);
    imageStore(smudge_buckets,ivec2(0,0),color);
}
subroutine uniform BrushRoutines uBrushRoutineSelection;
void main() {
    uBrushRoutineSelection();
}
)";

const char OUR_COMPOSITION_SHADER[]=R"(#version 430
layout(local_size_x = 32, local_size_y = 32, local_size_z = 1) in;
layout(rgba16, binding = 0) uniform image2D top;
layout(rgba16, binding = 1) uniform image2D bottom;
uniform int uBlendMode;
uniform float uAlphaTop;
uniform float uAlphaBottom;
vec4 mix_over(vec4 colora, vec4 colorb){
    colora=colora*uAlphaTop/uAlphaBottom;
    vec4 c; c.a=colora.a+colorb.a*(1-colora.a);
    c.rgb=(colora.rgb+colorb.rgb*(1-colora.a));
    return c;
}
vec4 add_over(vec4 colora, vec4 colorb){
    colora=colora*uAlphaTop/uAlphaBottom;
    vec4 a=colora+colorb; a.a=clamp(a.a,0,1); return a;
}
void main() {
    ivec2 px=ivec2(gl_GlobalInvocationID.xy);
    vec4 c1=imageLoad(top,px); vec4 c2=imageLoad(bottom,px);
    vec4 c=(uBlendMode==0)?mix_over(c1,c2):add_over(c1,c2);
    imageStore(bottom,px,c);
    imageStore(top,px,vec4(0,0,0,0));
}
)";