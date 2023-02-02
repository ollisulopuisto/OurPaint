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
uniform ivec2 uBrushCorner;
uniform vec2 uBrushCenter;
uniform float uBrushSize;
uniform float uBrushHardness;
uniform float uBrushSmudge;
uniform float uBrushSlender;
uniform float uBrushAngle;
uniform float uBrushRecentness;
uniform vec4 uBrushColor;
uniform vec4 uBackgroundColor;
uniform int uBrushErasing;
const vec4 p1_22=vec4(1.0/2.2,1.0/2.2,1.0/2.2,1.0/2.2);
const vec4 p22=vec4(2.2,2.2,2.2,2.2);
const float WGM_EPSILON=0.001f;
const float T_MATRIX_SMALL[3][12] = {{0.016163474652781, 0.001420445523172, -0.054887305841183,
  -0.065871603913166, -0.205903208273135, -0.062532921841612,
  0.074959700955855, 0.180474083483825, 0.398778827864023,
  0.461711312001723, 0.253843485461910, 0.001944673073610},
 {-0.023482789468200, -0.016937259153745, 0.054365284958976,
  0.068868972628298, 0.258888405935374, 0.412123548933354,
  0.280003145201754, 0.029172844285245, -0.016952611829275,
  -0.028985388398516, -0.016912989806334, -0.000181721110369},
 {0.451833383178114, 0.622986272902632, 0.020381940564709,
  0.015460521778453, -0.003629434201241, -0.048676538578416,
  -0.036229432289079, -0.007323977526407, -0.005984077846982,
  -0.005755040132913, -0.003041519739360, -0.000016761047877}};
const float spectral_r_small[12] = {0.021146405259067, 0.000101655202080, 0.398199126291765,
 0.001743053489250, 0.000148554860119, 0.000146064594547,
 0.000328614570185, 0.910913102591031, 0.916608409655475,
 0.700400409331843, 0.657122476831633, 0.142235538434584};
const float spectral_g_small[12] = {0.147994747800981, 0.000101679918159, 0.496335365339077,
 0.882788054089882, 0.964096335484527, 0.978765395560054,
 0.985895331330888, 0.076452920947158, 0.080461223658394,
 0.296672324245411, 0.339468867320878, 0.139019679306543};
const float spectral_b_small[12] = {0.831573896245073, 0.998692793883148, 0.105770703333882,
 0.112984668836056, 0.037214012464483, 0.022337434144516,
 0.015090957392503, 0.010456093241942, 0.002660665934598,
 0.002980934458404, 0.002468909304878, 0.716325888182986};
void rgb_to_spectral (vec3 rgb, out float spectral_[12]) {
    float offset = 1.0 - WGM_EPSILON;
    float r = rgb.r * offset + WGM_EPSILON;
    float g = rgb.g * offset + WGM_EPSILON;
    float b = rgb.b * offset + WGM_EPSILON;
    float spec_r[12] = {0,0,0,0,0,0,0,0,0,0,0,0}; for (int i=0; i < 12; i++) {spec_r[i] = spectral_r_small[i] * r;}
    float spec_g[12] = {0,0,0,0,0,0,0,0,0,0,0,0}; for (int i=0; i < 12; i++) {spec_g[i] = spectral_g_small[i] * g;}
    float spec_b[12] = {0,0,0,0,0,0,0,0,0,0,0,0}; for (int i=0; i < 12; i++) {spec_b[i] = spectral_b_small[i] * b;}
    for (int i=0; i<12; i++) {spectral_[i] = spec_r[i] + spec_g[i] + spec_b[i];}
}
vec3 spectral_to_rgb (float spectral[12]) {
    float offset = 1.0 - WGM_EPSILON;
    // We need this tmp. array to allow auto vectorization. <-- How about on GPU?
    float tmp[3] = {0,0,0};
    for (int i=0; i<12; i++) {
        tmp[0] += T_MATRIX_SMALL[0][i] * spectral[i];
        tmp[1] += T_MATRIX_SMALL[1][i] * spectral[i];
        tmp[2] += T_MATRIX_SMALL[2][i] * spectral[i];
    }
    vec3 rgb_;
    for (int i=0; i<3; i++) {rgb_[i] = clamp((tmp[i] - WGM_EPSILON) / offset, 0.0f, 1.0f);}
    return rgb_;
}
subroutine vec4 MixRoutines(vec4 a, vec4 b, float fac_a);
subroutine(MixRoutines) vec4 DoMixNormal(vec4 a, vec4 b, float fac_a){
    return mix(a,b,1-fac_a);
}
subroutine(MixRoutines) vec4 DoMixSpectral(vec4 a, vec4 b, float fac_a){
    vec4 result = vec4(0,0,0,0);
    result.a=mix(a.a,b.a,1-fac_a);
    float spec_a[12] = {0,0,0,0,0,0,0,0,0,0,0,0}; rgb_to_spectral(a.rgb, spec_a);
    float spec_b[12] = {0,0,0,0,0,0,0,0,0,0,0,0}; rgb_to_spectral(b.rgb, spec_b);
    float spectralmix[12] = {0,0,0,0,0,0,0,0,0,0,0,0};
    for (int i=0; i < 12; i++) { spectralmix[i] = pow(spec_a[i], fac_a) * pow(spec_b[i], 1-fac_a); }
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
    float aa=af/(af+(1-af)*colorb.a+0.000001);
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
int dab(float d, vec4 color, float size, float hardness, float smudge, vec4 smudge_color, vec4 last_color, out vec4 final){
    vec4 cc=color;
    float fac=1-pow(d/size,1+1/(1-hardness+1e-4));
    cc.a=color.a*fac*(1-smudge); cc.rgb=cc.rgb*cc.a;
    float erasing=float(uBrushErasing);
    cc=cc*(1-erasing);
    // this looks better than the one commented out below
    vec4 c2=spectral_mix_unpre(last_color,smudge_color,smudge*fac*color.a);
    c2=mix_over(cc,c2);
    //vec4 c2=mix_over(cc,last_color);
    //c2=spectral_mix_unpre(c2,smudge_color,smudge*fac*color.a);
    c2=spectral_mix_unpre(c2,c2*(1-fac*color.a),erasing);
    final=c2;
    return 1;
}
subroutine void BrushRoutines();
subroutine(BrushRoutines) void DoDabs(){
    ivec2 px = ivec2(gl_GlobalInvocationID.xy)+uBrushCorner;
    if(px.x<0||px.y<0||px.x>1024||px.y>1024) return;
    vec2 fpx=vec2(px);
    fpx=uBrushCenter+rotate(fpx-uBrushCenter,uBrushAngle);
    fpx.x=uBrushCenter.x+(fpx.x-uBrushCenter.x)*(1+uBrushSlender);
    float dd=distance(fpx,uBrushCenter); if(dd>uBrushSize) return;
    vec4 dabc=imageLoad(img, px);
    vec4 smudgec=pow(spectral_mix_unpre(pow(imageLoad(smudge_buckets,ivec2(1,0)),p1_22),pow(imageLoad(smudge_buckets,ivec2(0,0)),p1_22),uBrushRecentness),p22);
    vec4 final_color;
    dab(dd,uBrushColor,uBrushSize,uBrushHardness,uBrushSmudge,smudgec,dabc,final_color);
    dabc=final_color;
    imageStore(img, px, dabc);
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