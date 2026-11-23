#ifdef _WIN32
// raylib owns the Win32 headers. These two declarations are all gl.h needs
// for the few raw texture-copy calls below, without importing Win32 symbols
// that collide with raylib's API names.
#define APIENTRY __stdcall
#define WINGDIAPI __declspec(dllimport)
#endif
#include <GL/gl.h>
#include "render3d.hpp"
#include "camera_system.hpp"
#include "corridor.hpp"
#include "bulkhead.hpp"
#include "config.hpp"

void BulkheadPillarVisual::updateDistance(float signedDistance, float fadeDistance, const Event& event) {
    const float span = std::max(fadeDistance, 0.001f);
    const float fade = 0.16f;
    if (signedDistance > span) {
        opacity_ = 1.f;
        if (phase_ != 0) { phase_ = 0; if (event) event(BulkheadPillarPhase::Approaching); }
    } else if (signedDistance > 0.f) {
        opacity_ = fade + (1.f - fade) * (signedDistance / span);
        if (phase_ != 1) { phase_ = 1; if (event) event(BulkheadPillarPhase::Approaching); }
    } else if (signedDistance >= -span) {
        opacity_ = fade;
        if (phase_ != 2) { phase_ = 2; if (event) event(BulkheadPillarPhase::CrossedCenter); }
    } else {
        opacity_ = 1.f;
        if (phase_ != 3) { phase_ = 3; if (event) event(BulkheadPillarPhase::Passed); }
    }
}
#include "raymath.h"
#include "rlgl.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstdlib>
#include <string>
#include <fstream>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <cctype>
#include <thread>
#include <chrono>
#include <nlohmann/json.hpp>
#include "entities.hpp"
#include "turret.hpp"

namespace {
// Exact shaders from shooter/3d-test/main.cpp (canonical).
const char* LIT_VS = R"(#version 330
in vec3 vertexPosition; in vec2 vertexTexCoord; in vec3 vertexNormal;
uniform mat4 mvp; uniform mat4 matNormal;
out vec2 uv; out vec3 nrm;
void main(){ uv = vertexTexCoord; nrm = normalize((matNormal*vec4(vertexNormal,0.0)).xyz); gl_Position = mvp*vec4(vertexPosition,1.0); })";
const char* LIT_FS = R"(#version 330
in vec2 uv; in vec3 nrm; uniform sampler2D texture0; uniform vec4 colDiffuse; uniform vec3 lightDir; uniform float amb = 0.0;
out vec4 o;
void main(){ vec4 t = texture(texture0, uv) * colDiffuse;
  float d = max(dot(normalize(nrm), -lightDir), 0.0);
  o = vec4(t.rgb * (amb + 0.9 * d), 1.0); })";
// Title wordmark only: keep the imported metal colors readable from the title camera while
// retaining a directional highlight on the extruded glyph faces. Gameplay hulls keep LIT_FS.
const char* TITLE_GLYPH_FS = R"(#version 330
in vec2 uv; in vec3 nrm; uniform sampler2D texture0; uniform vec4 colDiffuse; uniform vec3 lightDir;
out vec4 o;
void main(){ vec4 t=texture(texture0,uv)*colDiffuse; vec3 n=normalize(nrm), l=normalize(-lightDir);
  float d=max(dot(n,l),0.0); vec3 v=vec3(0.0,0.0,1.0); vec3 h=normalize(l+v);
  float spec=pow(max(dot(n,h),0.0),36.0)*0.24;
  o=vec4(t.rgb*(0.62+0.34*d)+vec3(spec),1.0); })";
const char* METAL_VS = R"(#version 330
in vec3 vertexPosition; in vec2 vertexTexCoord; in vec3 vertexNormal;
uniform mat4 mvp; uniform mat4 matModel; uniform mat4 matNormal;
out vec2 uv; out vec3 nrm; out vec3 worldPos;
void main(){ uv=vertexTexCoord; worldPos=(matModel*vec4(vertexPosition,1.0)).xyz;
  nrm=normalize((matNormal*vec4(vertexNormal,0.0)).xyz); gl_Position=mvp*vec4(vertexPosition,1.0); })";
const char* METAL_FS = R"(#version 330
in vec2 uv; in vec3 nrm; in vec3 worldPos;
uniform sampler2D texture0; uniform vec4 colDiffuse; uniform vec3 lightDir, eye;
uniform float metalness, roughness, envStrength, opacity; uniform float amb = 0.28;
out vec4 o;
void main(){
  vec4 texel=texture(texture0,uv)*colDiffuse;
  vec3 n=normalize(nrm), v=normalize(eye-worldPos), l=normalize(-lightDir);
  float ndl=max(dot(n,l),0.0);
  vec3 h=normalize(l+v);
  float r=clamp(roughness,0.06,0.92);
  float exponent=mix(180.0,9.0,r*r);
  float highlight=pow(max(dot(n,h),0.0),exponent)*ndl;
  vec3 reflected=reflect(-v,n);
  // A restrained corridor environment approximation: cool ceiling strips, warm service
  // lighting and darker armored recesses tint reflections on metal instead of a flat gray.
  float ceiling=pow(max(dot(reflected,normalize(vec3(0.0,0.82,0.57))),0.0),10.0);
  float service=pow(max(dot(reflected,normalize(vec3(0.0,-0.58,0.82))),0.0),14.0);
  vec3 env=vec3(0.025,0.035,0.05)+vec3(0.19,0.36,0.46)*ceiling+vec3(0.26,0.13,0.045)*service;
  float fresnel=pow(1.0-max(dot(n,v),0.0),5.0);
  vec3 base=texel.rgb*(amb+0.86*ndl);
  vec3 spec=vec3(0.72,0.80,0.88)*highlight*(0.22+0.78*metalness);
  vec3 environment=env*envStrength*(0.12+0.48*metalness+0.40*fresnel);
  o=vec4(base*(1.0-0.22*metalness)+spec+environment,texel.a*opacity);
})";
const char* PICKUP_VS = R"(#version 330
in vec3 vertexPosition; in vec2 vertexTexCoord; in vec3 vertexNormal;
uniform mat4 mvp; uniform mat4 matNormal;
out vec2 uv; out vec3 nrm;
void main(){ uv=vertexTexCoord; nrm=normalize((matNormal*vec4(vertexNormal,0.0)).xyz); gl_Position=mvp*vec4(vertexPosition,1.0); })";
const char* PICKUP_FS = R"(#version 330
in vec2 uv; in vec3 nrm; uniform sampler2D texture0; uniform vec4 colDiffuse;
uniform vec3 lightDir, accent; uniform float opacity, flash, dissolve;
out vec4 o;
float noise21(vec2 p){ p=floor(p*180.0); return fract(sin(dot(p,vec2(127.1,311.7)))*43758.5453123); }
void main(){ vec4 t=texture(texture0,uv)*colDiffuse; float n=noise21(uv);
  float edge=0.0; if(dissolve>0.0001){ if(n<dissolve) discard; edge=1.0-smoothstep(dissolve,dissolve+0.10,n); }
  float d=max(dot(normalize(nrm),-lightDir),0.0); vec3 c=t.rgb*(0.34+0.95*d);
  c=mix(c,vec3(1.0),clamp(flash,0.0,1.0)); c+=accent*edge*2.4;
  o=vec4(c,clamp(opacity*t.a,0.0,1.0)); })";
// The shield is a genuine model-space sphere.  The view-dependent fresnel is deliberately
// restrained: it reads as a protective field while leaving the fighter clearly visible inside.
const char* SHIELD_VS = R"(#version 330
in vec3 vertexPosition; in vec3 vertexNormal;
uniform mat4 mvp; uniform mat4 matModel; uniform mat4 matNormal;
out vec3 worldPos; out vec3 nrm;
void main(){ worldPos=(matModel*vec4(vertexPosition,1.0)).xyz;
  nrm=normalize((matNormal*vec4(vertexNormal,0.0)).xyz);
  gl_Position=mvp*vec4(vertexPosition,1.0); })";
const char* SHIELD_FS = R"(#version 330
in vec3 worldPos; in vec3 nrm;
uniform vec3 eye; uniform float time; uniform float charge;
uniform vec4 hits[4];
out vec4 o;
void main(){ vec3 n=normalize(nrm), v=normalize(eye-worldPos);
  float fresnel=pow(1.0-max(dot(n,v),0.0),2.65);
  float flow=0.5+0.5*sin(worldPos.x*8.0+worldPos.y*6.0-worldPos.z*7.0-time*5.0);
  float filaments=pow(flow,15.0)*fresnel;
  float pulse=0.90+0.10*sin(time*4.0);
  float surge=0.0;
  for(int i=0;i<4;i++){ float age=time-hits[i].w; if(age<0.0||age>1.15) continue;
    // Geodesic distance turns this into a ring travelling across the curved shell from the impact.
    float arc=acos(clamp(dot(n,normalize(hits[i].xyz)),-1.0,1.0));
    float travelling=exp(-pow((arc-age*4.8)/0.135,2.0));
    float source=exp(-pow(arc/0.11,2.0))*exp(-age*7.0);
    surge=max(surge,max(travelling,source)*exp(-age*0.7)); }
  // Electric-blue field: a deep cobalt body preserves visibility through the sphere while the
  // rim, moving filaments, and impact arc read as high-energy blue instead of pale cyan.
  vec3 color=mix(vec3(0.012,0.20,1.0),vec3(0.08,0.68,1.0),fresnel);
  color+=vec3(0.14,0.88,1.0)*filaments*0.88;
  color+=vec3(0.46,0.96,1.0)*surge*1.42;
  // Opacity is the shield readout: the whole field (body, rim, filaments) fades with the charge
  // left, from solid at full to a faint, unstable film near empty. Impact flares stay bright
  // so hits still read on a weak field.
  float strength=clamp(charge,0.0,1.0);
  float weak=1.0-smoothstep(0.08,0.30,strength);
  float flicker=1.0-weak*0.35*(0.5+0.5*sin(time*31.0+worldPos.y*40.0));
  float field=(0.070+fresnel*0.31+filaments*0.18)*pulse;
  float alpha=clamp(field,0.0,0.48)*(0.06+0.94*strength)*flicker;
  alpha+=surge*0.34*(0.55+0.45*strength);
  o=vec4(color,clamp(alpha,0.0,0.48)); })";
// Enemy hull shader = LIT_VS/LIT_FS plus up to 8 dents: vertices pushed in along -normal with a smooth
// bowl falloff, normals tilted toward the dent centre, dented area darkened/scorched.
const char* DENT_VS = R"(#version 330
in vec3 vertexPosition; in vec2 vertexTexCoord; in vec3 vertexNormal;
uniform mat4 mvp; uniform mat4 matNormal;
uniform vec4 dents[8]; uniform vec4 surfaceFx[8]; uniform int nDents; uniform float dentRadius;
out vec2 uv; out vec3 nrm; out float dmg; out float rim; out float lip; out float plasmaHeat; out float flameHeat; out float charAmount;
void main(){ vec3 p = vertexPosition; vec3 n = vertexNormal; float k = 0.0, r = 0.0, l = 0.0, ph = 0.0, fh = 0.0, ch = 0.0;
  for (int i = 0; i < 8; i++) { if (i >= nDents) break;
    vec3 dv = p - dents[i].xyz; float d = length(dv);
    float f = 1.0 - smoothstep(0.0, dentRadius, d); f = f * f;
    float localHeat = 1.0 - smoothstep(0.0, dentRadius * 0.9, d);
    float charRadius = dentRadius * (1.0 + 2.0 * surfaceFx[i].w);
    float localChar = 1.0 - smoothstep(0.0, charRadius, d);
    ph = max(ph, surfaceFx[i].x * localHeat); fh = max(fh, surfaceFx[i].y * localHeat);
    ch = max(ch, surfaceFx[i].z * localChar);
    p -= vertexNormal * dents[i].w * f;
    vec3 u = dv - vertexNormal * dot(dv, vertexNormal); float ul = length(u);
    if (ul > 1e-4) n -= (u / ul) * clamp(dents[i].w / dentRadius * 9.0 * f * (1.0 - f), 0.0, 0.95);
    k = max(k, f * dents[i].w);
    float e = smoothstep(dentRadius * 0.55, dentRadius * 0.8, d) * (1.0 - smoothstep(dentRadius * 0.8, dentRadius * 1.05, d));
    r = max(r, e * dents[i].w);
    l = max(l, smoothstep(dentRadius * 0.95, dentRadius * 1.1, d) * (1.0 - smoothstep(dentRadius * 1.1, dentRadius * 1.35, d)) * dents[i].w); }
  dmg = k; rim = r; lip = l; plasmaHeat = ph; flameHeat = fh; charAmount = ch;
  uv = vertexTexCoord; nrm = normalize((matNormal*vec4(normalize(n),0.0)).xyz); gl_Position = mvp*vec4(p,1.0); })";
// Same dent deformation, then rigid GPU skinning (raylib 5.5 boneMatrices) for animated enemy GLBs.
// Dents are applied in the rest pose so they stay glued to the part that was hit.
const char* DENT_SKIN_VS = R"(#version 330
in vec3 vertexPosition; in vec2 vertexTexCoord; in vec3 vertexNormal; in vec4 vertexBoneIds; in vec4 vertexBoneWeights;
uniform mat4 mvp; uniform mat4 matNormal; uniform mat4 boneMatrices[16];
uniform vec4 dents[8]; uniform vec4 surfaceFx[8]; uniform int nDents; uniform float dentRadius;
out vec2 uv; out vec3 nrm; out float dmg; out float rim; out float lip; out float plasmaHeat; out float flameHeat; out float charAmount;
void main(){ vec3 p = vertexPosition; vec3 n = vertexNormal; float k = 0.0, r = 0.0, l = 0.0, ph = 0.0, fh = 0.0, ch = 0.0;
  for (int i = 0; i < 8; i++) { if (i >= nDents) break;
    vec3 dv = p - dents[i].xyz; float d = length(dv);
    float f = 1.0 - smoothstep(0.0, dentRadius, d); f = f * f;
    float localHeat = 1.0 - smoothstep(0.0, dentRadius * 0.9, d);
    float charRadius = dentRadius * (1.0 + 2.0 * surfaceFx[i].w);
    float localChar = 1.0 - smoothstep(0.0, charRadius, d);
    ph = max(ph, surfaceFx[i].x * localHeat); fh = max(fh, surfaceFx[i].y * localHeat);
    ch = max(ch, surfaceFx[i].z * localChar);
    p -= vertexNormal * dents[i].w * f;
    vec3 u = dv - vertexNormal * dot(dv, vertexNormal); float ul = length(u);
    if (ul > 1e-4) n -= (u / ul) * clamp(dents[i].w / dentRadius * 9.0 * f * (1.0 - f), 0.0, 0.95);
    k = max(k, f * dents[i].w);
    float e = smoothstep(dentRadius * 0.55, dentRadius * 0.8, d) * (1.0 - smoothstep(dentRadius * 0.8, dentRadius * 1.05, d));
    r = max(r, e * dents[i].w);
    l = max(l, smoothstep(dentRadius * 0.95, dentRadius * 1.1, d) * (1.0 - smoothstep(dentRadius * 1.1, dentRadius * 1.35, d)) * dents[i].w); }
  vec4 sp = vec4(p, 1.0); vec3 sn = normalize(n);
  float ws = vertexBoneWeights.x + vertexBoneWeights.y + vertexBoneWeights.z + vertexBoneWeights.w;
  if (ws > 0.001) {
    mat4 B = boneMatrices[int(vertexBoneIds.x)] * vertexBoneWeights.x + boneMatrices[int(vertexBoneIds.y)] * vertexBoneWeights.y
           + boneMatrices[int(vertexBoneIds.z)] * vertexBoneWeights.z + boneMatrices[int(vertexBoneIds.w)] * vertexBoneWeights.w;
    sp = B * vec4(p, 1.0); sn = mat3(B) * sn; }
  dmg = k; rim = r; lip = l; plasmaHeat = ph; flameHeat = fh; charAmount = ch;
  uv = vertexTexCoord; nrm = normalize((matNormal*vec4(normalize(sn),0.0)).xyz); gl_Position = mvp*sp; })";
const char* DENT_FS = R"(#version 330
in vec2 uv; in vec3 nrm; in float dmg; in float rim; in float lip; in float plasmaHeat; in float flameHeat; in float charAmount;
uniform sampler2D texture0; uniform vec4 colDiffuse; uniform vec3 lightDir;
uniform float dentMaxDepth, dentDarken, glowBoost, time; uniform float amb = 0.0;
out vec4 o;
void main(){ vec4 t = texture(texture0, uv) * colDiffuse;
  float d = max(dot(normalize(nrm), -lightDir), 0.0);
  vec3 c = t.rgb * (amb + 0.9 * d);
  // Emissive texels: the enemy GLBs store a glow mask as (1 - alpha) in the base colour texture
  // (opaque legacy textures have alpha 1 = no glow). Glow ignores the light and pulses gently.
  float g = clamp((1.0 - t.a) * 1.18, 0.0, 1.0) * (1.0 - clamp(dmg / dentMaxDepth, 0.0, 1.0));
  c = mix(c, t.rgb * (1.15 + 0.12 * sin(time * 5.0 + uv.x * 9.0) + glowBoost * 0.9), g);
  float a = clamp(dmg / dentMaxDepth, 0.0, 1.0) * dentDarken;
  c = mix(c, vec3(0.16, 0.08, 0.045) + c * 0.25, a);   // darkened, slightly scorched bowl
  float e = clamp(rim / dentMaxDepth, 0.0, 1.0);
  c = mix(c, vec3(0.03, 0.02, 0.015), e * 0.85);          // dark crease where the metal folds
  float soot = clamp(charAmount, 0.0, 1.0);
  c = mix(c, vec3(0.008, 0.006, 0.005), soot * 0.9);
  float melt = clamp(plasmaHeat, 0.0, 1.0), pulse = 0.84 + 0.16 * sin(time * 19.0 + uv.x * 15.0 + uv.y * 9.0);
  vec3 hotRed = mix(vec3(0.28, 0.006, 0.002), vec3(1.0, 0.13, 0.012), melt);
  c = mix(c, c * 0.16 + hotRed, melt * (0.56 + 0.38 * pulse));
  float burn = clamp(flameHeat, 0.0, 1.0);
  vec3 hotOrange = mix(vec3(0.22, 0.035, 0.004), vec3(1.0, 0.32, 0.025), burn);
  c = mix(c, c * 0.24 + hotOrange, burn * 0.82);
  c += vec3(1.0, 0.36, 0.06) * pow(a, 2.0) * 0.55;        // hot core in deep dents
  c += vec3(0.55, 0.6, 0.7) * clamp(lip / dentMaxDepth, 0.0, 1.0) * 0.45;  // bright pushed-up lip
  o = vec4(c, 1.0); })";
// Background planets use their embedded GLB albedo. The existing relief range still gives
// lightweight extra depth for Ryan's displaced source meshes.
const char* BG_VS = R"(#version 330
in vec3 vertexPosition; in vec2 vertexTexCoord; in vec3 vertexNormal;
uniform mat4 mvp; uniform mat4 matNormal;
out vec2 uv; out vec3 nrm; out vec3 mp;
void main(){ uv = vertexTexCoord; mp = vertexPosition; nrm = normalize((matNormal*vec4(vertexNormal,0.0)).xyz); gl_Position = mvp*vec4(vertexPosition,1.0); })";
const char* PLANET_FS = R"(#version 330
in vec2 uv; in vec3 nrm; in vec3 mp; uniform sampler2D texture0; uniform vec3 baseCol; uniform vec2 rr; uniform vec3 eye, sun; out vec4 o;
uniform float sunMode = 0.0, amb = 0.06, cityK = 0.0;
void main(){ vec3 n = normalize(nrm); vec3 L = sun;
  float e = clamp((length(mp) - rr.x) / max(1e-4, rr.y - rr.x), 0.0, 1.0);   // relief 0 = lowland .. 1 = peaks
  vec3 albedo = texture(texture0, uv).rgb * baseCol;
  vec3 low = albedo * 0.76, high = mix(albedo, vec3(0.9), 0.14);
  vec3 c = mix(low, high, smoothstep(0.25, 0.9, e));
  float d = max(dot(n, L), 0.0);
  float rim = pow(1.0 - max(dot(n, eye), 0.0), 3.0);                                   // thin atmosphere at the limb
  if (sunMode > 0.5) {  // level 6: the sun is the only light. Rim glow is sunlight scattered forward
    float twi = smoothstep(-0.22, 0.25, dot(n, L));                  // ...so it lives near the lit/terminator side
    float fwd = pow(max(dot(-eye, L), 0.0), 3.0);                    // strongest with the sun behind the planet
    vec3 col = c * (amb + 0.95 * d) + baseCol * rim * twi * (0.18 + 1.5 * fwd) * 0.6;
    float night = 1.0 - smoothstep(-0.14, 0.04, dot(n, L));           // faint city lights on the night side
    vec3 nm = normalize(mp), q = floor(nm * 150.0), Q = floor(nm * 11.0);
    float h = fract(sin(dot(q, vec3(12.9898, 78.233, 37.719))) * 43758.5453);
    float cl = fract(sin(dot(Q, vec3(39.346, 11.135, 83.155))) * 24634.6345);   // sparse clusters ("cities")
    col += vec3(1.0, 0.72, 0.40) * step(0.993, h + 0.006 * step(0.72, cl)) * step(0.72, cl) * (1.0 - smoothstep(0.15, 0.55, e)) * night * cityK;
    o = vec4(col, 1.0); return; }
  o = vec4((c * (0.06 + 0.85 * d) + baseCol * rim * 0.35 * (0.3 + d)) * 0.85, 1.0); })";  // background: a bit dimmer than ships
const char* TITLE_PLANET_VS = R"(#version 330
in vec3 vertexPosition; in vec2 vertexTexCoord; in vec3 vertexNormal; in vec4 vertexTangent;
uniform mat4 mvp; uniform mat4 matNormal;
out vec2 uv; out vec3 nrm; out vec3 tan; out float tanSign; out vec3 mp;
void main(){ uv=vertexTexCoord; mp=vertexPosition;
  float fl=dot(vertexNormal,vertexPosition)<0.0?-1.0:1.0;   // t228: the 4k glb's normals point inward; light the outside
  mat3 nm=mat3(matNormal); nrm=normalize(nm*vertexNormal)*fl; tan=normalize(nm*vertexTangent.xyz); tanSign=vertexTangent.w*fl;
  gl_Position=mvp*vec4(vertexPosition,1.0); })";
const char* TITLE_PLANET_FS = R"(#version 330
in vec2 uv; in vec3 nrm; in vec3 tan; in float tanSign; in vec3 mp;
uniform sampler2D texture0; uniform sampler2D texture2; uniform vec4 colDiffuse;
uniform vec3 eye, sun; uniform float roughnessFactor=1.0, normalScale=1.0;
out vec4 o;
void main(){
  vec3 N=normalize(nrm), T=normalize(tan-dot(tan,N)*N), B=normalize(cross(N,T))*tanSign;
  vec3 tn=texture(texture2,uv).xyz*2.0-1.0; tn.xy*=normalScale;
  N=normalize(mat3(T,B,N)*tn);
  vec3 L=normalize(sun), V=normalize(eye), H=normalize(L+V);
  vec3 albedo=texture(texture0,uv).rgb*colDiffuse.rgb;
  float ndl=max(dot(N,L),0.0), ndv=max(dot(N,V),0.0);
  float rough=clamp(roughnessFactor,0.08,1.0);
  float spec=pow(max(dot(N,H),0.0),mix(96.0,2.0,rough))*(1.0-rough)*0.22;
  float rim=pow(1.0-ndv,3.0)*0.10;
  vec3 color=albedo*(0.09+0.91*ndl)+vec3(spec)+albedo*rim*(0.35+0.65*ndl);
  o=vec4(color,1.0);
})";
const char* TITLE_SHELL_VS = R"(#version 330
in vec3 vertexPosition; in vec2 vertexTexCoord; in vec3 vertexNormal;
uniform mat4 mvp; uniform mat4 matNormal;
out vec2 uv; out vec3 nrm;
void main(){ uv=vertexTexCoord; nrm=normalize((matNormal*vec4(vertexNormal,0.0)).xyz); gl_Position=mvp*vec4(vertexPosition,1.0); })";
const char* TITLE_SHELL_FS = R"(#version 330
in vec2 uv; in vec3 nrm; uniform sampler2D texture0; uniform vec4 colDiffuse; uniform vec3 sun;
uniform float alphaK=1.0; out vec4 o;
void main(){ vec4 t=texture(texture0,uv)*colDiffuse; float d=max(dot(normalize(nrm),normalize(sun)),0.0);
  vec3 glow=t.rgb*(0.78+0.22*d); o=vec4(glow,t.a*alphaK); })";

// Level 6 atmosphere shell (drawn additively over the planet with the scenery camera). Each pixel
// marches the camera ray through the shell between Rp and Ra (stopping at the planet), summing
// density lit by the ONE sun (planet shadow tested per sample). Thickness therefore grows toward
// the limb by itself; the forward lobe is strongest on the limb where the planet hides the sun.
const char* ATMO_VS = R"(#version 330
in vec3 vertexPosition; uniform mat4 mvp; uniform mat4 matModel; out vec3 wp;
void main(){ wp = (matModel*vec4(vertexPosition,1.0)).xyz; gl_Position = mvp*vec4(vertexPosition,1.0); })";
const char* ATMO_FS = R"(#version 330
in vec3 wp; out vec4 o;
uniform vec3 camPos, sun; uniform float Rp = 0.995, Ra = 1.04, gain = 1.0, lobe = 10.0, dK = 4.0;
const vec3 blue = vec3(0.27, 0.34, 0.88), vio = vec3(0.67, 0.42, 1.0), hot = vec3(1.0, 0.93, 0.84);   // the old band's palette
void main(){
  vec3 d = normalize(wp - camPos);
  float b = dot(camPos, d), disc = b * b - (dot(camPos, camPos) - Ra * Ra);
  if (disc <= 0.0) discard;
  float sq = sqrt(disc);
  if (length(wp - camPos) < -b) discard;              // near-side fragment: exactly one per pixel
  float t0 = max(-b - sq, 0.0), t1 = -b + sq;
  float dp = b * b - (dot(camPos, camPos) - Rp * Rp);
  if (dp > 0.0) { float tp = -b - sqrt(dp); if (tp > 0.0) t1 = min(t1, tp); }
  if (t1 <= t0) discard;
  float H = Ra - Rp, ds = (t1 - t0) / 8.0, lit = 0.0, all = 0.0;
  for (int i = 0; i < 8; i++) {
    vec3 p = camPos + d * (t0 + ds * (float(i) + 0.5));
    float r = length(p), h = clamp((r - Rp) / H, 0.0, 1.0);
    float den = exp(-dK * h) * (1.0 - h);
    float ps = dot(p, sun), perp = sqrt(max(r * r - ps * ps, 0.0));
    float vis = ps > 0.0 ? 1.0 : smoothstep(Rp * 0.995, Rp + 0.35 * H, perp);   // planet shadow
    lit += den * vis; all += den;
  }
  lit *= ds / H; all *= ds / H;                        // columns in shell thicknesses
  vec3 ch = normalize(-camPos), dv = d - ch * dot(d, ch), sv = sun - ch * dot(sun, ch);
  float az = (dot(dv, dv) > 1e-12 && dot(sv, sv) > 1e-12) ? dot(normalize(dv), normalize(sv)) : 0.0;
  float a01 = 0.5 + 0.5 * az;                          // 1 on the limb where the sun is eclipsed
  float mu = dot(d, sun), g = 0.76;
  float hg = (1.0 - g * g) / pow(1.0 + g * g - 2.0 * g * mu, 1.5) * 0.08;
  float ray = 0.75 * (1.0 + mu * mu);
  vec3 rc = mix(blue, vio, smoothstep(0.3, 0.9, a01));
  vec3 col = (lit * (rc * 0.55 * ray * (0.10 + 0.90 * pow(a01, 3.0)) + hot * 0.9 * hg * pow(a01, lobe))
           + all * blue * 0.03) * gain;
  o = vec4(vec3(1.0) - exp(-col), 1.0); })";
// Liftoff sun (t214): a procedural ball of plasma, no photo. The billboard is a ray-sphere
// impostor (perfectly round limb at any size): the view-space sphere normal, turned into the sun's
// own frame (fixed in the world, slowly spinning), drives 3D value noise: domain-warped boiling
// granulation, ridged intergranular lanes, white-hot active regions with filaments that flare,
// limb darkening and a bright chromospheric rim; outside the disc a shader corona with streamers
// and limb spicules. Premultiplied output (body opaque, corona additive). texture0 = noise lattice.
#define U11_NOISE3 \
  "float n3(vec3 p){ vec3 i = floor(p); vec3 f = fract(p); f = f * f * (3.0 - 2.0 * f);\n" \
  "  vec2 uv = (i.xy + vec2(37.0, 17.0) * i.z) + f.xy;\n" \
  "  vec2 rg = textureLod(texture0, (uv + 0.5) / 256.0, 0.0).xy;\n" \
  "  return mix(rg.x, rg.y, f.z); }\n"
const char* SUN_FS = "#version 330\n"
"in vec2 fragTexCoord; in vec4 fragColor; out vec4 o;\n"
"uniform sampler2D texture0; uniform float time; uniform float aa; uniform float Q; uniform mat4 rot;\n"
U11_NOISE3 R"(
vec3 ramp(float T){
  vec3 c0 = vec3(0.50, 0.08, 0.01), c1 = vec3(0.90, 0.30, 0.03), c2 = vec3(1.00, 0.54, 0.07), c3 = vec3(1.00, 0.78, 0.28), c4 = vec3(1.00, 0.97, 0.86);
  T = clamp(T, 0.0, 1.0);
  if (T < 0.25) return mix(c0, c1, T / 0.25);
  if (T < 0.5) return mix(c1, c2, (T - 0.25) / 0.25);
  if (T < 0.75) return mix(c2, c3, (T - 0.5) / 0.25);
  return mix(c3, c4, (T - 0.75) / 0.25); }
void main(){
  vec2 p = (fragTexCoord * 2.0 - 1.0) * Q; p.y = -p.y;
  float r = length(p), t = time;
  vec3 body = vec3(0.0); float a = 0.0;
  if (r < 1.0 + aa) {
    float rr = min(r, 1.0), z = sqrt(max(1.0 - rr * rr, 0.0));
    vec3 nv = vec3(p / max(r, 1e-5) * rr, z);
    vec3 n = normalize((rot * vec4(nv, 0.0)).xyz);
    vec3 w = vec3(n3(n * 2.6 + vec3(0.0, 0.0, t * 0.04)), n3(n * 2.6 + vec3(5.2, 1.3, -t * 0.04)), n3(n * 2.6 + vec3(1.7, 9.2, t * 0.05))) - 0.5;
    vec3 qq = n * 7.0 + w * 1.6;
    float g = 0.5 * n3(qq + vec3(t * 0.10, 0.0, 0.0)) + 0.3 * n3(qq * 2.2 + vec3(0.0, -t * 0.16, 0.0)) + 0.2 * n3(qq * 4.7 + vec3(0.0, 0.0, t * 0.30));
    float gr = 0.65 * n3(n * 34.0 + w * 2.0 + vec3(0.0, t * 0.22, 0.0)) + 0.35 * n3(n * 71.0 + vec3(t * 0.3, 0.0, 0.0));
    float cl = smoothstep(0.30, 0.78, gr);                                                       // granulation: bright cells, dark lanes
    float act = smoothstep(0.54, 0.76, n3(n * 1.9 + vec3(t * 0.006, 3.3, 0.0)));               // active regions
    float fil = pow(1.0 - abs(2.0 * n3(n * 16.0 + w * 4.0 + vec3(0.0, 0.0, -t * 0.12)) - 1.0), 5.0);
    float fl = act * pow(0.5 + 0.5 * sin(t * 0.7 + 23.0 * n3(n * 1.1 + 7.0)), 10.0);         // flaring
    float T = 0.27 + 0.55 * (g - 0.35) + 0.20 * cl + act * (0.20 + 0.60 * fil) + 0.65 * fl;
    T *= mix(0.70, 1.0, pow(z, 0.45));                                                          // limb darkening
    body = ramp(T) * (1.0 + 0.6 * fl) + vec3(1.0, 0.82, 0.40) * smoothstep(0.88, 1.0, rr) * 0.40;   // bright limb
    a = 1.0 - smoothstep(1.0 - aa, 1.0 + aa, r);
  }
  float e = max(r - 1.0, 0.0), an = atan(p.y, p.x);
  vec2 cs = vec2(cos(an), sin(an));
  float st = n3(vec3(cs * 3.0, e * 2.5 - t * 0.12));
  float sp = n3(vec3(cs * 55.0, t * 0.5 + e * 30.0));
  vec3 cg = vec3(1.0, 0.50, 0.12) * (exp(-e * 10.0) * 0.85 + exp(-e * 3.0) * 0.30 * (0.5 + st) + exp(-e * 1.6) * 0.16)
          + vec3(1.0, 0.75, 0.35) * exp(-e * 45.0) * (0.45 + 0.75 * sp);
  cg *= 1.0 - smoothstep(Q * 0.75, Q, r);
  o = vec4(body * a + cg * (1.0 - a), a); })";
// Prominence / eruption ribbons (camera-facing strips along 3D arcs off the limb): u along, v across.
// fragColor: g = eruption heat, b = per-ribbon seed, a = fade x limb occlusion. Additive.
const char* PROM_FS = R"(#version 330
in vec2 fragTexCoord; in vec4 fragColor; out vec4 o;
uniform sampler2D texture0; uniform float time;
void main(){
  float u = fragTexCoord.x, v = fragTexCoord.y * 2.0 - 1.0, k = fragColor.b;
  float n1 = texture(texture0, (vec2(u * 10.0 - time * 0.45, v * 0.8) + k * 61.0) / 256.0).r;
  float n2 = texture(texture0, (vec2(u * 18.0 + time * 0.6, v * 1.5) + k * 97.0) / 256.0).b;
  float strands = 0.70 + 0.30 * sin(v * 6.0 + n1 * 7.0 + u * 3.0);   // twisted plasma threads
  float core = exp(-v * v * 3.0);
  float I = core * strands * (0.65 + 0.7 * n2) * fragColor.a * 1.35;
  vec3 c = mix(vec3(0.85, 0.16, 0.03), vec3(1.0, 0.62, 0.18), clamp(core * n2 * 1.1 + fragColor.g * 0.5, 0.0, 1.0));
  o = vec4(c * I, 1.0); })";
// Liftoff volumetric clouds + atmosphere. Unit planet at the origin, camera C (radii) with basis
// F/R/U; per pixel: the cloud deck (shell deck.x..deck.y above the ground) is ray-marched with 3D
// noise in the planet's own frame (W2L), lit by the ONE sun (one shadow sample toward it + sky
// ambient), and the exponential atmosphere (scale height Hs) is integrated in front of and behind
// it with the planet's shadow. [sMin, sMax] picks the far (ship..) or near (camera..ship) pass.
// Output premultiplied (rgb in-scatter, a = 1 - transmittance).
const char* VOLFOG_FS = "#version 330\n"
"in vec2 fragTexCoord; out vec4 o;\n"
"uniform sampler2D texture0; uniform vec3 C; uniform vec3 F; uniform vec3 R; uniform vec3 U; uniform vec3 sunD; uniform vec3 wind;\n"
"uniform mat4 W2L; uniform vec2 tanH; uniform vec2 deck;\n"
"uniform float cover; uniform float cloudK; uniform float atmoK; uniform float Hs; uniform float sMin; uniform float sMax;\n"
"uniform float gain; uniform float deckK; uniform float freq; uniform float steps; uniform vec4 patchL;\n"
U11_NOISE3 R"(
vec2 sph(vec3 c, vec3 d, float r){
  float b = dot(c, d), cc = dot(c, c) - r * r, disc = b * b - cc;
  if (disc < 0.0) return vec2(1e9, -1e9);
  float sq = sqrt(disc), q = (b > 0.0) ? -b - sq : -b + sq;
  float t1 = abs(q) > 1e-20 ? cc / q : 0.0;
  return vec2(min(q, t1), max(q, t1)); }
float FP;   // noise cells per pixel at the sample (fades the octaves the pixel cannot resolve)
// Coverage: weather-system sized patches with clear gaps (planet frame, so they stay put on the globe
// and read the same from the ground, from the punch-through and from orbit), wispy domain-warped edges.
float coverage(vec3 pl){
  vec3 qm = pl * 11.0;
  vec3 w1 = vec3(n3(qm * 0.8 + 1.3), n3(qm * 0.8 + 7.1), n3(qm * 0.8 + 4.2)) - 0.5;   // big swirls (curl-like double warp)
  vec3 qa = qm + w1 * 2.4;
  vec3 w2 = vec3(n3(qa * 2.2 + 3.0), n3(qa * 2.2 + 8.0), n3(qa * 2.2 + 5.5)) - 0.5;
  vec3 qs = qa + w2 * 0.8;
  float m = 0.50 * n3(qs) + 0.26 * n3(qs * 2.3 + 5.1)
          + 0.24 * n3(vec3(qs.x * 2.2, qs.y * 7.0, qs.z * 2.2) + 2.7);   // stretched streaks along the latitude
  float pc = patchL.w * smoothstep(0.050, 0.012, distance(normalize(pl), patchL.xyz)) * (0.6 + 0.4 * n3(qm * 6.0));
  return max(smoothstep(0.34, 0.74, m), clamp(pc, 0.0, 1.0)); }   // wide, feathered patch edges
float dens(vec3 p, bool full, float covIn){
  float alt = length(p) - 1.0, hn = (alt - deck.x) / (deck.y - deck.x);
  if (hn <= 0.0 || hn >= 1.0) return 0.0;
  vec3 pl = (W2L * vec4(p, 0.0)).xyz;
  float cov = covIn >= 0.0 ? covIn : coverage(pl);
  if (cov < 0.004) return 0.0;
  vec3 q = pl * freq + wind;
  vec3 wv = vec3(n3(q * 0.12 + 4.0), n3(q * 0.12 + 1.0), n3(q * 0.12 + 9.0)) - 0.5;   // domain warp: wisps, not blobs
  vec3 qb = q * 0.45 + wv * 1.6;
  float w2 = 1.0 - smoothstep(0.2, 0.6, FP * 1.0), w3 = 1.0 - smoothstep(0.2, 0.6, FP * 9.1);
  float f = 0.45 * n3(qb) + 0.22 * mix(0.5, n3(q * 1.03 + wv * 2.0 + 3.1), w2);
  f += 0.15 * (w3 > 0.01 ? mix(0.5, n3(q * 9.1 + 7.7), w3) : 0.5);
  if (full) { float w4 = 1.0 - smoothstep(0.2, 0.6, FP * 37.0);
    f += 0.18 * (w4 > 0.01 ? mix(0.5, n3(q * 37.0 + 1.9), w4) : 0.5); }
  else f += 0.09;
  float prof = smoothstep(0.0, 0.40, hn) * smoothstep(1.0, 0.50, hn);
  float b = f - 1.0 + cover * cov;
  float d = smoothstep(-0.06, 0.50, b) * 0.70 * prof;           // soft, wide density falloff (no hard threshold)
  if (full) {   // cirrus: thin stretched filaments in the upper deck
    vec3 qc = vec3(q.x * 0.35, q.y * 2.6, q.z * 0.35) + wv * 3.0;
    float ci = smoothstep(0.58, 0.86, n3(qc * 0.5 + 6.0));
    d += 0.16 * ci * smoothstep(0.35, 0.75, hn) * smoothstep(1.0, 0.80, hn) * w2;
  }
  return d * deckK * cov; }
vec3 col; float T;
void atmo(float x0, float x1, vec3 d, float mu){
  if (x1 <= x0 || T < 0.01 || atmoK < 0.5) return;
  float prev = 0.0;
  for (int i = 0; i < 8; i++) {
    float u1 = pow((float(i) + 1.0) / 8.0, 1.7), um = pow((float(i) + 0.5) / 8.0, 1.7);
    float ds = (x1 - x0) * (u1 - prev); prev = u1;
    vec3 p = C + d * (x0 + (x1 - x0) * um);
    float r = length(p), alt = max(r - 1.0, 0.0), ex = exp(-alt / Hs);
    float ps = dot(p, sunD), perp = sqrt(max(r * r - ps * ps, 0.0));
    float vis = ps > 0.0 ? 1.0 : smoothstep(0.995, 1.004, perp);                 // planet shadow
    float tS = exp(-atmoK * Hs * ex / max(ps / r + 0.15, 0.05) * 0.5);           // sunlight left at this height
    vec3 sc = vec3(0.58, 0.50, 0.95) * 0.75 * (1.0 + mu * mu) + vec3(1.0, 0.86, 0.72) * 0.30 * pow(max(mu, 0.0), 6.0);
    vec3 sunc = mix(vec3(1.0, 0.55, 0.35), vec3(1.0), tS);
    float tr = exp(-atmoK * ex * ds);
    col += T * (1.0 - tr) * (sc * sunc * vis * (0.25 + 0.75 * tS) + vec3(0.05, 0.04, 0.09));
    T *= tr;
  } }
void main(){
  col = vec3(0.0); T = 1.0; FP = 0.0;
  float pixA = 2.0 * tanH.y * abs(dFdy(fragTexCoord.y));   // (uniform control flow here)
  vec2 s = fragTexCoord * 2.0 - 1.0;
  vec3 d = normalize(F + R * (s.x * tanH.x) - U * (s.y * tanH.y));
  float h = length(C) - 1.0, mu = dot(d, sunD);
  vec2 ig = sph(C, d, 1.0);
  float tg = ig.x > 0.0 ? ig.x : 1e9;                                   // ground
  vec2 ia = sph(C, d, 1.0 + 12.0 * Hs);
  float s0 = sMin, s1 = min(min(tg, ia.y), sMax);
  float a0 = 1e9, a1 = -1e9;
  vec2 ib = sph(C, d, 1.0 + deck.x), it = sph(C, d, 1.0 + deck.y);
  if (h < deck.x) { if (ib.y > 0.0) { a0 = ib.y; a1 = it.y; } }
  else if (h < deck.y) { a0 = 0.0; a1 = ib.x > 0.0 ? ib.x : it.y; }
  else if (it.x > 0.0) { a0 = it.x; a1 = ib.x > 0.0 ? ib.x : it.y; }
  a0 = max(a0, s0); a1 = min(min(a1, tg), s1); a1 = min(a1, a0 + 24.0 * (deck.y - deck.x));
  if (a1 > a0 && deckK > 0.001) {
    atmo(s0, a0, d, mu);
    float L = a1 - a0, th = deck.y - deck.x;
    float jit = fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));
    float ph = 0.55 + 0.9 * pow(max(mu, 0.0), 6.0) + 0.15 * (1.0 - mu);   // silver lining + back scatter
    vec3 sunCol = vec3(1.0, 0.95, 0.88), skyAmb = vec3(0.50, 0.44, 0.78);
    for (int i = 0; i < 24; i++) {
      if (float(i) >= steps || T < 0.03) break;
      float u0 = (float(i) + jit) / steps, u1 = (float(i) + 1.0 + jit) / steps;
      float tt = a0 + L * u0 * u0, ds = L * (u1 * u1 - u0 * u0);   // quadratic spacing: fine steps near the eye
      vec3 p = C + d * tt;
      FP = tt * pixA * freq;
      float cv = coverage((W2L * vec4(p, 0.0)).xyz);
      float de = cv < 0.004 ? 0.0 : dens(p, true, cv);
      if (de > 0.002) {
        float dl = dens(p + sunD * 0.3 * th, false, cv);
        float hn = clamp((length(p) - 1.0 - deck.x) / th, 0.0, 1.0);
        float li = exp(-dl * cloudK * 0.15 * th);
        float day = smoothstep(-0.10, 0.10, dot(normalize(p), sunD));   // night side of the globe: dark clouds
        float thin = 1.0 - smoothstep(0.0, 0.30, de);
        float fwd = 1.2 * pow(max(mu, 0.0), 10.0) * thin;   // forward scattering: silver-lit thin edges toward the sun
        vec3 Lc = (sunCol * ((0.12 + 1.05 * li) * ph + fwd * li) + skyAmb * (0.22 + 0.60 * hn) * (1.0 - 0.45 * de)) * mix(0.03, 1.0, day);
        float tr = exp(-de * cloudK * ds);
        col += T * (1.0 - tr) * Lc; T *= tr;
      }
    }
    atmo(a1, s1, d, mu);
  } else atmo(s0, s1, d, mu);
  o = vec4(col * gain, (1.0 - T) * gain); })";
// Space backdrop, baked ONCE at load into an equirectangular texture (GPU, same lattice noise):
// soft coloured gas (magenta / violet / teal), a brighter galactic band with dark dust lanes, faint
// unresolved-star glow. Drawn additive under the point stars, so the clear colour stays the dark.
// Cloud composite: 5-tap soft upsample of the low-res ray-march (premultiplied), no pixel blocks.
const char* FOGUP_FS = "#version 330\n"
"in vec2 fragTexCoord; in vec4 fragColor; out vec4 o; uniform sampler2D texture0; uniform vec2 px;\n"
"void main(){ vec2 t = fragTexCoord; vec2 a = px * vec2(0.85, 0.85), b = px * vec2(0.85, -0.85);\n"
"  o = texture(texture0, t) * 0.36 + (texture(texture0, t + a) + texture(texture0, t - a) + texture(texture0, t + b) + texture(texture0, t - b)) * 0.16; }\n";
const char* NEB_FS = "#version 330\n"
"in vec2 fragTexCoord; out vec4 o; uniform sampler2D texture0;\n"
U11_NOISE3 R"(
float fbm(vec3 p){ float s = 0.0, a = 0.5; for (int i = 0; i < 5; i++) { s += a * n3(p); p = p * 2.03 + vec3(1.7, 9.2, 3.1); a *= 0.5; } return s; }
void main(){
  float lon = (fragTexCoord.x - 0.5) * 6.2831853, lat = (0.5 - fragTexCoord.y) * 3.1415927;
  vec3 d = vec3(cos(lat) * cos(lon), sin(lat), cos(lat) * sin(lon));
  vec3 bn = normalize(vec3(0.30, 0.86, 0.41));
  float bh = dot(d, bn), band = exp(-bh * bh / 0.055), halo = exp(-bh * bh / 0.40);
  vec3 w = vec3(fbm(d * 1.6 + 3.0), fbm(d * 1.6 + 8.0), fbm(d * 1.6 + 13.0)) - 0.5;
  vec3 q = d * 3.0 + w * 1.8;
  float g1 = smoothstep(0.38, 0.78, fbm(q * 1.2 + 21.0));
  float g2 = smoothstep(0.42, 0.80, fbm(q * 1.7 + 5.0));
  float g3 = smoothstep(0.50, 0.85, fbm(q * 2.6 + 40.0));
  float fil = pow(1.0 - abs(2.0 * fbm(q * 4.5 + 2.0) - 1.0), 6.0);          // bright gas filaments
  float dust = smoothstep(0.45, 0.75, fbm(d * 7.0 + w * 2.5 + 30.0));        // dark lanes
  vec3 c = vec3(0.0);
  c += vec3(0.46, 0.10, 0.36) * g1 * (0.35 + 0.65 * halo);                   // magenta / H-alpha-ish
  c += vec3(0.10, 0.24, 0.46) * g2 * (0.30 + 0.70 * halo);                   // blue / teal reflection gas
  c += vec3(0.30, 0.16, 0.50) * g3 * 0.8;                                    // violet
  c += vec3(0.55, 0.42, 0.62) * fil * (g1 + g2) * 0.35;
  c += vec3(0.30, 0.27, 0.34) * band * (0.35 + 0.65 * fbm(d * 12.0 + 7.0));   // unresolved stars of the band
  c *= 1.0 - 0.85 * dust * (0.4 + 0.6 * halo);
  c *= 0.30;
  o = vec4(c, 1.0); })";
const char* SKY_FS = R"(#version 330
in vec2 fragTexCoord; out vec4 o; uniform sampler2D texture0;
uniform vec3 F; uniform vec3 R; uniform vec3 U; uniform vec2 tanH; uniform float K; uniform vec3 tint;
void main(){
  vec2 s = fragTexCoord * 2.0 - 1.0;
  vec3 d = normalize(F + R * (s.x * tanH.x) - U * (s.y * tanH.y));
  vec2 uv = vec2(atan(d.z, d.x) / 6.2831853 + 0.5, 0.5 - asin(clamp(d.y, -1.0, 1.0)) / 3.1415927);
  o = vec4(texture(texture0, uv).rgb * tint * K, 1.0); })";
const char* CLOUD_FS = R"(#version 330
in vec2 uv; in vec3 nrm; uniform sampler2D texture0; uniform vec3 sun; uniform float amb = 0.34, alphaK = 1.0; out vec4 o;
void main(){ vec4 t = texture(texture0, uv); if (t.a < 0.03) discard;
  float d = max(dot(normalize(nrm), sun), 0.0);
  o = vec4(t.rgb * (amb + (1.0 - amb) * d), t.a * 0.82 * alphaK); })";
const char* TERRAIN_FS = R"(#version 330
in vec3 nrm; in vec3 mp; uniform vec3 tint; uniform vec3 hr; out vec4 o;
uniform vec3 sunL = vec3(-0.45, 0.8, 0.35); uniform float amb = 0.30;
void main(){ vec3 n = normalize(nrm); vec3 L = normalize(sunL);
  float d = max(dot(n, L), 0.0);
  vec3 c;
  if (mp.y <= hr.z + 0.004) c = vec3(0.07, 0.17, 0.30) + vec3(0.05, 0.12, 0.18) * d;  // the water quad
  else { float h = clamp((mp.y - hr.x) / max(1e-4, hr.y - hr.x), 0.0, 1.0);
    c = mix(vec3(0.30, 0.27, 0.20), vec3(0.36, 0.38, 0.26), smoothstep(0.59, 0.66, h));  // shore -> lowland (water quad sits at h~0.59)
    c = mix(c, vec3(0.50, 0.47, 0.42), smoothstep(0.66, 0.82, h));                     // rock
    c = mix(c, vec3(0.80, 0.80, 0.84), smoothstep(0.86, 0.97, h));                     // peaks
    c *= tint * (amb + 0.90 * d); }
  o = vec4(c, 1.0); })";
// Shader terrain mirrors the supplied terrain-mesh.js algorithm. The CPU creates only the
// deterministic permutation/crater data; all vertex heights and final colours are evaluated here.
const char* TERRAIN_PROC_VS = R"(#version 330
in vec3 vertexPosition; in vec2 vertexTexCoord;
uniform mat4 mvp; uniform mat4 matModel; uniform sampler2D permutationTex;
uniform float gridSize, heightScale, frequency, ridgeSharpness, waterLevel;
uniform vec4 craters[32]; uniform int craterCount, isWater;
out vec3 worldPos; out float localHeight;
float fade5(float t){ return t*t*t*(t*(t*6.0-15.0)+10.0); }
int permAt(int i){ return int(texelFetch(permutationTex, ivec2(i & 255, 0), 0).r*255.0 + 0.5); }
float grad8(int hash, float x, float y){ int h=hash & 7; float u=h<4?x:y, v=h<4?y:x;
  return ((h & 1)==0?u:-u) + ((h & 2)==0?v:-v); }
float noise2d(vec2 q){ ivec2 c=ivec2(floor(q)); vec2 f=fract(q); int X=c.x & 255, Y=c.y & 255;
  float u=fade5(f.x), v=fade5(f.y);
  int aa=permAt(permAt(X)+Y), ab=permAt(permAt(X)+Y+1);
  int ba=permAt(permAt(X+1)+Y), bb=permAt(permAt(X+1)+Y+1);
  float x1=mix(grad8(aa,f.x,f.y),grad8(ba,f.x-1.0,f.y),u);
  float x2=mix(grad8(ab,f.x,f.y-1.0),grad8(bb,f.x-1.0,f.y-1.0),u);
  return mix(x1,x2,v); }
float terrainHeight(vec2 p){ float f=frequency*0.25;
  float h=(noise2d(p*f)+noise2d(p*f*2.3)*0.5);
  if(ridgeSharpness>0.05) h=pow(abs(h),1.0+ridgeSharpness*0.8)*sign(h);
  h*=heightScale;
  for(int i=0;i<32;i++){ if(i>=craterCount) break; vec2 d=p-craters[i].xy; float dist=length(d);
    if(dist<craters[i].z){ float t=dist/craters[i].z;
      float craterShape=(1.0-cos(t*3.141592653589793))*0.5, rim=sin(t*3.141592653589793)*0.25;
      h-=(1.0-craterShape)*craters[i].w-rim; } }
  return h; }
void main(){ vec3 p=vertexPosition; p.y=isWater!=0?waterLevel:terrainHeight(p.xz);
  localHeight=p.y; worldPos=(matModel*vec4(p,1.0)).xyz; gl_Position=mvp*vec4(p,1.0); })";
const char* TERRAIN_PROC_FS = R"(#version 330
in vec3 worldPos; in float localHeight;
uniform vec2 heightRange; uniform vec3 tint, lowColor, midColor, highColor;
uniform vec4 waterColor; uniform int isWater; out vec4 o;
uniform vec3 sunL = vec3(-0.45, 0.8, 0.35); uniform float amb = 0.30;
void main(){ vec3 n=normalize(cross(dFdx(worldPos),dFdy(worldPos))); if(n.y<0.0)n=-n;
  vec3 L=normalize(sunL); float d=max(dot(n,L),0.0);
  if(isWater!=0){ vec3 V=vec3(0.0,1.0,0.0), H=normalize(L+V);
    float spec=pow(max(dot(n,H),0.0),115.2)*(0.25+0.75*0.25);
    vec3 c=waterColor.rgb*(amb-0.02+0.72*d)+vec3(spec); o=vec4(c,waterColor.a); return; }
  float h=clamp((localHeight-heightRange.x)/max(0.0001,heightRange.y-heightRange.x),0.0,1.0);
  vec3 c=h<0.45?mix(lowColor,midColor,h/0.45):mix(midColor,highColor,(h-0.45)/0.55);
  o=vec4(c*tint*(amb+0.90*d),1.0); })";
const char* FLAME_FS = R"(#version 330
in vec2 fragTexCoord; in vec4 fragColor; uniform sampler2D texture0; uniform float time;
uniform vec4 recolor;  // rgb = enemy engineTint, a = 1 to recolour (0 = original blue-white flame)
out vec4 o;
void main(){ vec2 uv = fragTexCoord;
  float w = (sin(uv.x*18.0 - time*31.0) + 0.6*sin(uv.x*41.0 - time*53.0 + uv.y*9.0)) * 0.035 * uv.x;
  uv.y += w; uv.x = uv.x * (1.0 + 0.06*sin(time*47.0 + uv.y*12.0));
  vec4 t = texture(texture0, clamp(uv, 0.0, 1.0));
  float L = max(t.r, max(t.g, t.b)), core = pow(t.r, 6.0);      // texture: white-hot core, blue outer flame
  vec3 rc = mix(recolor.rgb * L, vec3(L), core * 0.3);           // tinted flame, keep a thin hot core
  t.rgb = mix(t.rgb, rc, recolor.a);
  t *= fragColor;
  o = vec4(t.rgb, t.a); })";
const char* HEAT_FS = R"(#version 330
in vec2 fragTexCoord; in vec4 fragColor; uniform sampler2D texture0; uniform float time; uniform vec2 screen;
out vec4 o;
void main(){ float u = fragTexCoord.x, v = fragTexCoord.y - 0.5;
  float spread = mix(0.18, 0.5, u);
  float edge = 1.0 - smoothstep(spread*0.55, spread, abs(v));
  float decay = pow(1.0 - u, 1.4) * smoothstep(0.0, 0.06, u);
  float a = edge * decay;
  vec2 off = vec2(sin(v*38.0 + u*22.0 - time*19.0) + 0.5*sin(v*71.0 - u*13.0 - time*31.0),
                  sin(u*29.0 - time*23.0 + v*17.0)) * 0.012 * a;
  vec2 suv = gl_FragCoord.xy / screen + off;
  float band = 0.5 + 0.5*sin(v*46.0 + u*30.0 - time*21.0) * sin(u*17.0 - time*9.0 + v*11.0);
  vec3 haze = vec3(0.50, 0.72, 1.0) * a * band * 0.26;
  o = vec4(texture(texture0, suv).rgb * (1.0 + 0.10*a) + haze, 1.0); })";

struct TerrainPrng {
    explicit TerrainPrng(std::int64_t seed) : state(std::llabs(seed) % 2147483647) { if (state == 0) state = 1; }
    double next() { state = (state * 16807) % 2147483647; return (double)(state - 1) / 2147483646.0; }
    std::int64_t state;
};

int terrainPerm(const unsigned char* permutation, int i) {
    i %= 256; if (i < 0) i += 256;
    return permutation[i];
}

double terrainGrad(int hash, double x, double y) {
    const int h = hash & 7;
    const double u = h < 4 ? x : y, v = h < 4 ? y : x;
    return ((h & 1) == 0 ? u : -u) + ((h & 2) == 0 ? v : -v);
}

double terrainNoise(const unsigned char* permutation, double x, double y) {
    const int fx = (int)std::floor(x), fy = (int)std::floor(y);
    const int X = fx & 255, Y = fy & 255;
    const double xf = x - fx, yf = y - fy;
    const auto fade = [](double t) { return t * t * t * (t * (t * 6 - 15) + 10); };
    const double u = fade(xf), v = fade(yf);
    const int aa = terrainPerm(permutation, terrainPerm(permutation, X) + Y);
    const int ab = terrainPerm(permutation, terrainPerm(permutation, X) + Y + 1);
    const int ba = terrainPerm(permutation, terrainPerm(permutation, X + 1) + Y);
    const int bb = terrainPerm(permutation, terrainPerm(permutation, X + 1) + Y + 1);
    const double x1 = (1 - u) * terrainGrad(aa, xf, yf) + u * terrainGrad(ba, xf - 1, yf);
    const double x2 = (1 - u) * terrainGrad(ab, xf, yf - 1) + u * terrainGrad(bb, xf - 1, yf - 1);
    return (1 - v) * x1 + v * x2;
}

double terrainHeight(const unsigned char* permutation, const Vector4* craters, int craterCount,
                     double x, double z, double heightScale, double frequency, double ridgeSharpness) {
    const double f = frequency * 0.25;
    double h = terrainNoise(permutation, x * f, z * f) + terrainNoise(permutation, x * f * 2.3, z * f * 2.3) * 0.5;
    if (ridgeSharpness > 0.05) h = std::pow(std::abs(h), 1.0 + ridgeSharpness * 0.8) * (h < 0 ? -1 : 1);
    h *= heightScale;
    for (int i = 0; i < craterCount; i++) {
        const double dx = x - craters[i].x, dz = z - craters[i].y;
        const double dist = std::sqrt(dx * dx + dz * dz);
        if (dist >= craters[i].z) continue;
        const double t = dist / craters[i].z;
        const double craterShape = (1.0 - std::cos(t * PI)) * 0.5;
        const double rim = std::sin(t * PI) * 0.25;
        h -= (1.0 - craterShape) * craters[i].w - rim;
    }
    return h;
}

Color terrainColor(const std::string& hex) {
    if (hex.size() != 7 || hex[0] != '#') throw std::runtime_error("terrain color must be #rrggbb");
    const unsigned long rgb = std::stoul(hex.substr(1), nullptr, 16);
    return {(unsigned char)(rgb >> 16), (unsigned char)(rgb >> 8), (unsigned char)rgb, 255};
}
}

void Render3D::ensureSceneRt() {
    const int w = GetScreenWidth(), h = GetScreenHeight();
    if (w < 2 || h < 2) return;
    if (scene_.id > 0 && rtW_ == w && rtH_ == h) return;
    if (scene_.id > 0) UnloadRenderTexture(scene_);
    scene_ = LoadRenderTexture(w, h);
    // Make sure we are not left drawing into the new RT (title/menus must hit the backbuffer).
    rlDrawRenderBatchActive();
    rlDisableFramebuffer();
    SetTextureFilter(scene_.texture, TEXTURE_FILTER_BILINEAR);
    rtW_ = w; rtH_ = h;
    Vector2 sc{(float)w, (float)h};
    SetShaderValue(heatSh_, hScreen_, &sc, SHADER_UNIFORM_VEC2);
}

void Render3D::init(const char* root) {
    heat_ = !getenv("NOHEAT"); flames_ = !getenv("NOFLAME");
    if (const char* e = getenv("R3D_SCALE")) shipScale_ = (float)atof(e);
    else shipScale_ = 1.8f;  // canonical demo scale
    std::string r = root ? root : "assets";
    root_ = r;
    auto path = [&](const char* rel) { return r + "/" + rel; };
    player_ = LoadModel(path("models/player_starfighter.glb").c_str());
    playerAnims_ = LoadModelAnimations(path("models/player_starfighter.glb").c_str(), &playerAnimCount_);
    for (int i = 0; i < playerAnimCount_; ++i) {
        const std::string n = playerAnims_[i].name ? playerAnims_[i].name : "";
        if (n.find("Landing ") == 0 || n.find("Landing_") == 0) playerGearAnims_.push_back(i);
        else if (n.find("GUN_SPIN_") == 0) playerGunAnims_.push_back(i);
        else if (n.find("ENGINE_EXHAUST_") == 0) playerExhaustAnims_.push_back(i);
    }
    fprintf(stderr, "R3D player model animations=%d gear=%zu guns=%zu exhaust=%zu\n", playerAnimCount_, playerGearAnims_.size(), playerGunAnims_.size(), playerExhaustAnims_.size());
    playerAnchors_ = findAnchors(player_, {-1, 0, 0}, {0, 0, 1});   // t250: red_fighter: nose -X, wings Z
    shipLightsOn_ = !getenv("NOSHIPLIGHTS");
    {   // t250: one soft round sprite for every ship light (hot core + radial falloff, no corners)
        const int N = 64;
        Image img = GenImageColor(N, N, BLANK);
        Color* px = (Color*)img.data;
        for (int y = 0; y < N; y++) for (int x = 0; x < N; x++) {
            const float dx = (x + 0.5f) / N * 2.f - 1.f, dy = (y + 0.5f) / N * 2.f - 1.f;
            const float r = sqrtf(dx * dx + dy * dy);
            const float halo = powf(fmaxf(0.f, 1.f - r), 1.7f);
            const float core = expf(-r * r * 30.f);
            const float a = fminf(1.f, 0.5f * halo + core);
            px[y * N + x] = {255, 255, 255, (unsigned char)std::lround(a * 255.f)};
        }
        lightTex_ = LoadTextureFromImage(img);
        UnloadImage(img);
        SetTextureFilter(lightTex_, TEXTURE_FILTER_BILINEAR);
        SetTextureWrap(lightTex_, TEXTURE_WRAP_CLAMP);
    }
    {   // t251: soft ring (shockwave / shield): bright crest near the rim, faint inner haze, nothing outside
        const int N = 256;
        Image img = GenImageColor(N, N, BLANK);
        Color* px = (Color*)img.data;
        for (int y = 0; y < N; y++)
            for (int x = 0; x < N; x++) {
                const float dx = (x + 0.5f) / N * 2.f - 1.f, dy = (y + 0.5f) / N * 2.f - 1.f, r = sqrtf(dx * dx + dy * dy);
                const float crest = expf(-powf((r - 0.86f) / 0.055f, 2.f)), haze = 0.28f * expf(-powf((r - 0.70f) / 0.16f, 2.f));
                const float a = r >= 0.995f ? 0.f : fminf(1.f, crest + haze);
                px[y * N + x] = {255, 255, 255, (unsigned char)std::lround(a * 255.f)};
            }
        ringTex_ = LoadTextureFromImage(img);
        UnloadImage(img);
        SetTextureFilter(ringTex_, TEXTURE_FILTER_BILINEAR);
        SetTextureWrap(ringTex_, TEXTURE_WRAP_CLAMP);
    }
    {   // Use the fighter's largest true half-axis.  This keeps the sphere compact while
        // enclosing the hull in the camera-facing views, unlike the overly conservative box diagonal.
        const BoundingBox b = GetModelBoundingBox(player_);
        const Vector3 c{(b.min.x + b.max.x) * .5f, (b.min.y + b.max.y) * .5f, (b.min.z + b.max.z) * .5f};
        shieldBaseRadius_ = 0.f;
        shieldBaseRadius_ = fmaxf(fabsf(b.max.x - c.x), fmaxf(fabsf(b.max.y - c.y), fabsf(b.max.z - c.z)));
        shieldBaseRadius_ *= 1.03f;  // just-clearance margin; the field should hug the fighter
        // Overhead: the camera sees the hull's width (x) instead of its height (y), and the shield is drawn
        // smaller (kTopDownShieldScaleMul vs kSidePlayerScaleMul). Keep the side view's gap above/below the
        // hull (radius - half height), converted to the same on-screen size, beside the hull from above.
        const float halfW = fabsf(b.max.x - c.x);
        const float halfH = fabsf(b.max.y - c.y);
        const float sideGap = shieldBaseRadius_ - halfH;
        shieldTopRadius_ = fmaxf(shieldBaseRadius_, halfW + sideGap * (kSidePlayerScaleMul / kTopDownShieldScaleMul) * kShieldTopGapMatch);
        fprintf(stderr, "SHIELD side r=%.3f top r=%.3f (half w=%.3f h=%.3f gap=%.3f)\n", shieldBaseRadius_, shieldTopRadius_, halfW, halfH, sideGap);
    }
    // enemies: the default model (index 0) is a decimated LOD (blue_racer.glb = 40k tris original,
    // kept alongside). It is loaded below, after the dent shaders exist; per-type models via enemyModel().
    std::string defaultEnemy;
    {
        const char* em = getenv("ENEMY_MODEL") ? getenv("ENEMY_MODEL") : "blue_racer_lod4k.glb";
        defaultEnemy = path((std::string("models/") + em).c_str());
        if (!FileExists(defaultEnemy.c_str())) defaultEnemy = path("models/blue_racer.glb");
    }
    direct_ = !getenv("SHIPRT"); hidePlayer_ = getenv("HIDEPLAYER3D") != nullptr;
    flameTex_ = LoadTexture(path("models/flame_billboard.png").c_str());
    SetTextureFilter(flameTex_, TEXTURE_FILTER_BILINEAR);
    SetTextureWrap(flameTex_, TEXTURE_WRAP_CLAMP);
    flameSh_ = LoadShaderFromMemory(nullptr, FLAME_FS);
    heatSh_ = LoadShaderFromMemory(nullptr, HEAT_FS);
    lit_ = LoadShaderFromMemory(LIT_VS, LIT_FS);
    lit_.locs[SHADER_LOC_VERTEX_POSITION] = GetShaderLocationAttrib(lit_, "vertexPosition");
    lit_.locs[SHADER_LOC_VERTEX_TEXCOORD01] = GetShaderLocationAttrib(lit_, "vertexTexCoord");
    lit_.locs[SHADER_LOC_VERTEX_NORMAL] = GetShaderLocationAttrib(lit_, "vertexNormal");
    lit_.locs[SHADER_LOC_MATRIX_MVP] = GetShaderLocation(lit_, "mvp");
    lit_.locs[SHADER_LOC_MATRIX_NORMAL] = GetShaderLocation(lit_, "matNormal");
    lit_.locs[SHADER_LOC_COLOR_DIFFUSE] = GetShaderLocation(lit_, "colDiffuse");
    lit_.locs[SHADER_LOC_MAP_DIFFUSE] = GetShaderLocation(lit_, "texture0");
    titleGlyphSh_ = LoadShaderFromMemory(LIT_VS, TITLE_GLYPH_FS);
    titleGlyphSh_.locs[SHADER_LOC_VERTEX_POSITION] = GetShaderLocationAttrib(titleGlyphSh_, "vertexPosition");
    titleGlyphSh_.locs[SHADER_LOC_VERTEX_TEXCOORD01] = GetShaderLocationAttrib(titleGlyphSh_, "vertexTexCoord");
    titleGlyphSh_.locs[SHADER_LOC_VERTEX_NORMAL] = GetShaderLocationAttrib(titleGlyphSh_, "vertexNormal");
    titleGlyphSh_.locs[SHADER_LOC_MATRIX_MVP] = GetShaderLocation(titleGlyphSh_, "mvp");
    titleGlyphSh_.locs[SHADER_LOC_MATRIX_NORMAL] = GetShaderLocation(titleGlyphSh_, "matNormal");
    titleGlyphSh_.locs[SHADER_LOC_COLOR_DIFFUSE] = GetShaderLocation(titleGlyphSh_, "colDiffuse");
    titleGlyphSh_.locs[SHADER_LOC_MAP_DIFFUSE] = GetShaderLocation(titleGlyphSh_, "texture0");
    Vector3 L = Vector3Normalize(Vector3{-0.4f, -0.8f, -0.45f});
    SetShaderValue(lit_, GetShaderLocation(lit_, "lightDir"), &L, SHADER_UNIFORM_VEC3);
    metal_ = LoadShaderFromMemory(METAL_VS, METAL_FS);
    metal_.locs[SHADER_LOC_VERTEX_POSITION] = GetShaderLocationAttrib(metal_, "vertexPosition");
    metal_.locs[SHADER_LOC_VERTEX_TEXCOORD01] = GetShaderLocationAttrib(metal_, "vertexTexCoord");
    metal_.locs[SHADER_LOC_VERTEX_NORMAL] = GetShaderLocationAttrib(metal_, "vertexNormal");
    metal_.locs[SHADER_LOC_MATRIX_MVP] = GetShaderLocation(metal_, "mvp");
    metal_.locs[SHADER_LOC_MATRIX_MODEL] = GetShaderLocation(metal_, "matModel");
    metal_.locs[SHADER_LOC_MATRIX_NORMAL] = GetShaderLocation(metal_, "matNormal");
    metal_.locs[SHADER_LOC_COLOR_DIFFUSE] = GetShaderLocation(metal_, "colDiffuse");
    metal_.locs[SHADER_LOC_MAP_DIFFUSE] = GetShaderLocation(metal_, "texture0");
    metalEye_ = GetShaderLocation(metal_, "eye");
    metalRoughness_ = GetShaderLocation(metal_, "roughness");
    metalness_ = GetShaderLocation(metal_, "metalness");
    metalEnvStrength_ = GetShaderLocation(metal_, "envStrength");
    metalOpacity_ = GetShaderLocation(metal_, "opacity");
    SetShaderValue(metal_, GetShaderLocation(metal_, "lightDir"), &L, SHADER_UNIFORM_VEC3);
    playerGlassSh_ = LoadShaderFromMemory(METAL_VS, METAL_FS);
    playerGlassSh_.locs[SHADER_LOC_VERTEX_POSITION] = GetShaderLocationAttrib(playerGlassSh_, "vertexPosition");
    playerGlassSh_.locs[SHADER_LOC_VERTEX_TEXCOORD01] = GetShaderLocationAttrib(playerGlassSh_, "vertexTexCoord");
    playerGlassSh_.locs[SHADER_LOC_VERTEX_NORMAL] = GetShaderLocationAttrib(playerGlassSh_, "vertexNormal");
    playerGlassSh_.locs[SHADER_LOC_MATRIX_MVP] = GetShaderLocation(playerGlassSh_, "mvp");
    playerGlassSh_.locs[SHADER_LOC_MATRIX_MODEL] = GetShaderLocation(playerGlassSh_, "matModel");
    playerGlassSh_.locs[SHADER_LOC_MATRIX_NORMAL] = GetShaderLocation(playerGlassSh_, "matNormal");
    playerGlassSh_.locs[SHADER_LOC_COLOR_DIFFUSE] = GetShaderLocation(playerGlassSh_, "colDiffuse");
    playerGlassSh_.locs[SHADER_LOC_MAP_DIFFUSE] = GetShaderLocation(playerGlassSh_, "texture0");
    glassEye_ = GetShaderLocation(playerGlassSh_, "eye");
    glassMetalness_ = GetShaderLocation(playerGlassSh_, "metalness");
    glassRoughness_ = GetShaderLocation(playerGlassSh_, "roughness");
    glassEnvStrength_ = GetShaderLocation(playerGlassSh_, "envStrength");
    glassOpacity_ = GetShaderLocation(playerGlassSh_, "opacity");
    SetShaderValue(playerGlassSh_, GetShaderLocation(playerGlassSh_, "lightDir"), &L, SHADER_UNIFORM_VEC3);
    for (int i = 0; i < player_.materialCount; i++) {
        player_.materials[i].shader = lit_;
    }
    // The imported GLB keeps separate metallic and canopy materials. Give those surfaces
    // the existing solar specular shader; preserve their authored base colors.
    for (int i : {1, 3, 7}) {
        if (i < player_.materialCount) player_.materials[i].shader = metal_;
    }
    for (int i : {2, 9}) {
        if (i < player_.materialCount) player_.materials[i].shader = playerGlassSh_;
    }
    shield_ = LoadModelFromMesh(GenMeshSphere(1.f, 32, 24));
    shieldSh_ = LoadShaderFromMemory(SHIELD_VS, SHIELD_FS);
    shieldSh_.locs[SHADER_LOC_VERTEX_POSITION] = GetShaderLocationAttrib(shieldSh_, "vertexPosition");
    shieldSh_.locs[SHADER_LOC_VERTEX_NORMAL] = GetShaderLocationAttrib(shieldSh_, "vertexNormal");
    shieldSh_.locs[SHADER_LOC_MATRIX_MVP] = GetShaderLocation(shieldSh_, "mvp");
    shieldSh_.locs[SHADER_LOC_MATRIX_MODEL] = GetShaderLocation(shieldSh_, "matModel");
    shieldSh_.locs[SHADER_LOC_MATRIX_NORMAL] = GetShaderLocation(shieldSh_, "matNormal");
    shieldTime_ = GetShaderLocation(shieldSh_, "time");
    shieldCharge_ = GetShaderLocation(shieldSh_, "charge");
    shieldEye_ = GetShaderLocation(shieldSh_, "eye");
    shieldHitsLoc_ = GetShaderLocation(shieldSh_, "hits");
    shield_.materials[0].shader = shieldSh_;
    pickupSh_ = LoadShaderFromMemory(PICKUP_VS, PICKUP_FS);
    pickupSh_.locs[SHADER_LOC_VERTEX_POSITION] = GetShaderLocationAttrib(pickupSh_, "vertexPosition");
    pickupSh_.locs[SHADER_LOC_VERTEX_TEXCOORD01] = GetShaderLocationAttrib(pickupSh_, "vertexTexCoord");
    pickupSh_.locs[SHADER_LOC_VERTEX_NORMAL] = GetShaderLocationAttrib(pickupSh_, "vertexNormal");
    pickupSh_.locs[SHADER_LOC_MATRIX_MVP] = GetShaderLocation(pickupSh_, "mvp");
    pickupSh_.locs[SHADER_LOC_MATRIX_NORMAL] = GetShaderLocation(pickupSh_, "matNormal");
    pickupSh_.locs[SHADER_LOC_COLOR_DIFFUSE] = GetShaderLocation(pickupSh_, "colDiffuse");
    pickupSh_.locs[SHADER_LOC_MAP_DIFFUSE] = GetShaderLocation(pickupSh_, "texture0");
    puLocOpacity_ = GetShaderLocation(pickupSh_, "opacity");
    puLocFlash_ = GetShaderLocation(pickupSh_, "flash");
    puLocDissolve_ = GetShaderLocation(pickupSh_, "dissolve");
    puLocAccent_ = GetShaderLocation(pickupSh_, "accent");
    SetShaderValue(pickupSh_, GetShaderLocation(pickupSh_, "lightDir"), &L, SHADER_UNIFORM_VEC3);
    static const char* pickupNames[kPickupTypes] = {"weapon", "missiles", "shield", "repair", "rapid"};
    pickupOk_ = pickupSh_.id > 0;
    for (int p = 0; p < kPickupTypes; p++) {
        const std::string file = path((std::string("models/pickups/pickup_") + pickupNames[p] + ".glb").c_str());
        pickup_[p] = LoadModel(file.c_str());
        pickupAnim_[p] = LoadModelAnimations(file.c_str(), &pickupAnimCount_[p]);
        const bool valid = pickup_[p].meshCount > 0 && pickup_[p].boneCount == 2 && pickupAnim_[p] &&
                           pickupAnimCount_[p] == 1 && pickupAnim_[p][0].boneCount == 2 &&
                           std::strcmp(pickupAnim_[p][0].name, "Pickup_Spin") == 0 &&
                           IsModelAnimationValid(pickup_[p], pickupAnim_[p][0]);
        pickupOk_ = pickupOk_ && valid;
        pickupBounds_[p] = pickup_[p].meshCount > 0 ? GetModelBoundingBox(pickup_[p]) : BoundingBox{};
        int tris = 0; for (int m = 0; m < pickup_[p].meshCount; m++) tris += pickup_[p].meshes[m].triangleCount;
        for (int m = 0; m < pickup_[p].materialCount; m++) {
            pickup_[p].materials[m].shader = pickupSh_;
            pickup_[p].materials[m].maps[MATERIAL_MAP_DIFFUSE].color = WHITE;
        }
        fprintf(stderr, "PICKUP3D %s valid=%d meshes=%d tris=%d bones=%d anims=%d frames=%d bbox=(%.2f,%.2f,%.2f)-(%.2f,%.2f,%.2f)\n",
                pickupNames[p], valid, pickup_[p].meshCount, tris, pickup_[p].boneCount, pickupAnimCount_[p],
                pickupAnim_[p] ? pickupAnim_[p][0].frameCount : 0,
                pickupBounds_[p].min.x, pickupBounds_[p].min.y, pickupBounds_[p].min.z,
                pickupBounds_[p].max.x, pickupBounds_[p].max.y, pickupBounds_[p].max.z);
    }
    dentSh_ = LoadShaderFromMemory(DENT_VS, DENT_FS);
    dentSh_.locs[SHADER_LOC_VERTEX_POSITION] = GetShaderLocationAttrib(dentSh_, "vertexPosition");
    dentSh_.locs[SHADER_LOC_VERTEX_TEXCOORD01] = GetShaderLocationAttrib(dentSh_, "vertexTexCoord");
    dentSh_.locs[SHADER_LOC_VERTEX_NORMAL] = GetShaderLocationAttrib(dentSh_, "vertexNormal");
    dentSh_.locs[SHADER_LOC_MATRIX_MVP] = GetShaderLocation(dentSh_, "mvp");
    dentSh_.locs[SHADER_LOC_MATRIX_NORMAL] = GetShaderLocation(dentSh_, "matNormal");
    dentSh_.locs[SHADER_LOC_COLOR_DIFFUSE] = GetShaderLocation(dentSh_, "colDiffuse");
    dentSh_.locs[SHADER_LOC_MAP_DIFFUSE] = GetShaderLocation(dentSh_, "texture0");
    SetShaderValue(dentSh_, GetShaderLocation(dentSh_, "lightDir"), &L, SHADER_UNIFORM_VEC3);
    dLocDents_ = GetShaderLocation(dentSh_, "dents"); dLocN_ = GetShaderLocation(dentSh_, "nDents");
    dLocSurfaceFx_ = GetShaderLocation(dentSh_, "surfaceFx");
    loadBreakCfg(path("fx/enemy_breakup.json"));
    SetShaderValue(dentSh_, GetShaderLocation(dentSh_, "dentRadius"), &bc_.dentRadius, SHADER_UNIFORM_FLOAT);
    SetShaderValue(dentSh_, GetShaderLocation(dentSh_, "dentMaxDepth"), &bc_.dentMaxDepth, SHADER_UNIFORM_FLOAT);
    SetShaderValue(dentSh_, GetShaderLocation(dentSh_, "dentDarken"), &bc_.dentDarken, SHADER_UNIFORM_FLOAT);
    if (dentSh_.id == 0) dentSh_ = lit_;  // fallback: no dents
    dLocGlow_ = GetShaderLocation(dentSh_, "glowBoost"); dLocTime_ = GetShaderLocation(dentSh_, "time");
    dentSkinSh_ = LoadShaderFromMemory(DENT_SKIN_VS, DENT_FS);
    if (dentSkinSh_.id > 0) {
        Shader& k = dentSkinSh_;
        k.locs[SHADER_LOC_VERTEX_POSITION] = GetShaderLocationAttrib(k, "vertexPosition");
        k.locs[SHADER_LOC_VERTEX_TEXCOORD01] = GetShaderLocationAttrib(k, "vertexTexCoord");
        k.locs[SHADER_LOC_VERTEX_NORMAL] = GetShaderLocationAttrib(k, "vertexNormal");
        k.locs[SHADER_LOC_VERTEX_BONEIDS] = GetShaderLocationAttrib(k, "vertexBoneIds");
        k.locs[SHADER_LOC_VERTEX_BONEWEIGHTS] = GetShaderLocationAttrib(k, "vertexBoneWeights");
        k.locs[SHADER_LOC_BONE_MATRICES] = GetShaderLocation(k, "boneMatrices");
        k.locs[SHADER_LOC_MATRIX_MVP] = GetShaderLocation(k, "mvp");
        k.locs[SHADER_LOC_MATRIX_NORMAL] = GetShaderLocation(k, "matNormal");
        k.locs[SHADER_LOC_COLOR_DIFFUSE] = GetShaderLocation(k, "colDiffuse");
        k.locs[SHADER_LOC_MAP_DIFFUSE] = GetShaderLocation(k, "texture0");
        SetShaderValue(k, GetShaderLocation(k, "lightDir"), &L, SHADER_UNIFORM_VEC3);
        SetShaderValue(k, GetShaderLocation(k, "dentRadius"), &bc_.dentRadius, SHADER_UNIFORM_FLOAT);
        SetShaderValue(k, GetShaderLocation(k, "dentMaxDepth"), &bc_.dentMaxDepth, SHADER_UNIFORM_FLOAT);
        SetShaderValue(k, GetShaderLocation(k, "dentDarken"), &bc_.dentDarken, SHADER_UNIFORM_FLOAT);
        dsLocDents_ = GetShaderLocation(k, "dents"); dsLocN_ = GetShaderLocation(k, "nDents");
        dsLocSurfaceFx_ = GetShaderLocation(k, "surfaceFx");
        dsLocGlow_ = GetShaderLocation(k, "glowBoost"); dsLocTime_ = GetShaderLocation(k, "time");
    }
    {   // index 0: default enemy model (every enemy JSON without a "model" key)
        em_.clear(); emIndex_.clear();
        em_.emplace_back();
        loadEnemyModel(em_[0], defaultEnemy);
        emIndex_[""] = 0;
    }
    planetSh_ = LoadShaderFromMemory(BG_VS, PLANET_FS);
    cloudSh_ = LoadShaderFromMemory(BG_VS, CLOUD_FS);
    terrainSh_ = LoadShaderFromMemory(BG_VS, TERRAIN_FS);
    terrainProcSh_ = LoadShaderFromMemory(TERRAIN_PROC_VS, TERRAIN_PROC_FS);
    titlePlanetSh_ = LoadShaderFromMemory(TITLE_PLANET_VS, TITLE_PLANET_FS);
    titleShellSh_ = LoadShaderFromMemory(TITLE_SHELL_VS, TITLE_SHELL_FS);
    for (Shader* sh : {&planetSh_, &cloudSh_, &terrainSh_}) {
        sh->locs[SHADER_LOC_VERTEX_POSITION] = GetShaderLocationAttrib(*sh, "vertexPosition");
        sh->locs[SHADER_LOC_VERTEX_TEXCOORD01] = GetShaderLocationAttrib(*sh, "vertexTexCoord");
        sh->locs[SHADER_LOC_VERTEX_NORMAL] = GetShaderLocationAttrib(*sh, "vertexNormal");
        sh->locs[SHADER_LOC_MATRIX_MVP] = GetShaderLocation(*sh, "mvp");
        sh->locs[SHADER_LOC_MATRIX_NORMAL] = GetShaderLocation(*sh, "matNormal");
        sh->locs[SHADER_LOC_MAP_DIFFUSE] = GetShaderLocation(*sh, "texture0");
    }
    for (Shader* sh : {&titlePlanetSh_, &titleShellSh_}) {
        sh->locs[SHADER_LOC_VERTEX_POSITION] = GetShaderLocationAttrib(*sh, "vertexPosition");
        sh->locs[SHADER_LOC_VERTEX_TEXCOORD01] = GetShaderLocationAttrib(*sh, "vertexTexCoord");
        sh->locs[SHADER_LOC_VERTEX_NORMAL] = GetShaderLocationAttrib(*sh, "vertexNormal");
        sh->locs[SHADER_LOC_MATRIX_MVP] = GetShaderLocation(*sh, "mvp");
        sh->locs[SHADER_LOC_MATRIX_NORMAL] = GetShaderLocation(*sh, "matNormal");
        sh->locs[SHADER_LOC_COLOR_DIFFUSE] = GetShaderLocation(*sh, "colDiffuse");
        sh->locs[SHADER_LOC_MAP_DIFFUSE] = GetShaderLocation(*sh, "texture0");
    }
    titlePlanetSh_.locs[SHADER_LOC_VERTEX_TANGENT] = GetShaderLocationAttrib(titlePlanetSh_, "vertexTangent");
    for (int i = 1; i <= MATERIAL_MAP_HEIGHT; ++i) {
        const std::string sampler = "texture" + std::to_string(i);
        titlePlanetSh_.locs[SHADER_LOC_MAP_DIFFUSE + i] = GetShaderLocation(titlePlanetSh_, sampler.c_str());
    }
    titlePlanetRoughnessLoc_ = GetShaderLocation(titlePlanetSh_, "roughnessFactor");
    titlePlanetNormalScaleLoc_ = GetShaderLocation(titlePlanetSh_, "normalScale");
    titlePlanetEyeLoc_ = GetShaderLocation(titlePlanetSh_, "eye");
    titlePlanetSunLoc_ = GetShaderLocation(titlePlanetSh_, "sun");
    titleShellSunLoc_ = GetShaderLocation(titleShellSh_, "sun");
    fprintf(stderr, "TITLE planet shaders ok=%d/%d\n",
            titlePlanetSh_.id > 0 && titlePlanetSh_.id != rlGetShaderIdDefault(),
            titleShellSh_.id > 0 && titleShellSh_.id != rlGetShaderIdDefault());
    pLocCol_ = GetShaderLocation(planetSh_, "baseCol"); pLocR_ = GetShaderLocation(planetSh_, "rr");
    atmoSh_ = LoadShaderFromMemory(ATMO_VS, ATMO_FS);
    if (atmoSh_.id > 0 && atmoSh_.id != rlGetShaderIdDefault()) {
        atmoSh_.locs[SHADER_LOC_VERTEX_POSITION] = GetShaderLocationAttrib(atmoSh_, "vertexPosition");
        atmoSh_.locs[SHADER_LOC_MATRIX_MVP] = GetShaderLocation(atmoSh_, "mvp");
        atmoSh_.locs[SHADER_LOC_MATRIX_MODEL] = GetShaderLocation(atmoSh_, "matModel");
        aLocCam_ = GetShaderLocation(atmoSh_, "camPos"); aLocSun_ = GetShaderLocation(atmoSh_, "sun");
        aLocRp_ = GetShaderLocation(atmoSh_, "Rp"); aLocRa_ = GetShaderLocation(atmoSh_, "Ra");
        aLocGain_ = GetShaderLocation(atmoSh_, "gain"); aLocLobe_ = GetShaderLocation(atmoSh_, "lobe");
        aLocDK_ = GetShaderLocation(atmoSh_, "dK");
        atmoMesh_ = GenMeshSphere(1.f, 32, 48);   // ~3k tris: one shell, cheap on the tablet
        atmoMat_ = LoadMaterialDefault(); atmoMat_.shader = atmoSh_;
        atmoOk_ = atmoMesh_.vertexCount > 0;
    }
    fprintf(stderr, "ATMO shell ok=%d\n", (int)atmoOk_);
    {   // Liftoff: procedural sun, prominences and volumetric clouds share one 3D value-noise lattice
        // (R = lattice, G = R shifted by (37,17): one bilinear fetch per octave gives two z layers).
        Image ni = GenImageColor(256, 256, BLANK);
        Color* px = (Color*)ni.data;
        auto hsh = [](int x, int y, uint32_t salt) {
            uint32_t h = (uint32_t)(x & 255) * 374761393u + (uint32_t)(y & 255) * 668265263u + salt * 2246822519u;
            h = (h ^ (h >> 13)) * 1274126177u; return (unsigned char)((h ^ (h >> 16)) & 255u); };
        for (int y = 0; y < 256; y++) for (int x = 0; x < 256; x++)
            px[y * 256 + x] = {hsh(x, y, 1), hsh(x + 37, y + 17, 1), hsh(x, y, 7), 255};
        noiseTex_ = LoadTextureFromImage(ni); UnloadImage(ni);
        SetTextureFilter(noiseTex_, TEXTURE_FILTER_BILINEAR); SetTextureWrap(noiseTex_, TEXTURE_WRAP_REPEAT);
        sunSh_ = LoadShaderFromMemory(nullptr, SUN_FS);
        promSh_ = LoadShaderFromMemory(nullptr, PROM_FS);
        fogSh_ = LoadShaderFromMemory(nullptr, VOLFOG_FS);
        auto okSh = [](const Shader& x) { return x.id > 0 && x.id != rlGetShaderIdDefault(); };
        sunOk_ = noiseTex_.id > 0 && okSh(sunSh_) && okSh(promSh_);
        fogOk_ = noiseTex_.id > 0 && okSh(fogSh_);
        fogUpSh_ = LoadShaderFromMemory(nullptr, FOGUP_FS);
        if (!okSh(fogUpSh_)) fogUpSh_ = Shader{}; else fogUpPx_ = GetShaderLocation(fogUpSh_, "px");
        if (sunOk_) {
            sLocTime_ = GetShaderLocation(sunSh_, "time"); sLocAA_ = GetShaderLocation(sunSh_, "aa");
            sLocQ_ = GetShaderLocation(sunSh_, "Q"); sLocRot_ = GetShaderLocation(sunSh_, "rot");
            prLocTime_ = GetShaderLocation(promSh_, "time");
        }
        if (fogOk_) {
            const char* nm[] = {"C", "F", "R", "U", "sunD", "wind", "W2L", "tanH", "deck", "cover", "cloudK", "atmoK", "Hs",
                                "sMin", "sMax", "gain", "deckK", "freq", "steps", "patchL"};
            for (int i = 0; i < (int)(sizeof(nm) / sizeof(nm[0])); i++) fLoc_[i] = GetShaderLocation(fogSh_, nm[i]);
        }
    }
    if (noiseTex_.id > 0) {   // bake the nebula / gas sky once (GPU, ~instant), equirectangular 2048 x 1024
        Shader nb = LoadShaderFromMemory(nullptr, NEB_FS);
        skySh_ = LoadShaderFromMemory(nullptr, SKY_FS);
        auto okSh = [](const Shader& x) { return x.id > 0 && x.id != rlGetShaderIdDefault(); };
        if (okSh(nb) && okSh(skySh_)) {
            static RenderTexture2D neb = LoadRenderTexture(2048, 1024);
            BeginTextureMode(neb);
            ClearBackground(BLACK);
            BeginShaderMode(nb);
            DrawTexturePro(noiseTex_, {0, 0, (float)noiseTex_.width, (float)noiseTex_.height}, {0, 0, 2048, 1024}, {0, 0}, 0.f, WHITE);
            EndShaderMode();
            EndTextureMode();
            nebTex_ = neb.texture;
            SetTextureFilter(nebTex_, TEXTURE_FILTER_BILINEAR); SetTextureWrap(nebTex_, TEXTURE_WRAP_REPEAT);
            const char* nm[] = {"F", "R", "U", "tanH", "K", "tint"};
            for (int i = 0; i < 6; i++) skyLoc_[i] = GetShaderLocation(skySh_, nm[i]);
            skyOk_ = nebTex_.id > 0;
        }
        if (nb.id > 0 && nb.id != rlGetShaderIdDefault()) UnloadShader(nb);
    }
    fprintf(stderr, "SUN procedural ok=%d VOLFOG ok=%d NEBULA ok=%d\n", (int)sunOk_, (int)fogOk_, (int)skyOk_);
    tLocTint_ = GetShaderLocation(terrainSh_, "tint"); tLocH_ = GetShaderLocation(terrainSh_, "hr");
    terrainProcSh_.locs[SHADER_LOC_VERTEX_POSITION] = GetShaderLocationAttrib(terrainProcSh_, "vertexPosition");
    terrainProcSh_.locs[SHADER_LOC_VERTEX_TEXCOORD01] = GetShaderLocationAttrib(terrainProcSh_, "vertexTexCoord");
    terrainProcSh_.locs[SHADER_LOC_MATRIX_MVP] = GetShaderLocation(terrainProcSh_, "mvp");
    terrainProcSh_.locs[SHADER_LOC_MATRIX_MODEL] = GetShaderLocation(terrainProcSh_, "matModel");
    tpLocPerm_ = GetShaderLocation(terrainProcSh_, "permutationTex");
    tpLocGrid_ = GetShaderLocation(terrainProcSh_, "gridSize");
    tpLocHeight_ = GetShaderLocation(terrainProcSh_, "heightScale");
    tpLocFreq_ = GetShaderLocation(terrainProcSh_, "frequency");
    tpLocRidge_ = GetShaderLocation(terrainProcSh_, "ridgeSharpness");
    tpLocCraters_ = GetShaderLocation(terrainProcSh_, "craters");
    tpLocCraterCount_ = GetShaderLocation(terrainProcSh_, "craterCount");
    tpLocRange_ = GetShaderLocation(terrainProcSh_, "heightRange");
    tpLocTint_ = GetShaderLocation(terrainProcSh_, "tint");
    tpLocLow_ = GetShaderLocation(terrainProcSh_, "lowColor");
    tpLocMid_ = GetShaderLocation(terrainProcSh_, "midColor");
    tpLocHigh_ = GetShaderLocation(terrainProcSh_, "highColor");
    tpLocWater_ = GetShaderLocation(terrainProcSh_, "waterColor");
    tpLocWaterLevel_ = GetShaderLocation(terrainProcSh_, "waterLevel");
    tpLocIsWater_ = GetShaderLocation(terrainProcSh_, "isWater");
    fTime_ = GetShaderLocation(flameSh_, "time");
    fRecolor_ = GetShaderLocation(flameSh_, "recolor");
    hTime_ = GetShaderLocation(heatSh_, "time");
    hScreen_ = GetShaderLocation(heatSh_, "screen");
    // Defer scene RT to beginFrame so title/menus keep the default framebuffer (LoadRenderTexture
    // can leave an FBO bound on this driver, which made the title screen pure black).
    shipTris_ = 0;
    for (int i = 0; i < player_.meshCount; i++) shipTris_ += player_.meshes[i].triangleCount;
    ok_ = player_.meshCount > 0 && flameTex_.id > 0 && lit_.id > 0;
    fprintf(stderr, "RENDER3D ok=%d tris=%d scale=%.2f heat=%d (demo-pipeline)\n",
            ok_, shipTris_, shipScale_, heat_);
}

void Render3D::shutdown() {
    if (!ok_) return;
    planet_.reset();   // joins its worker threads, frees chunk GPU data
    for (auto& [name, b] : bgModels_) {
        (void)name;
        if (b.m.meshCount > 0) UnloadModel(b.m);
        if (b.water.meshCount > 0) UnloadModel(b.water);
        if (b.permutation.id > 0) UnloadTexture(b.permutation);
    }
    bgModels_.clear();
    pickupOk_ = false;
    for (int p = 0; p < kPickupTypes; p++) {
        if (pickupAnim_[p]) UnloadModelAnimations(pickupAnim_[p], pickupAnimCount_[p]);
        pickupAnim_[p] = nullptr; pickupAnimCount_[p] = 0;
        if (pickup_[p].meshCount > 0) UnloadModel(pickup_[p]);
        pickup_[p] = {};
    }
    for (auto& E : em_) {
        if (E.anims) UnloadModelAnimations(E.anims, E.animCount);
        for (auto& c : E.chunks) UnloadMesh(c.mesh);
        if (E.m.meshCount > 0) UnloadModel(E.m);
    }
    em_.clear(); emIndex_.clear();
    if (playerAnims_) UnloadModelAnimations(playerAnims_, playerAnimCount_);
    UnloadModel(player_); UnloadModel(shield_);
    if (dentSkinSh_.id > 0) UnloadShader(dentSkinSh_);
    UnloadTexture(flameTex_);
    if (lightTex_.id > 0) UnloadTexture(lightTex_);
    lightTex_ = {};
    if (ringTex_.id > 0) UnloadTexture(ringTex_);
    ringTex_ = {};
    UnloadShader(flameSh_); UnloadShader(heatSh_); UnloadShader(shieldSh_); if (pickupSh_.id > 0) UnloadShader(pickupSh_);
    if (metal_.id > 0) UnloadShader(metal_);
    if (playerGlassSh_.id > 0) UnloadShader(playerGlassSh_);
    if (dentSh_.id != lit_.id) UnloadShader(dentSh_); UnloadShader(lit_);
    if (titleGlyphSh_.id > 0 && titleGlyphSh_.id != rlGetShaderIdDefault()) UnloadShader(titleGlyphSh_);
    UnloadShader(planetSh_); UnloadShader(cloudSh_); UnloadShader(terrainSh_); UnloadShader(terrainProcSh_);
    if (titlePlanetSh_.id > 0 && titlePlanetSh_.id != rlGetShaderIdDefault()) UnloadShader(titlePlanetSh_);
    if (titleShellSh_.id > 0 && titleShellSh_.id != rlGetShaderIdDefault()) UnloadShader(titleShellSh_);
    if (atmoOk_) { UnloadMesh(atmoMesh_); atmoMat_.shader = {}; MemFree(atmoMat_.maps); UnloadShader(atmoSh_); atmoOk_ = false; }
    for (Shader* x : {&sunSh_, &promSh_, &fogSh_}) { if (x->id > 0 && x->id != rlGetShaderIdDefault()) UnloadShader(*x); *x = {}; }
    for (auto& rt : fogRt_) { if (rt.id > 0) UnloadRenderTexture(rt); rt = {}; }
    if (noiseTex_.id > 0) UnloadTexture(noiseTex_);
    if (skySh_.id > 0 && skySh_.id != rlGetShaderIdDefault()) UnloadShader(skySh_);
    if (fogUpSh_.id > 0) UnloadShader(fogUpSh_);
    skySh_ = {}; skyOk_ = false;   // (the baked nebula RT lives until exit)
    noiseTex_ = {}; sunOk_ = fogOk_ = fogValid_ = false;
    if (scene_.id > 0) UnloadRenderTexture(scene_);
    ok_ = false;
}

// ============================================================ per-type enemy models
bool Render3D::loadEnemyModel(EnemyModel& E, const std::string& p) {
    E.file = p;
    if (!FileExists(p.c_str())) { fprintf(stderr, "R3D enemy model missing %s\n", p.c_str()); return false; }
    E.m = LoadModel(p.c_str());
    if (E.m.meshCount <= 0) return false;
    E.lights = findAnchors(E.m, {0, 0, 1}, {1, 0, 0});   // t250: enemy model space: nose +Z, wings X
    int t = 0; for (int i = 0; i < E.m.meshCount; i++) t += E.m.meshes[i].triangleCount;
    // Optional sidecar <model>.json (written by tools/blender_enemies/pipeline.py): engine nozzles,
    // weapon muzzle (glTF model space, nose +Z) and the Idle/Fire clip names.
    std::string idleName = "Idle", fireName = "Fire";
    std::string side = p.substr(0, p.size() - 4) + ".json";
    if (FileExists(side.c_str())) {
        try {
            std::ifstream in(side); nlohmann::json j; in >> j;
            if (j.contains("nozzles")) for (int i = 0; i < 2 && i < (int)j["nozzles"].size(); i++)
                E.noz[i] = {j["nozzles"][i][0].get<float>(), j["nozzles"][i][1].get<float>(), j["nozzles"][i][2].get<float>()};
            if (j.contains("muzzle")) E.muzzle = {j["muzzle"][0].get<float>(), j["muzzle"][1].get<float>(), j["muzzle"][2].get<float>()};
            idleName = j.value("idle", idleName); fireName = j.value("fire", fireName);
            if (j.contains("fire_sustain")) { E.sustain0 = j["fire_sustain"][0].get<float>(); E.sustain1 = j["fire_sustain"][1].get<float>(); }
        } catch (const std::exception& ex) { fprintf(stderr, "R3D enemy sidecar %s: %s\n", side.c_str(), ex.what()); }
    }
    bool hasBones = false;
    for (int i = 0; i < E.m.meshCount; i++) hasBones |= E.m.meshes[i].boneMatrices != nullptr;
    if (hasBones && dentSkinSh_.id > 0) {
        E.anims = LoadModelAnimations(p.c_str(), &E.animCount);
        for (int a = 0; a < E.animCount; a++) {
            if (!IsModelAnimationValid(E.m, E.anims[a])) continue;
            if (idleName == E.anims[a].name) E.idle = a;
            if (fireName == E.anims[a].name) E.fire = a;
        }
        E.skinned = E.m.boneCount > 0 && E.m.boneCount <= 16;
        E.weaponBone.assign(E.m.boneCount, 0);
        for (int b = 0; b < E.m.boneCount; b++) E.weaponBone[b] = std::strncmp(E.m.bones[b].name, "W_", 2) == 0;
        E.scratch.assign(E.m.boneCount, MatrixIdentity());
    }
    for (int i = 0; i < E.m.materialCount; i++) {
        E.m.materials[i].shader = E.skinned ? dentSkinSh_ : dentSh_;
        E.m.materials[i].maps[MATERIAL_MAP_DIFFUSE].color = WHITE;
    }
    if (E.skinned && E.idle >= 0) UpdateModelAnimationBones(E.m, E.anims[E.idle], 0);
    buildChunks(E);
    fprintf(stderr, "R3D enemy model %s tris=%d bones=%d anims=%d idle=%d fire=%d skinned=%d\n", p.c_str(), t,
            E.m.boneCount, E.animCount, E.idle, E.fire, (int)E.skinned);
    return true;
}

int Render3D::enemyModel(const std::string& file) {
    auto it = emIndex_.find(file);
    if (it != emIndex_.end()) return it->second;
    if (!ok_) return 0;
    em_.emplace_back();
    const int idx = (int)em_.size() - 1;
    if (!loadEnemyModel(em_[idx], root_ + "/models/" + file)) {
        if (em_[idx].m.meshCount > 0) UnloadModel(em_[idx].m);
        em_.pop_back(); emIndex_[file] = 0; return 0;
    }
    emIndex_[file] = idx;
    return idx;
}

void Render3D::animateEnemy(EnemyModel& E, const EnemyAnim& a) {
    if (!E.skinned) return;
    auto selectClip = [&](const char* role, int defaultIndex) {
        if (!a.clips) return defaultIndex;
        const auto selected = a.clips->find(role);
        if (selected == a.clips->end()) return defaultIndex;
        for (int i = 0; i < E.animCount; ++i)
            if (selected->second == E.anims[i].name && IsModelAnimationValid(E.m, E.anims[i])) return i;
        return -1; // an explicitly named missing clip must not silently play another clip
    };
    const int idle = selectClip("idle", E.idle), fire = selectClip("fire", E.fire);
    if (idle < 0) return;
    auto frameOf = [](const ModelAnimation& A, float t) {
        int f = (int)(t * 1000.f / 17.f); f %= A.frameCount; return f < 0 ? f + A.frameCount : f; };
    const ModelAnimation& I = E.anims[idle];
    UpdateModelAnimationBones(E.m, I, frameOf(I, a.idleT));
    if (a.fireT < 0 || fire < 0) return;
    const ModelAnimation& F = E.anims[fire];
    float t = a.fireT;
    if (a.held && E.sustain1 > E.sustain0 && t > E.sustain0) t = E.sustain0 + std::fmod(t - E.sustain0, E.sustain1 - E.sustain0);
    const int f = (int)(t * 1000.f / 17.f);
    if (f >= F.frameCount) return;  // one-shot clip finished: idle pose only
    Mesh* first = nullptr;
    for (int i = 0; i < E.m.meshCount; i++) if (E.m.meshes[i].boneMatrices) { first = &E.m.meshes[i]; break; }
    if (!first) return;
    const int n = std::min(E.m.boneCount, (int)E.scratch.size());
    std::memcpy(E.scratch.data(), first->boneMatrices, n * sizeof(Matrix));
    UpdateModelAnimationBones(E.m, F, f);
    for (int b = 0; b < n; b++) if (!E.weaponBone[b]) first->boneMatrices[b] = E.scratch[b];  // W_ bones from Fire
    for (int i = 0; i < E.m.meshCount; i++)
        if (E.m.meshes[i].boneMatrices && &E.m.meshes[i] != first) std::memcpy(E.m.meshes[i].boneMatrices, first->boneMatrices, n * sizeof(Matrix));
}

Vector2 Render3D::muzzleLocal(int model, Vector2 shipLocal, float depth) const {
    if (!ok_) return shipLocal;
    const Matrix M = makeShipMatrix(toWorld(shipLocal, depth), true, 0, 0, enemyScale() * em(model).scaleMul);
    return view_.toLocal(GetWorldToScreen(Vector3Transform(em(model).muzzle, M), cam_));
}

Vector3 Render3D::nozzleModel(int i) const {
    // Replacement starfighter engine outlets, in the player model basis:
    // nose -X, aft +X, wings Z, up +Y.
    static const Vector3 n[2] = {{4.27f, 0.22f, -0.98f}, {4.27f, 0.22f, 0.98f}};
    return n[i & 1];
}

void Render3D::setupCamera() {
    cam_ = CameraSystem::makeCamera(view_, cameraTurn_);
}

Vector3 Render3D::toWorld(Vector2 local, float depth) const {
    // CameraSystem::toWorld clamps to the visible playfield. That pinned every ship still beyond
    // the far edge onto the edge (nose showing, apparently paused, then "charging" once its real
    // position arrived) and drew it where it could not be hit. Along the forward axis objects are
    // now drawn where they really are; the lateral clamp is unchanged.
    local.y = Clamp(local.y, 0.f, view_.lateral());
    // Turned / shaken view (level 6 approach): CameraSystem's clamp is axis-aligned on screen,
    // so use the same affine map without it.
    if (local.x < 0.f || local.x > view_.forward() || view_.turned()) return worldAt(local, depth);
    return CameraSystem::toWorld(view_, cam_, local, depth);
}

Matrix Render3D::makeShipMatrix(Vector3 pos, bool facingBack, float pitch, float roll, float scale) const {
    const float S = scale;
    // raylib MatrixMultiply(A,B) applies A first. Attitude (roll about the nose axis, pitch about the
    // wing axis) is applied in MODEL space, then the view basis, then scale, then translation.
    Matrix R;
    if (!facingBack) {  // player: red_fighter, nose -X, wings Z, up +Y
        const float rl = Clamp(roll, -0.55f, 0.55f), pt = Clamp(pitch, -0.35f, 0.35f);
        if (view_.mode == Mode::TopDown) {
            // Roll about the model nose-tail axis (X) only; nose yaw (model Y, + = nose screen-right,
            // hence the minus) comes from the pitch slot and is non-zero only on diagonal forward moves.
            R = MatrixMultiply(MatrixRotateX(rl + profile_), MatrixRotateY(-Clamp(pitch, -0.17f, 0.17f)));
            if (climbPitch_ != 0.f) R = MatrixMultiply(MatrixRotateZ(-climbPitch_), R);   // Liftoff pull-up: nose (-X) toward +Y
            R = MatrixMultiply(R, MatrixRotateY(-PI / 2.f));  // nose -X -> world -Z, screen up
        } else {  // side: unchanged demo basis (looked right to Ryan)
            R = MatrixRotateY(PI);
            R = MatrixMultiply(R, MatrixRotateX(rl));
            // The model's nose starts along -X. After the Y half-turn, a positive
            // gameplay pitch must rotate that nose upward on screen.
            R = MatrixMultiply(R, MatrixRotateZ(-pt));
        }
    } else {  // enemy: blue_racer, nose +Z, wings X, up +Y
        const float rl = Clamp(roll, -0.4f, 0.4f), pt = Clamp(pitch, -0.3f, 0.3f);
        R = MatrixMultiply(MatrixRotateZ(rl + (view_.mode == Mode::TopDown ? profile_ : 0.f)), MatrixRotateX(-pt));  // model-space bank / pitch
        if (view_.mode == Mode::TopDown) {
            // overhead: seen from above, nose +Z = screen down (toward player). No basis turn.
        } else {
            R = MatrixMultiply(R, MatrixRotateY(-PI / 2.f));  // nose +Z -> -X (face left), profile view
        }
    }
    // Level 6 approach: the playfield image is turned view_.rot clockwise on screen; turn the hull
    // with it about the overhead camera's view axis (world Y) so its nose keeps pointing forward.
    if (view_.mode == Mode::TopDown && view_.rot != 0.f) R = MatrixMultiply(R, MatrixRotateY(-view_.rot));
    return MatrixMultiply(MatrixMultiply(R, MatrixScale(S, S, S)), MatrixTranslate(pos.x, pos.y, pos.z));
}

void Render3D::setSceneLight(bool on, Vector3 lightDir, float metalAmb, float terrainAmb) {
    if (!ok_ || (!on && !sceneLight_)) return;
    sceneLight_ = on;
    Vector3 L = on ? Vector3Normalize(lightDir) : Vector3Normalize(Vector3{-0.4f, -0.8f, -0.45f});
    const float sa = 0.f, ma = on ? metalAmb : 0.28f, ta = on ? terrainAmb : 0.30f;
    for (Shader* sh : {&lit_, &dentSh_, &dentSkinSh_}) if (sh->id > 0) {
        SetShaderValue(*sh, GetShaderLocation(*sh, "lightDir"), &L, SHADER_UNIFORM_VEC3);
        SetShaderValue(*sh, GetShaderLocation(*sh, "amb"), &sa, SHADER_UNIFORM_FLOAT);
    }
    if (metal_.id > 0) {
        SetShaderValue(metal_, GetShaderLocation(metal_, "lightDir"), &L, SHADER_UNIFORM_VEC3);
        SetShaderValue(metal_, GetShaderLocation(metal_, "amb"), &ma, SHADER_UNIFORM_FLOAT);
    }
    if (playerGlassSh_.id > 0) {
        SetShaderValue(playerGlassSh_, GetShaderLocation(playerGlassSh_, "lightDir"), &L, SHADER_UNIFORM_VEC3);
        const float glassAmbient = 0.03f;
        SetShaderValue(playerGlassSh_, GetShaderLocation(playerGlassSh_, "amb"), &glassAmbient, SHADER_UNIFORM_FLOAT);
    }
    const Vector3 toSun = on ? Vector3Negate(L) : Vector3{-0.45f, 0.8f, 0.35f};
    sceneToSun_ = toSun; sceneTerrainAmb_ = ta;
    for (Shader* sh : {&terrainSh_, &terrainProcSh_}) if (sh->id > 0) {
        SetShaderValue(*sh, GetShaderLocation(*sh, "sunL"), &toSun, SHADER_UNIFORM_VEC3);
        SetShaderValue(*sh, GetShaderLocation(*sh, "amb"), &ta, SHADER_UNIFORM_FLOAT);
    }
}

void Render3D::drawApproachScenery(const ApproachScene& a, ApproachOut& out) {
    // Level 6 planet approach: own camera around a unit planet at the origin, oriented exactly like
    // the render camera (so the planet, the sun and the ship share one world), placed a.dist radii
    // from the centre along -a.pdir. A starfield at infinity sits behind it.
    out = {};
    if (!ok_) return;
    BgModel& pm = bgModel(a.titleScene ? "title_exoplanet_surface_4k.glb" : "nova_planet_surface.glb");
    if (!pm.ok) return;
    const Vector3 f = Vector3Normalize(a.f), r = Vector3Normalize(Vector3CrossProduct(f, a.u)), u = Vector3CrossProduct(r, f);
    const Vector3 sun = Vector3Normalize(a.sun);
    Camera3D c{};
    c.position = Vector3Scale(Vector3Normalize(a.pdir), -fmaxf(a.dist, 1.0005f));
    c.target = Vector3Add(Vector3Add(c.position, f), Vector3Add(Vector3Scale(r, a.jitter.x), Vector3Scale(u, a.jitter.y)));
    c.up = u; c.fovy = a.fovy; c.projection = CAMERA_PERSPECTIVE;
    const int SW = GetScreenWidth(), SH = GetScreenHeight();
    {   // sky at infinity: baked nebula / gas (additive, so the 2D layers under it survive), then point stars
        const Vector3 fj = Vector3Normalize(Vector3Subtract(c.target, c.position));
        const Vector3 rj = Vector3Normalize(Vector3CrossProduct(fj, u)), uj = Vector3CrossProduct(rj, fj);
        const float th = tanf(a.fovy * 0.5f * DEG2RAD), fp = SH * 0.5f / th;
        if (skyOk_ && a.nebula > 0.01f) {
            const Vector2 tanH{th * SW / (float)std::max(1, SH), th};
            SetShaderValue(skySh_, skyLoc_[0], &fj, SHADER_UNIFORM_VEC3);
            SetShaderValue(skySh_, skyLoc_[1], &rj, SHADER_UNIFORM_VEC3);
            SetShaderValue(skySh_, skyLoc_[2], &uj, SHADER_UNIFORM_VEC3);
            SetShaderValue(skySh_, skyLoc_[3], &tanH, SHADER_UNIFORM_VEC2);
            SetShaderValue(skySh_, skyLoc_[4], &a.nebula, SHADER_UNIFORM_FLOAT);
            const Vector3 nebulaTint{a.nebulaTint.r / 255.f, a.nebulaTint.g / 255.f, a.nebulaTint.b / 255.f};
            SetShaderValue(skySh_, skyLoc_[5], &nebulaTint, SHADER_UNIFORM_VEC3);
            BeginBlendMode(BLEND_ADDITIVE);
            BeginShaderMode(skySh_);
            DrawTexturePro(nebTex_, {0, 0, (float)nebTex_.width, (float)nebTex_.height}, {0, 0, (float)SW, (float)SH}, {0, 0}, 0.f, WHITE);
            EndShaderMode();
            EndBlendMode();
        }
        if (a.stars > 0.01f) {   // ~7000 point stars: 1 px (2 px for the brightest few %), real colour temperatures
            const Vector3 baseF = a.separateStarFrame ? Vector3Normalize(a.starF) : fj;
            const Vector3 su = a.separateStarFrame ? Vector3Normalize(a.starU) : uj;
            const Vector3 baseR = Vector3Normalize(Vector3CrossProduct(baseF, su));
            const float starAngle = a.time * a.starSpeed;
            const Vector3 sf = Vector3Normalize(Vector3Add(Vector3Scale(baseF, cosf(starAngle)), Vector3Scale(baseR, sinf(starAngle))));
            const Vector3 sr = Vector3Normalize(Vector3CrossProduct(sf, su));
            const float previousAngle = starAngle - a.starSpeed * 0.010f;
            const Vector3 previousF = Vector3Normalize(Vector3Add(Vector3Scale(baseF, cosf(previousAngle)), Vector3Scale(baseR, sinf(previousAngle))));
            const Vector3 previousR = Vector3Normalize(Vector3CrossProduct(previousF, su));
            static std::vector<Vector4> st; static std::vector<Color> sc;
            if (st.empty()) {
                uint32_t h = 2463534242u;
                auto rnd = [&]() { h ^= h << 13; h ^= h >> 17; h ^= h << 5; return (h & 0xFFFFFF) / 16777216.f; };
                static const Color temp[] = {{170, 195, 255, 255}, {205, 220, 255, 255}, {255, 255, 255, 255}, {255, 244, 220, 255}, {255, 214, 160, 255}, {255, 180, 140, 255}};
                for (int i = 0; i < 7000; i++) {
                    const float z = rnd() * 2.f - 1.f, ph = rnd() * 2.f * PI, rr = sqrtf(1.f - z * z);
                    st.push_back({rr * cosf(ph), z, rr * sinf(ph), powf(rnd(), 4.f)});
                    const float tr = rnd(); sc.push_back(temp[tr < 0.12f ? 0 : tr < 0.30f ? 1 : tr < 0.62f ? 2 : tr < 0.82f ? 3 : tr < 0.94f ? 4 : 5]);
                }
            }
            const float cosLim = cosf(fminf(1.5f, a.fovy * DEG2RAD));
            for (size_t i = 0; i < st.size(); i++) {
                const Vector3 d{st[i].x, st[i].y, st[i].z};
                const float z = Vector3DotProduct(d, sf);
                if (z < cosLim) continue;
                const float px = floorf(SW * 0.5f + Vector3DotProduct(d, sr) / z * fp), py = floorf(SH * 0.5f - Vector3DotProduct(d, su) / z * fp);
                if (px < 0 || py < 0 || px >= SW || py >= SH) continue;
                const float w = st[i].w, b = 0.18f + 0.82f * w, sz = w > 0.55f ? 2.f : 1.f;
                const float previousZ = Vector3DotProduct(d, previousF);
                if (a.starSpeed > 0.f && w > 0.72f && previousZ > cosLim) {
                    const float previousX = SW * 0.5f + Vector3DotProduct(d, previousR) / previousZ * fp;
                    const float previousY = SH * 0.5f - Vector3DotProduct(d, su) / previousZ * fp;
                    DrawLineV({previousX, previousY}, {px, py}, Fade(sc[i], b * a.stars));
                } else DrawRectangleV({px, py}, {sz, sz}, Fade(sc[i], b * a.stars));
            }
        }
    }
    rlDrawRenderBatchActive();
    glClear(GL_DEPTH_BUFFER_BIT);  // the 2D stars must not occlude the planet
    rlDisableBackfaceCulling();
    BeginMode3D(c);
    const bool sunDraw = sunOk_ && a.sunR > 0.f;
    const float sunD = 300.f, sunRad = sunD * tanf(fminf(a.sunR, 0.6f));
    const Vector3 sunC = Vector3Add(c.position, Vector3Scale(sun, sunD));
    if (sunDraw && Vector3DotProduct(sun, f) > -0.3f) {   // the ONE light: a plasma ball far beyond the planet (drawn first: the planet covers it)
        const Matrix V = MatrixLookAt(c.position, c.target, c.up);
        const Vector3 Xs{V.m0, V.m4, V.m8}, Ys{V.m1, V.m5, V.m9}, Zs{V.m2, V.m6, V.m10};   // camera right / up / toward the camera
        drawSunAndFlares(c, sunC, sunRad, a.time, Xs, Ys, Zs, tanf(a.sunR) / tanf(a.fovy * 0.5f * DEG2RAD) * SH * 0.5f);
    }
    const float one = 1.f, pAmb = 0.09f, cAmb = 0.08f, defP = 0.06f, defC = 0.34f, zero = 0.f;
    if (a.planet) {
    Shader& surfaceShader = a.titleScene ? titlePlanetSh_ : planetSh_;
    for (int i = 0; i < pm.m.materialCount; i++) pm.m.materials[i].shader = surfaceShader;
    // Spin about the flight's lateral axis (world X): the surface ahead streams back under us.
    const Matrix spin = MatrixMultiply(MatrixMultiply(MatrixMultiply(MatrixRotateY(a.spin), MatrixRotateZ(-PI * 0.5f)), MatrixRotateY(0.25f)), a.prot);
    const float sc = 1.f / fmaxf(1e-3f, pm.rMax);
    pm.m.transform = MatrixMultiply(spin, MatrixScale(sc, sc, sc));
    float col[3] = {a.tint.r / 255.f, a.tint.g / 255.f, a.tint.b / 255.f}, rr[2] = {pm.rMin, pm.rMax};
    const Vector3 eye = Vector3Negate(f);
    if (a.titleScene) {
        float roughness = 1.f, normalScale = 1.f;
        if (pm.m.meshCount > 0) {
            const int materialIndex = pm.m.meshMaterial[0];
            if (materialIndex >= 0 && materialIndex < pm.m.materialCount) {
                const Material& material = pm.m.materials[materialIndex];
                if (material.maps[MATERIAL_MAP_ROUGHNESS].value > 0.f)
                    roughness = material.maps[MATERIAL_MAP_ROUGHNESS].value;
                if (material.maps[MATERIAL_MAP_NORMAL].value > 0.f)
                    normalScale = material.maps[MATERIAL_MAP_NORMAL].value;
            }
        }
        SetShaderValue(titlePlanetSh_, titlePlanetEyeLoc_, &eye, SHADER_UNIFORM_VEC3);
        SetShaderValue(titlePlanetSh_, titlePlanetSunLoc_, &sun, SHADER_UNIFORM_VEC3);
        SetShaderValue(titlePlanetSh_, titlePlanetRoughnessLoc_, &roughness, SHADER_UNIFORM_FLOAT);
        SetShaderValue(titlePlanetSh_, titlePlanetNormalScaleLoc_, &normalScale, SHADER_UNIFORM_FLOAT);
    } else {
        SetShaderValue(planetSh_, pLocCol_, col, SHADER_UNIFORM_VEC3);
        SetShaderValue(planetSh_, pLocR_, rr, SHADER_UNIFORM_VEC2);
        SetShaderValue(planetSh_, GetShaderLocation(planetSh_, "eye"), &eye, SHADER_UNIFORM_VEC3);
        SetShaderValue(planetSh_, GetShaderLocation(planetSh_, "sun"), &sun, SHADER_UNIFORM_VEC3);
        SetShaderValue(planetSh_, GetShaderLocation(planetSh_, "sunMode"), &one, SHADER_UNIFORM_FLOAT);
        SetShaderValue(planetSh_, GetShaderLocation(planetSh_, "amb"), &pAmb, SHADER_UNIFORM_FLOAT);
        SetShaderValue(planetSh_, GetShaderLocation(planetSh_, "cityK"), &a.cityK, SHADER_UNIFORM_FLOAT);
    }
    DrawModel(pm.m, {0, 0, 0}, 1, WHITE);
    if (a.titleScene && a.titleShell) {
        BgModel& shell = bgModel("title_exoplanet_atmosphere_4k.glb");
        if (shell.ok) {
            for (int i = 0; i < shell.m.materialCount; i++) shell.m.materials[i].shader = titleShellSh_;
            shell.m.transform = pm.m.transform;
            SetShaderValue(titleShellSh_, titleShellSunLoc_, &sun, SHADER_UNIFORM_VEC3);
            BeginBlendMode(BLEND_ALPHA);
            rlDisableDepthMask();
            DrawModel(shell.m, {0, 0, 0}, 1, WHITE);
            rlEnableDepthMask();
            EndBlendMode();
        }
    }
    if (!a.titleScene) {
    BgModel& cm = bgModel("nova_planet_clouds.glb");
    if (cm.ok && a.cloudA > 0.01f) {
        for (int i = 0; i < cm.m.materialCount; i++) cm.m.materials[i].shader = cloudSh_;
        const float cs = a.cloudR / fmaxf(1e-3f, cm.rMax);
        const Matrix cspin = MatrixMultiply(MatrixMultiply(MatrixMultiply(MatrixRotateY(a.cloudSpin), MatrixRotateZ(-PI * 0.5f)), MatrixRotateY(0.25f)), a.prot);
        SetShaderValue(cloudSh_, GetShaderLocation(cloudSh_, "alphaK"), &a.cloudA, SHADER_UNIFORM_FLOAT);
        cm.m.transform = MatrixMultiply(cspin, MatrixScale(cs, cs, cs));
        SetShaderValue(cloudSh_, GetShaderLocation(cloudSh_, "sun"), &sun, SHADER_UNIFORM_VEC3);
        SetShaderValue(cloudSh_, GetShaderLocation(cloudSh_, "amb"), &cAmb, SHADER_UNIFORM_FLOAT);
        BeginBlendMode(BLEND_ALPHA);
        rlDisableDepthMask();
        DrawModel(cm.m, {0, 0, 0}, 1, WHITE);
        rlEnableDepthMask();
        EndBlendMode();
    }
    }
    if ((!a.titleScene || a.titleAtmo) && atmoOk_ && a.atmoK > 0.01f) {  // gameplay atmosphere remains unchanged (title: same sun-lit shell)
        // Shell 4% of the radius; never thinner than ~6 px on screen so a small, far planet keeps a smooth
        // rim. Close in, the grazing columns get very long: ease the gain and tighten the sunrise lobe.
        const float angP = asinf(fminf(1.f, 1.f / fmaxf(a.dist, 1.0005f)));
        const float rpx = tanf(angP) / tanf(a.fovy * 0.5f * DEG2RAD) * SH * 0.5f;
        const float close = Clamp((1.3f - a.dist) / (1.3f - 1.05f), 0.f, 1.f);
        const float Rp = 0.995f, Ra = 1.f + fmaxf(0.04f, fminf(0.15f, 6.f / fmaxf(1.f, rpx))), meshR = Ra * 1.02f;   // mesh a bit larger than the analytic shell (facets)
        const float gain = Lerp(1.f, 0.6f, close) * a.atmoK, lobe = Lerp(10.f, 22.f, close);
        SetShaderValue(atmoSh_, aLocGain_, &gain, SHADER_UNIFORM_FLOAT);
        SetShaderValue(atmoSh_, aLocLobe_, &lobe, SHADER_UNIFORM_FLOAT);
        const float dK = Lerp(2.5f, 4.f, Clamp((rpx - 60.f) / 400.f, 0.f, 1.f));   // small disc: softer falloff (no 1-px line)
        SetShaderValue(atmoSh_, aLocDK_, &dK, SHADER_UNIFORM_FLOAT);
        SetShaderValue(atmoSh_, aLocCam_, &c.position, SHADER_UNIFORM_VEC3);
        SetShaderValue(atmoSh_, aLocSun_, &sun, SHADER_UNIFORM_VEC3);
        SetShaderValue(atmoSh_, aLocRp_, &Rp, SHADER_UNIFORM_FLOAT);
        SetShaderValue(atmoSh_, aLocRa_, &Ra, SHADER_UNIFORM_FLOAT);
        rlDrawRenderBatchActive();
        BeginBlendMode(BLEND_ADDITIVE);
        rlDisableDepthTest(); rlDisableDepthMask();
        DrawMesh(atmoMesh_, atmoMat_, MatrixScale(meshR, meshR, meshR));
        rlDrawRenderBatchActive();
        rlEnableDepthMask(); rlEnableDepthTest();
        EndBlendMode();
    }
    }   // a.planet
    if (a.titleScene && player_.meshCount > 0) {
        const float pixelWorld = 2.f * tanf(a.fovy * 0.5f * DEG2RAD) / fmaxf(1.f, (float)SH);
        const Vector3 shipLight = Vector3Negate(sun);
        const float noAmbient = 0.f;
        const int lightLoc = GetShaderLocation(lit_, "lightDir");
        const int ambientLoc = GetShaderLocation(lit_, "amb");
        SetShaderValue(lit_, lightLoc, &shipLight, SHADER_UNIFORM_VEC3);
        SetShaderValue(lit_, ambientLoc, &noAmbient, SHADER_UNIFORM_FLOAT);

        // t228: Ryan's fighter in front of the planet (camera space pose from the title), sun-lit with a small fill.
        {
            const float th = tanf(a.fovy * 0.5f * DEG2RAD), asp = SW / (float)std::max(1, SH), d = fmaxf(0.05f, a.titleShip.z);
            const Vector3 P = Vector3Add(Vector3Add(Vector3Add(c.position, Vector3Scale(f, d)),
                Vector3Scale(r, (a.titleShip.x * 2.f - 1.f) * th * asp * d)), Vector3Scale(u, (1.f - a.titleShip.y * 2.f) * th * d));
            const Vector3 H = Vector3Normalize(Vector3Add(Vector3Add(Vector3Scale(r, a.titleShipHead.x), Vector3Scale(u, a.titleShipHead.y)), Vector3Scale(f, a.titleShipHead.z)));
            Vector3 U = Vector3Normalize(Vector3Subtract(u, Vector3Scale(H, Vector3DotProduct(u, H))));
            U = Vector3RotateByAxisAngle(U, H, a.titleShipRoll);
            const Vector3 X = Vector3Negate(H), Z = Vector3CrossProduct(X, U);   // model: nose -X, up +Y
            const Matrix keep = player_.transform;   // (gameplay's drawShip leaves its matrix here: measure the bare hull)
            player_.transform = MatrixIdentity();
            const BoundingBox bb = GetModelBoundingBox(player_);
            const float len = fmaxf(0.01f, bb.max.x - bb.min.x), S = a.titleShipW * 2.f * th * d / len;
            Matrix M = MatrixIdentity();
            M.m0 = X.x * S; M.m1 = X.y * S; M.m2 = X.z * S;
            M.m4 = U.x * S; M.m5 = U.y * S; M.m6 = U.z * S;
            M.m8 = Z.x * S; M.m9 = Z.y * S; M.m10 = Z.z * S;
            M.m12 = P.x; M.m13 = P.y; M.m14 = P.z;
            SetShaderValue(lit_, ambientLoc, &a.titleShipAmb, SHADER_UNIFORM_FLOAT);
            const float chromeMetal = 0.94f, chromeRough = 0.12f, chromeEnv = 0.72f, opaque = 1.f;
            const float glassMetal = 0.06f, glassRough = 0.07f, glassEnv = 0.95f;
            SetShaderValue(metal_, metalEye_, &c.position, SHADER_UNIFORM_VEC3);
            SetShaderValue(metal_, metalness_, &chromeMetal, SHADER_UNIFORM_FLOAT);
            SetShaderValue(metal_, metalRoughness_, &chromeRough, SHADER_UNIFORM_FLOAT);
            SetShaderValue(metal_, metalEnvStrength_, &chromeEnv, SHADER_UNIFORM_FLOAT);
            SetShaderValue(metal_, metalOpacity_, &opaque, SHADER_UNIFORM_FLOAT);
            SetShaderValue(playerGlassSh_, glassEye_, &c.position, SHADER_UNIFORM_VEC3);
            SetShaderValue(playerGlassSh_, glassMetalness_, &glassMetal, SHADER_UNIFORM_FLOAT);
            SetShaderValue(playerGlassSh_, glassRoughness_, &glassRough, SHADER_UNIFORM_FLOAT);
            SetShaderValue(playerGlassSh_, glassEnvStrength_, &glassEnv, SHADER_UNIFORM_FLOAT);
            SetShaderValue(playerGlassSh_, glassOpacity_, &opaque, SHADER_UNIFORM_FLOAT);
            player_.transform = M;
            DrawModel(player_, {0, 0, 0}, 1.f, WHITE);
            player_.transform = keep;
            for (int i = 0; i < 2; i++) out.titleNoz[i] = GetWorldToScreenEx(Vector3Transform(nozzleModel(i), M), c, SW, SH);
            const Vector2 tail = GetWorldToScreenEx(Vector3Transform(Vector3Add(nozzleModel(0), {0.5f, 0, 0}), M), c, SW, SH);
            const Vector2 ex = Vector2Subtract(tail, out.titleNoz[0]);
            out.titleExhaust = Vector2Length(ex) > 1e-3f ? Vector2Normalize(ex) : Vector2{-1, 0};
            const Vector2 nose = GetWorldToScreenEx(Vector3Transform({bb.min.x, 0, 0}, M), c, SW, SH);
            const Vector2 aft = GetWorldToScreenEx(Vector3Transform({bb.max.x, 0, 0}, M), c, SW, SH);
            out.titleShipPx = fmaxf(Vector2Distance(nose, aft), a.titleShipW * SH * 0.5f);
        }

        const float logoDepth = 0.68f;
        const float logoWidth = SW * 0.17f * pixelWorld * logoDepth;
        const float logoScreenX = SW * 0.235f, logoScreenY = SH * 0.19f;
        const Vector3 logoPos = Vector3Add(Vector3Add(Vector3Add(c.position, Vector3Scale(f, logoDepth)),
            Vector3Scale(r, (logoScreenX - SW * 0.5f) * pixelWorld * logoDepth)),
            Vector3Scale(u, (SH * 0.5f - logoScreenY) * pixelWorld * logoDepth));
        if (a.titleGlyphs) {   // t228: the 3D glyph wordmark is optional (the title draws an Oxanium wordmark)
        const Vector3 titleWordmarkLight = Vector3Negate(sun);
        SetShaderValue(titleGlyphSh_, GetShaderLocation(titleGlyphSh_, "lightDir"),
                       &titleWordmarkLight, SHADER_UNIFORM_VEC3);
        const char* glyphLines[2] = {"u11", "starfighter"};
        const int glyphCounts[2] = {3, 11};
        float glyphMinX = 1e9f, glyphMaxX = -1e9f;
        for (int line = 0; line < 2; ++line) {
            for (int index = 0; index < glyphCounts[line]; ++index) {
                const std::string asset = std::string("title_glyphs/") + glyphLines[line] + "_" +
                    (index < 10 ? "0" : "") + std::to_string(index) + ".glb";
                BgModel& glyph = bgModel(asset);
                if (!glyph.ok) continue;
                glyphMinX = std::min(glyphMinX, glyph.bmin.x);
                glyphMaxX = std::max(glyphMaxX, glyph.bmax.x);
            }
        }
        const float logoScale = logoWidth / fmaxf(0.01f, glyphMaxX - glyphMinX);
        const float revealStep = 0.15f, revealFade = 0.24f;
        for (int line = 0; line < 2; ++line) {
            for (int index = 0; index < glyphCounts[line]; ++index) {
                const std::string asset = std::string("title_glyphs/") + glyphLines[line] + "_" +
                    (index < 10 ? "0" : "") + std::to_string(index) + ".glb";
                BgModel& glyph = bgModel(asset);
                if (!glyph.ok) continue;
                const int revealLayer = std::min(index, glyphCounts[line] - 1 - index);
                const float alpha = Clamp((a.titleRevealTime - revealLayer * revealStep) / revealFade, 0.f, 1.f);
                if (alpha <= 0.f) continue;
                for (int i = 0; i < glyph.m.materialCount; i++) glyph.m.materials[i].shader = titleGlyphSh_;
                DrawModelEx(glyph.m, logoPos, {0, 1, 0}, 1.2f * sinf(a.time * 0.28f),
                            {logoScale, logoScale, logoScale}, Fade(WHITE, alpha));
            }
        }
        }
        SetShaderValue(lit_, ambientLoc, &noAmbient, SHADER_UNIFORM_FLOAT);
        const Vector3 gameplayLight = Vector3Normalize({-0.4f, -0.8f, -0.45f});
        SetShaderValue(lit_, lightLoc, &gameplayLight, SHADER_UNIFORM_VEC3);
    }
    EndMode3D();
    rlDrawRenderBatchActive();
    // shared shaders back to their defaults for the other levels' background planets
    SetShaderValue(planetSh_, GetShaderLocation(planetSh_, "sunMode"), &zero, SHADER_UNIFORM_FLOAT);
    SetShaderValue(planetSh_, GetShaderLocation(planetSh_, "amb"), &defP, SHADER_UNIFORM_FLOAT);
    SetShaderValue(cloudSh_, GetShaderLocation(cloudSh_, "amb"), &defC, SHADER_UNIFORM_FLOAT);
    SetShaderValue(cloudSh_, GetShaderLocation(cloudSh_, "alphaK"), &one, SHADER_UNIFORM_FLOAT);
    glClear(GL_DEPTH_BUFFER_BIT);  // scenery has its own camera: ships / terrain must not test against it
    out.centre = GetWorldToScreenEx({0, 0, 0}, c, SW, SH);
    out.sunPx = GetWorldToScreenEx(Vector3Add(c.position, Vector3Scale(sun, 50.f)), c, SW, SH);
    const float ang = asinf(fminf(1.f, 1.f / fmaxf(a.dist, 1.0005f)));
    out.radiusPx = tanf(ang) / tanf(a.fovy * 0.5f * DEG2RAD) * SH * 0.5f;
    if (sunDraw) {   // where the sun disc is, and how much of it the planet leaves uncovered
        out.sunOn = Vector3DotProduct(sun, f) > 0.05f;
        out.sunDiscPx = GetWorldToScreenEx(sunC, c, SW, SH);
        out.sunRadPx = tanf(a.sunR) / tanf(a.fovy * 0.5f * DEG2RAD) * SH * 0.5f;
        const float sep = acosf(Clamp(Vector3DotProduct(sun, Vector3Normalize(Vector3Negate(c.position))), -1.f, 1.f));
        out.sunVis = Clamp((sep - (ang - a.sunR)) / fmaxf(1e-4f, 2.f * a.sunR), 0.f, 1.f);
        if (!a.planet) out.sunVis = 1.f;
    }
    {   // sunrise point: the limb point (seen from the camera) on the sun's side
        const float d = fmaxf(a.dist, 1.0005f), sq = sqrtf(fmaxf(0.f, 1.f - 1.f / (d * d)));
        const Vector3 Ch = Vector3Normalize(c.position);   // centre -> camera
        Vector3 sp = Vector3Subtract(sun, Vector3Scale(Ch, Vector3DotProduct(sun, Ch)));
        if (Vector3LengthSqr(sp) < 1e-10f) sp = Vector3Normalize(Vector3CrossProduct(Ch, fabsf(Ch.y) < 0.9f ? Vector3{0, 1, 0} : Vector3{1, 0, 0}));
        sp = Vector3Normalize(sp);
        const Vector3 e2 = Vector3CrossProduct(Ch, sp);
        auto limb = [&](float an) { return Vector3Add(Vector3Scale(Ch, 1.f / d), Vector3Scale(Vector3Add(Vector3Scale(sp, cosf(an)), Vector3Scale(e2, sinf(an))), sq)); };
        const Vector3 q0 = limb(0.f), q1 = limb(0.01f);
        out.flareOk = Vector3DotProduct(Vector3Subtract(q0, c.position), f) > 0.02f;
        out.flarePx = GetWorldToScreenEx(q0, c, SW, SH);
        out.flareTanPx = GetWorldToScreenEx(q1, c, SW, SH);
    }
}

// Liftoff sun body + corona (impostor) and its prominences. Prominences: loops arching off the limb
// between two footpoints and eruptions (one foot, the jet climbs, widens and fades), each on its own
// life cycle so several are always up; ribbons face the camera, the part behind the disc is hidden.
void Render3D::drawSunAndFlares(const Camera3D& c, Vector3 sunC, float sunRad, float time, Vector3 Xs, Vector3 Ys, Vector3 Zs, float rpx) {
    rlDrawRenderBatchActive();
    const float aa = fmaxf(0.002f, 1.6f / fmaxf(1.f, rpx)), Q = 2.2f;
    Matrix B = {Xs.x, Ys.x, Zs.x, 0, Xs.y, Ys.y, Zs.y, 0, Xs.z, Ys.z, Zs.z, 0, 0, 0, 0, 1};   // view -> world (columns)
    const Matrix rot = MatrixMultiply(B, MatrixRotateY(-time * 0.02f));                       // ... -> the sun's spinning frame
    SetShaderValue(sunSh_, sLocTime_, &time, SHADER_UNIFORM_FLOAT);
    SetShaderValue(sunSh_, sLocAA_, &aa, SHADER_UNIFORM_FLOAT);
    SetShaderValue(sunSh_, sLocQ_, &Q, SHADER_UNIFORM_FLOAT);
    SetShaderValueMatrix(sunSh_, sLocRot_, rot);
    rlDisableDepthMask();
    BeginBlendMode(BLEND_ALPHA_PREMULTIPLY);
    BeginShaderMode(sunSh_);
    const float size = sunRad * 2.f * Q;
    DrawBillboardPro(c, noiseTex_, {0, 0, (float)noiseTex_.width, (float)noiseTex_.height}, sunC, Ys, {size, size}, {size * 0.5f, size * 0.5f}, 0.f, WHITE);
    EndShaderMode();
    rlDrawRenderBatchActive();
    EndBlendMode();
    // ---- prominences / eruptions ----
    struct Prom { float phi, period, off, h, w, span, z0, z1; bool erupt; };
    static const Prom P[] = {
        {2.30f, 14.f, 0.0f, 0.60f, 0.090f, 0.26f, 0.06f, 0.03f, false},   // the big upper-left arch of Ryan's photo
        {0.45f, 11.f, 4.0f, 0.36f, 0.070f, 0.15f, 0.03f, 0.05f, false},   // right limb
        {4.30f, 13.f, 8.5f, 0.42f, 0.075f, 0.18f, 0.04f, 0.03f, false},    // lower left
        {5.60f, 19.f, 2.0f, 0.95f, 0.070f, 0.00f, 0.03f, 0.f, true},       // lower right: a slow eruption that lifts off and fades
    };
    constexpr int N = 28;
    const float wS = Clamp(14.f / (0.09f * fmaxf(rpx, 1.f)), 1.f, 1.35f);   // keep the plasma ribbons solid when the sun is small on screen
    BeginBlendMode(BLEND_ADDITIVE);
    BeginShaderMode(promSh_);
    SetShaderValue(promSh_, prLocTime_, &time, SHADER_UNIFORM_FLOAT);
    rlSetTexture(noiseTex_.id);
    rlBegin(RL_QUADS);
    for (int i = 0; i < (int)(sizeof(P) / sizeof(P[0])); i++) {
        const Prom& p = P[i];
        const float u = fmodf((time + p.off) / p.period, 1.f);
        const float fade = Clamp(u / 0.18f, 0.f, 1.f) * (1.f - Clamp((u - 0.72f) / 0.28f, 0.f, 1.f));
        if (fade < 0.01f) continue;
        const float phi = p.phi + 0.10f * sinf(time * 0.05f + i);
        const Vector3 b = Vector3Add(Vector3Scale(Xs, cosf(phi)), Vector3Scale(Ys, sinf(phi)));
        const Vector3 tn = Vector3Add(Vector3Scale(Xs, -sinf(phi)), Vector3Scale(Ys, cosf(phi)));
        Vector3 pts[N + 1]; float wid[N + 1], al[N + 1];
        for (int k = 0; k <= N; k++) {
            const float sk = k / (float)N;
            Vector3 q;
            if (!p.erupt) {
                const Vector3 A = Vector3Normalize(Vector3Add(Vector3Add(Vector3Scale(b, cosf(p.span)), Vector3Scale(tn, sinf(p.span))), Vector3Scale(Zs, p.z0)));
                const Vector3 Bf = Vector3Normalize(Vector3Add(Vector3Add(Vector3Scale(b, cosf(p.span)), Vector3Scale(tn, -sinf(p.span))), Vector3Scale(Zs, p.z1)));
                const float H = p.h * (0.45f + 0.55f * Clamp(u / 0.5f, 0.f, 1.f)) * (1.f + 0.08f * sinf(time * 0.9f + i));
                q = Vector3Scale(Vector3Normalize(Vector3Lerp(A, Bf, sk)), 1.f + H * sinf(PI * sk));
                q = Vector3Add(q, Vector3Scale(tn, 0.05f * H * sinf(2.f * PI * sk + time * 0.35f + i)));   // twist
                wid[k] = p.w * (0.55f + 0.45f * sinf(PI * sk)) * (1.f + 0.25f * sinf(sk * 9.f + time * 0.6f + i));
                al[k] = fade;
            } else {
                const Vector3 A = Vector3Normalize(Vector3Add(b, Vector3Scale(Zs, p.z0)));
                const float H = p.h * (0.25f + 0.75f * u);
                const float bend = 0.35f * sinf(time * 0.07f + i);
                q = Vector3Add(Vector3Scale(A, 1.f + H * sk), Vector3Scale(tn, H * sk * sk * bend));
                wid[k] = p.w * (0.55f + 0.45f * sinf(PI * fminf(1.f, sk * 1.3f))) * (0.9f + 0.2f * u);   // tapered, never a fan
                al[k] = fade * (1.f - 0.80f * sk) * (0.6f + 0.4f * (1.f - u));
            }
            const float zc = Vector3DotProduct(q, Zs), sky = Vector3Length(Vector3Subtract(q, Vector3Scale(Zs, zc)));
            const float behind = (1.f - Clamp((sky - 0.90f) / 0.14f, 0.f, 1.f)) * Clamp((0.02f - zc) / 0.12f, 0.f, 1.f);   // soft: no hard cut along the limb
            const float onDisc = sky < 1.f && zc > 0.f ? 0.35f : 1.f;   // (seen against the disc: dark filament-ish, faint)
            al[k] *= (1.f - behind) * onDisc;
            pts[k] = Vector3Add(sunC, Vector3Scale(q, sunRad));
        }
        // ribbon side vectors in the camera plane (Xs/Ys) from the screen-projected tangent: a loop
        // that runs toward the viewer near its footpoint no longer spins its quads into a starburst
        Vector3 side[N + 1]; Vector2 prev2{0, 1};
        for (int k = 0; k <= N; k++) {
            const Vector3 tg = Vector3Subtract(pts[std::min(k + 1, N)], pts[std::max(k - 1, 0)]);
            Vector2 t2{Vector3DotProduct(tg, Xs), Vector3DotProduct(tg, Ys)};
            const float l2 = sqrtf(t2.x * t2.x + t2.y * t2.y);
            Vector2 n2 = l2 > 1e-4f * sunRad ? Vector2{-t2.y / l2, t2.x / l2} : prev2;
            if (k > 0 && n2.x * prev2.x + n2.y * prev2.y < 0.f) n2 = {-n2.x, -n2.y};   // keep one orientation along the strip
            prev2 = n2;
            side[k] = Vector3Add(Vector3Scale(Xs, n2.x), Vector3Scale(Ys, n2.y));
        }
        const unsigned char heat = p.erupt ? 200 : 40, seed = (unsigned char)(37 * i + 11);
        // half-width limit from the projected curvature (radius ~ segment / turn angle): the inner edge of a
        // tight bend never crosses itself (that fold was the 'starburst')
        float wmax[N + 1];
        for (int k = 0; k <= N; k++) {
            const int k0 = std::max(k - 1, 0), k1 = std::min(k + 1, N);
            const Vector3 da = Vector3Subtract(pts[k], pts[k0]), db = Vector3Subtract(pts[k1], pts[k]);
            const Vector2 a2{Vector3DotProduct(da, Xs), Vector3DotProduct(da, Ys)}, b2{Vector3DotProduct(db, Xs), Vector3DotProduct(db, Ys)};
            const float la = sqrtf(a2.x * a2.x + a2.y * a2.y), lb = sqrtf(b2.x * b2.x + b2.y * b2.y);
            float rad = 1e9f;
            if (la > 1e-6f && lb > 1e-6f) {
                const float cs = Clamp((a2.x * b2.x + a2.y * b2.y) / (la * lb), -1.f, 1.f), ang = acosf(cs);
                if (ang > 1e-4f) rad = 0.5f * (la + lb) / ang;
            }
            wmax[k] = 0.8f * rad;
        }
        for (int k = 1; k < N; k++) wmax[k] = fminf(wmax[k], fminf(wmax[k - 1], wmax[k + 1]) * 1.6f);   // smooth the limit a little
        for (int layer = 1; layer < 3; layer++) {   // soft glow, then the bright core (no wide haze layer)
            const float wk = layer == 1 ? 1.7f : 1.f, ak = layer == 1 ? 0.42f : 1.f;
            for (int k = 0; k < N; k++) {
                Vector3 sd[2];
                for (int j = 0; j < 2; j++) {
                    const int kk = k + j;
                    sd[j] = Vector3Scale(side[kk], fminf(wid[kk] * wk * wS * sunRad, wmax[kk]));
                }
                const float s0 = k / (float)N, s1 = (k + 1) / (float)N;
                const unsigned char a0 = (unsigned char)(255.f * Clamp(al[k] * ak, 0.f, 1.f)), a1 = (unsigned char)(255.f * Clamp(al[k + 1] * ak, 0.f, 1.f));
                if (a0 == 0 && a1 == 0) continue;
                rlColor4ub(255, heat, seed, a0); rlTexCoord2f(s0, 0.f); rlVertex3f(pts[k].x - sd[0].x, pts[k].y - sd[0].y, pts[k].z - sd[0].z);
                rlColor4ub(255, heat, seed, a0); rlTexCoord2f(s0, 1.f); rlVertex3f(pts[k].x + sd[0].x, pts[k].y + sd[0].y, pts[k].z + sd[0].z);
                rlColor4ub(255, heat, seed, a1); rlTexCoord2f(s1, 1.f); rlVertex3f(pts[k + 1].x + sd[1].x, pts[k + 1].y + sd[1].y, pts[k + 1].z + sd[1].z);
                rlColor4ub(255, heat, seed, a1); rlTexCoord2f(s1, 0.f); rlVertex3f(pts[k + 1].x - sd[1].x, pts[k + 1].y - sd[1].y, pts[k + 1].z - sd[1].z);
            }
        }
    }
    rlEnd();
    EndShaderMode();
    rlDrawRenderBatchActive();
    rlSetTexture(0);
    EndBlendMode();
    rlEnableDepthMask();
}

static Matrix planetSpinMatrix(float spin, const Matrix& prot) {
    return MatrixMultiply(MatrixMultiply(MatrixMultiply(MatrixRotateY(spin), MatrixRotateZ(-PI * 0.5f)), MatrixRotateY(0.25f)), prot);
}

void Render3D::renderVolFog(const ApproachScene& a, const VolFog& v) {
    fogValid_ = false;
    if (!ok_ || !fogOk_ || !v.on || v.gain < 0.003f) return;
    const int SW = GetScreenWidth(), SH = GetScreenHeight();
    static const int capEnv = getenv("VOLW") ? atoi(getenv("VOLW")) : 0;
    const bool orbit = a.dist > 1.3f;
    const int cap = capEnv > 0 ? capEnv : (orbit ? 720 : 480);   // from orbit most rays miss: spend it on sharper cloud patches
    const int w = std::max(64, std::min(orbit ? SW / 2 : SW / 3, cap)), h = std::max(36, w * SH / std::max(1, SW));
    for (auto& rt : fogRt_) if (rt.id == 0 || rt.texture.width != w || rt.texture.height != h) {
        if (rt.id > 0) UnloadRenderTexture(rt);
        rt = LoadRenderTexture(w, h);
        SetTextureFilter(rt.texture, TEXTURE_FILTER_BILINEAR);
        SetTextureWrap(rt.texture, TEXTURE_WRAP_CLAMP);
    }
    const Vector3 f = Vector3Normalize(a.f), r = Vector3Normalize(Vector3CrossProduct(f, a.u)), u = Vector3CrossProduct(r, f);
    const Vector3 C = Vector3Scale(Vector3Normalize(a.pdir), -fmaxf(a.dist, 1.00002f)), sun = Vector3Normalize(a.sun);
    const Matrix W2L = MatrixTranspose(planetSpinMatrix(a.spin, a.prot));
    const float th = tanf(a.fovy * 0.5f * DEG2RAD);
    const Vector2 tanH{th * SW / (float)std::max(1, SH), th};
    const float freq = 650.f;
    if (v.trackPatch || !patchSet_) {   // the punch-through patch: on the ground point under us, frozen once we are out
        const Vector3 Cl = Vector3Transform(C, W2L);
        patchL_ = Vector3Normalize(Cl); patchSet_ = true;
    }
    const float pv[4] = {patchL_.x, patchL_.y, patchL_.z, 1.f};
    SetShaderValue(fogSh_, fLoc_[19], pv, SHADER_UNIFORM_VEC4);
    SetShaderValue(fogSh_, fLoc_[0], &C, SHADER_UNIFORM_VEC3);
    SetShaderValue(fogSh_, fLoc_[1], &f, SHADER_UNIFORM_VEC3);
    SetShaderValue(fogSh_, fLoc_[2], &r, SHADER_UNIFORM_VEC3);
    SetShaderValue(fogSh_, fLoc_[3], &u, SHADER_UNIFORM_VEC3);
    SetShaderValue(fogSh_, fLoc_[4], &sun, SHADER_UNIFORM_VEC3);
    SetShaderValue(fogSh_, fLoc_[5], &v.wind, SHADER_UNIFORM_VEC3);
    SetShaderValueMatrix(fogSh_, fLoc_[6], W2L);
    SetShaderValue(fogSh_, fLoc_[7], &tanH, SHADER_UNIFORM_VEC2);
    SetShaderValue(fogSh_, fLoc_[8], &v.deck, SHADER_UNIFORM_VEC2);
    SetShaderValue(fogSh_, fLoc_[9], &v.cover, SHADER_UNIFORM_FLOAT);
    SetShaderValue(fogSh_, fLoc_[11], &v.atmoK, SHADER_UNIFORM_FLOAT);
    SetShaderValue(fogSh_, fLoc_[12], &v.Hs, SHADER_UNIFORM_FLOAT);
    SetShaderValue(fogSh_, fLoc_[15], &v.gain, SHADER_UNIFORM_FLOAT);
    SetShaderValue(fogSh_, fLoc_[16], &v.deckK, SHADER_UNIFORM_FLOAT);
    SetShaderValue(fogSh_, fLoc_[17], &freq, SHADER_UNIFORM_FLOAT);
    for (int pass = 0; pass < 2; pass++) {   // 0 = far (ship .. end), 1 = near (camera .. ship)
        const float sMin = pass == 0 ? v.sNear : 0.f, sMax = pass == 0 ? 1e9f : v.sNear;
        const float steps = pass == 0 ? 14.f : 4.f, ck = v.cloudK * (pass == 0 ? 1.f : v.nearK);
        SetShaderValue(fogSh_, fLoc_[10], &ck, SHADER_UNIFORM_FLOAT);
        SetShaderValue(fogSh_, fLoc_[13], &sMin, SHADER_UNIFORM_FLOAT);
        SetShaderValue(fogSh_, fLoc_[14], &sMax, SHADER_UNIFORM_FLOAT);
        SetShaderValue(fogSh_, fLoc_[18], &steps, SHADER_UNIFORM_FLOAT);
        BeginTextureMode(fogRt_[pass]);
        ClearBackground(BLANK);
        BeginBlendMode(BLEND_ALPHA_PREMULTIPLY);
        BeginShaderMode(fogSh_);
        DrawTexturePro(noiseTex_, {0, 0, (float)noiseTex_.width, (float)noiseTex_.height}, {0, 0, (float)w, (float)h}, {0, 0}, 0.f, WHITE);
        EndShaderMode();
        EndBlendMode();
        EndTextureMode();
    }
    fogValid_ = true;
}

void Render3D::drawVolFog(bool nearPass, float destW, float destH) const {
    if (!fogValid_) return;
    const RenderTexture2D& rt = fogRt_[nearPass ? 1 : 0];
    BeginBlendMode(BLEND_ALPHA_PREMULTIPLY);
    const bool up = fogUpSh_.id > 0;
    if (up) { const float px[2] = {1.f / rt.texture.width, 1.f / rt.texture.height}; SetShaderValue(fogUpSh_, fogUpPx_, px, SHADER_UNIFORM_VEC2); BeginShaderMode(fogUpSh_); }
    DrawTexturePro(rt.texture, {0, 0, (float)rt.texture.width, -(float)rt.texture.height}, {0, 0, destW, destH}, {0, 0}, 0.f, WHITE);
    if (up) EndShaderMode();
    EndBlendMode();
}

void Render3D::beginFrame(const View& view, Color clear, float cameraTurn) {
    view_ = view; cameraTurn_ = Clamp(cameraTurn, -1.f, 1.f); plumes_.clear();
    if (!ok_) return;
    setupCamera();
    ensureSceneRt();
    if (heat_ && !direct_) {  // legacy A/B path (SHIPRT=1): whole scene into the RT, then a full-screen blit
        BeginTextureMode(scene_);
        inSceneRt_ = true;
    } else {
        inSceneRt_ = false;
    }
    ClearBackground(clear);
}

void Render3D::beginShips() {
    if (!ok_) return;
    inShips_ = true;
    lights_.clear(); rings_.clear(); shipSeq_ = 0;
    // Flush 2D batch left by Mode2D / additive so Mode3D gets a clean rlgl state (demo never
    // mixes Mode2D, but the game must). Ships stay inside the same scene RT as the bg.
    rlDrawRenderBatchActive();
    rlDisableBackfaceCulling();  // GLB + rotations; demo relies on double-sided-ish lighting
    BeginMode3D(renderCamera());
}

void Render3D::beginPickupBatch() {
    if (!ok_ || !pickupOk_ || !inShips_ || pickupBatch_) return;
    rlDrawRenderBatchActive();
    BeginBlendMode(BLEND_ALPHA);
    // Pickups are gameplay markers and must remain visible over 3D background planets/terrain.
    // They are drawn before ships, so depth testing can be safely restored for ship rendering.
    rlDisableDepthTest();
    rlDisableDepthMask();
    pickupBatch_ = true;
}

void Render3D::drawPickup(Vector2 local, int type, float sizePx, Color accent,
                          float opacity, float flash, float dissolve, float expand) {
    if (!pickupBatch_ || !pickupOk_ || type < 0 || type >= kPickupTypes || opacity <= 0.001f) return;
    const int frames = pickupAnim_[type][0].frameCount;
    const int frame = frames > 0 ? (int)(GetTime() * 60.0) % frames : 0;
    if (pickupAnimFrame_[type] != frame) {
        UpdateModelAnimation(pickup_[type], pickupAnim_[type][0], frame);
        pickupAnimFrame_[type] = frame;
    }
    Vector2 screen = view_.toScreen(local);
    const float radius = sizePx * 0.65f;
    if (screen.x + radius < view_.pf.x - view_.vis || screen.x - radius > view_.pf.x + view_.pf.width + view_.vis ||
        screen.y + radius < view_.pf.y - view_.vis || screen.y - radius > view_.pf.y + view_.pf.height + view_.vis) return;
    const BoundingBox& box = pickupBounds_[type];
    const float diameter = fmaxf(0.001f, fmaxf(box.max.x - box.min.x, box.max.z - box.min.z));
    const float pixelsPerWorld = fmaxf(1.f, view_.pf.height) / CameraSystem::kViewHeight;  // focal plane (pickups sit at depth 0)
    const float scale = sizePx / (pixelsPerWorld * diameter) * fmaxf(0.05f, expand);
    // raylib's GLTF import presents the pickup face in model XZ. Top-down renders
    // world XZ directly; side view renders world XY, so it needs a 90-degree fold.
    Matrix basis = view_.mode == Mode::TopDown ? MatrixIdentity() : MatrixRotateX(PI / 2.f);
    Vector3 pos = toWorld(local);
    pickup_[type].transform = MatrixMultiply(MatrixMultiply(basis, MatrixScale(scale, scale, scale)),
                                             MatrixTranslate(pos.x, pos.y, pos.z));
    float col[3] = {accent.r / 255.f, accent.g / 255.f, accent.b / 255.f};
    SetShaderValue(pickupSh_, puLocAccent_, col, SHADER_UNIFORM_VEC3);
    SetShaderValue(pickupSh_, puLocOpacity_, &opacity, SHADER_UNIFORM_FLOAT);
    SetShaderValue(pickupSh_, puLocFlash_, &flash, SHADER_UNIFORM_FLOAT);
    SetShaderValue(pickupSh_, puLocDissolve_, &dissolve, SHADER_UNIFORM_FLOAT);
    DrawModel(pickup_[type], {0, 0, 0}, 1.f, WHITE);
}

void Render3D::animatePlayer(float weaponHeat) {
    if (!playerAnims_ || playerAnimCount_ <= 0) return;
    playerAnimClock_ += fminf(GetFrameTime(), 0.1f) * 30.f;
    auto apply = [&](int index, int frame) {
        if (index < 0 || index >= playerAnimCount_) return;
        const ModelAnimation& a = playerAnims_[index];
        if (a.frameCount <= 0 || !IsModelAnimationValid(player_, a)) return;
        UpdateModelAnimation(player_, a, frame % a.frameCount);
    };
    // Keep the landing assemblies stowed in normal flight. Their complete clips remain
    // in the GLB for the liftoff/landing controller to select later.
    for (int index : playerGearAnims_) apply(index, 0);
    for (int index : playerExhaustAnims_) apply(index, (int)playerAnimClock_);
    const bool firing = weaponHeat > 0.01f;
    for (int index : playerGunAnims_) apply(index, firing ? (int)playerAnimClock_ : 0);
}

void Render3D::endPickupBatch() {
    if (!pickupBatch_) return;
    rlDrawRenderBatchActive();
    rlEnableDepthMask();
    rlEnableDepthTest();
    EndBlendMode();
    pickupBatch_ = false;
}

void Render3D::drawShip(Vector2 local, float sizePx, bool facingBack, float pitch, float roll,
                        Color tint, bool player, float flamePower, const float (*dent)[4], int nDent,
                        const EnemyAnim* anim, Color flameTint, float depth, float weaponHeat,
                        const float (*surfaceFx)[4]) {
    if (!ok_) return;
    // The player GLB carries the authored clips, but animation playback stays
    // disabled until the rig is validated against the live player pose. This
    // keeps detached sub-meshes from entering gameplay or destabilizing Level 1.
    if (player) flameTint = {0, 0, 0, 0};  // the player's flame is never tinted
    const int mi = (!player && anim && anim->model > 0 && anim->model < (int)em_.size()) ? anim->model : 0;
    if (!player) {
        EnemyModel& E = em_[mi];
        if (anim) animateEnemy(E, *anim);
        else if (E.skinned && E.idle >= 0) UpdateModelAnimationBones(E.m, E.anims[E.idle], 0);
        setDentUniforms(dent, nDent, E.skinned, anim ? anim->glow : 0.f, surfaceFx);
    }
    // Side view + all enemies keep demo shipScale_. Overhead player only is half that on-screen size.
    float S = shipScale_;
    if (player) S *= kPlayerModelScale * (view_.mode == Mode::TopDown ? kTopDownPlayerScaleMul : kSidePlayerScaleMul);
    else S *= (view_.mode == Mode::SideScroll ? cfg::kSideEnemyScaleMul : cfg::kTopDownEnemyScaleMul) * em_[mi].scaleMul;   // t251: per-model size
    Vector3 pos = toWorld(local, depth);
    Matrix M = makeShipMatrix(pos, facingBack, pitch, roll, S);
    Model& m = player ? player_ : em_[mi].m;
    m.transform = M;
    if (!inShips_) BeginMode3D(renderCamera());
    if (player) {
        const float metalness = 0.94f, roughness = 0.12f, envStrength = 0.72f, opacity = 1.f;
        const Vector3 eye = renderCamera().position;
        SetShaderValue(metal_, metalEye_, &eye, SHADER_UNIFORM_VEC3);
        SetShaderValue(metal_, metalness_, &metalness, SHADER_UNIFORM_FLOAT);
        SetShaderValue(metal_, metalRoughness_, &roughness, SHADER_UNIFORM_FLOAT);
        SetShaderValue(metal_, metalEnvStrength_, &envStrength, SHADER_UNIFORM_FLOAT);
        SetShaderValue(metal_, metalOpacity_, &opacity, SHADER_UNIFORM_FLOAT);
        const float glassMetalness = 0.06f, glassRoughness = 0.07f, glassEnv = 0.95f;
        SetShaderValue(playerGlassSh_, glassEye_, &eye, SHADER_UNIFORM_VEC3);
        SetShaderValue(playerGlassSh_, glassMetalness_, &glassMetalness, SHADER_UNIFORM_FLOAT);
        SetShaderValue(playerGlassSh_, glassRoughness_, &glassRoughness, SHADER_UNIFORM_FLOAT);
        SetShaderValue(playerGlassSh_, glassEnvStrength_, &glassEnv, SHADER_UNIFORM_FLOAT);
        SetShaderValue(playerGlassSh_, glassOpacity_, &opacity, SHADER_UNIFORM_FLOAT);
    }
    if (!(player && hidePlayer_)) DrawModel(m, {0, 0, 0}, 1.0f, tint);
    if (!inShips_) EndMode3D();

    auto Xform = [&](Vector3 p) { return Vector3Transform(p, M); };
    // Nose sample in model space: player nose -X; facingBack enemies +X before basis.
    Vector3 noseDir = Vector3Normalize(Vector3Subtract(
        Xform(facingBack ? Vector3{0, 0, 1.f} : Vector3{-1.f, 0, 0}), Xform({0, 0, 0})));
    Vector3 exhaust = Vector3Negate(noseDir);
    Vector2 n0 = GetWorldToScreen(Xform(player ? nozzleModel(0) : enemyNozzle(mi, 0)), renderCamera());
    Vector2 n1 = GetWorldToScreen(Xform(player ? nozzleModel(1) : enemyNozzle(mi, 1)), renderCamera());
    Vector2 n0b = GetWorldToScreen(Vector3Add(Xform(player ? nozzleModel(0) : enemyNozzle(mi, 0)), exhaust), renderCamera());
    float angDeg = atan2f(n0b.y - n0.y, n0b.x - n0.x) * RAD2DEG;
    // Demo: unitPx = px per 1 world unit along exhaust (NOT pre-multiplied by S).
    float unitPx = fmaxf(1.f, Vector2Distance(n0, n0b));
    (void)sizePx;
    const float power = player ? Clamp(flamePower, 0.4f, 1.8f) : 1.f;  // edge throttle; enemy flames stay as they are
    const float Sp = player ? S * flameBoost_ : S;   // (Liftoff punch-out: the player's plume up to 2.5x)
    plumes_.push_back({n0, angDeg, unitPx, Sp, player, flameTint, power});
    plumes_.push_back({n1, angDeg, unitPx, Sp, player, flameTint, power});
    if (inShips_ && shipLightsOn_ && !(player && hidePlayer_)) {   // t250: exterior lights
        const ShipAnchors& A = player ? playerAnchors_ : em_[mi].lights;
        const int seq = shipSeq_++;
        // strobe clock: the enemy's own age when known (phase stays with the ship), else its slot
        const float strobeT = player ? (float)GetTime() : anim ? anim->idleT + 0.37f * (float)mi : (float)GetTime() + 0.618f * (float)seq;
        const Color eng = (!player && flameTint.a > 0) ? flameTint : Color{255, 150, 70, 255};
        queueShipLights(M, A, player ? nozzleModel(0) : enemyNozzle(mi, 0), player ? nozzleModel(1) : enemyNozzle(mi, 1),
                        S, strobeT, player ? (power - 0.4f) / 1.4f : 0.45f, eng, player);
    }

    if (player) {
        static int logLeft = 3;
        if (logLeft-- > 0) {
            Vector2 sp = GetWorldToScreen(pos, renderCamera());
            Vector3 originW = Vector3Transform({0,0,0}, M);
            Vector2 so = GetWorldToScreen(originW, renderCamera());
            Vector3 noseW = Vector3Transform({facingBack ? 1.f : -1.f, 0, 0}, M);
            Vector2 sn = GetWorldToScreen(noseW, renderCamera());
            fprintf(stderr, "R3D %s world=(%.2f,%.2f,%.2f) screen=(%.0f,%.0f) drawn=(%.0f,%.0f) nose=(%.0f,%.0f) scale=%.2f mode=%s\n",
                    player ? "player" : "enemy", pos.x, pos.y, pos.z, sp.x, sp.y, so.x, so.y, sn.x, sn.y, S,
                    view_.mode == Mode::TopDown ? "top" : "side");
        }
    }
}

void Render3D::drawPlayerShield(Vector2 local, float pitch, float roll, float charge, float depth) {
    // charge = remaining shield fraction (0..1). The drawn opacity eases toward it (~0.2 s) so a
    // hit visibly drains the field and a pickup visibly fills it; at zero the field is gone.
    const float dt = fminf(GetFrameTime(), 0.1f);
    if (charge <= 0.f) { shieldShown_ = 0.f; return; }
    shieldShown_ += (Clamp(charge, 0.f, 1.f) - shieldShown_) * fminf(1.f, dt * 10.f);
    static const char* alphaLog = getenv("SHIELD_ALPHA_LOG");
    if (const char* e = alphaLog) { static float acc = 0; acc += dt; if (acc > atof(e)) { acc = 0; fprintf(stderr, "SHIELD alpha charge=%.3f shown=%.3f\n", charge, shieldShown_); } }
    charge = shieldShown_;
    if (!ok_ || !inShips_) return;
    const float S = shipScale_ * kPlayerModelScale * (view_.mode == Mode::TopDown ? kTopDownShieldScaleMul : kSidePlayerScaleMul);
    const float time = (float)GetTime();
    const float base = view_.mode == Mode::TopDown ? shieldTopRadius_ : shieldBaseRadius_;
    const float radius = base * S * (1.0f + 0.018f * sinf(time * 4.f));
    const Vector3 pos = toWorld(local, depth);
    (void)pitch; (void)roll;  // The sphere needs no orientation; it is centred on the rotated ship.
    SetShaderValue(shieldSh_, shieldTime_, &time, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shieldSh_, shieldCharge_, &charge, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shieldSh_, shieldEye_, &renderCamera().position, SHADER_UNIFORM_VEC3);
    SetShaderValueV(shieldSh_, shieldHitsLoc_, shieldHits_, SHADER_UNIFORM_VEC4, 4);
    rlDrawRenderBatchActive();
    rlEnableBackfaceCulling();
    rlDisableDepthMask();
    BeginBlendMode(BLEND_ALPHA);
    DrawModel(shield_, pos, radius, WHITE);
    EndBlendMode();
    // A second additive shell creates a visible electric bloom without hiding the fighter.
    BeginBlendMode(BLEND_ADDITIVE);
    DrawModel(shield_, pos, radius, WHITE);
    EndBlendMode();
    rlEnableDepthMask();
    rlDisableBackfaceCulling();
}

void Render3D::shieldImpact(Vector2 playerLocal, Vector2 contactLocal, float depth) {
    if (!ok_) return;
    const Vector3 center = toWorld(playerLocal, depth);
    Vector3 normal = Vector3Subtract(toWorld(contactLocal, depth), center);
    if (Vector3LengthSqr(normal) < 0.0001f) return;
    normal = Vector3Normalize(normal);
    shieldHits_[nextShieldHit_] = {normal.x, normal.y, normal.z, (float)GetTime()};
    nextShieldHit_ = (nextShieldHit_ + 1) % 4;
}


// ============================================================ t250: ship exterior lights
// Anchors from the mesh itself (no art edits): each wingtip = mean of the vertices within 2.5% of
// the span of the extreme along the wing axis; cockpit = top of the hull in the front 45%.
Render3D::ShipAnchors Render3D::findAnchors(const Model& m, Vector3 nose, Vector3 wing) {
    ShipAnchors A;
    float wMin = 1e9f, wMax = -1e9f, nMin = 1e9f, nMax = -1e9f;
    auto each = [&](auto fn) {
        for (int i = 0; i < m.meshCount; i++) {
            const Mesh& me = m.meshes[i];
            if (!me.vertices) continue;
            for (int v = 0; v < me.vertexCount; v++) fn(Vector3{me.vertices[v * 3], me.vertices[v * 3 + 1], me.vertices[v * 3 + 2]});
        }
    };
    each([&](Vector3 p) {
        const float w = Vector3DotProduct(p, wing), n = Vector3DotProduct(p, nose);
        wMin = fminf(wMin, w); wMax = fmaxf(wMax, w); nMin = fminf(nMin, n); nMax = fmaxf(nMax, n);
    });
    if (wMax <= wMin) return A;
    const float span = wMax - wMin, tol = 0.025f * span;
    Vector3 sp{}, sn{}; int cp = 0, cn = 0;
    float fp = -1e9f, fn2 = -1e9f; Vector3 tipP{}, tipN{};   // the forward-most point of each outer band (leading edge)
    float yTop = -1e9f;
    const float front = nMin + 0.55f * (nMax - nMin);
    each([&](Vector3 p) {
        const float w = Vector3DotProduct(p, wing), n = Vector3DotProduct(p, nose);
        if (w >= wMax - tol) { sp = Vector3Add(sp, p); cp++; if (n > fp) { fp = n; tipP = p; } }
        if (w <= wMin + tol) { sn = Vector3Add(sn, p); cn++; if (n > fn2) { fn2 = n; tipN = p; } }
        if (n >= front && fabsf(w - 0.5f * (wMin + wMax)) < 0.2f * span) yTop = fmaxf(yTop, p.y);
    });
    Vector3 sc{}; int cc = 0;
    const float hTol = 0.03f * span;
    each([&](Vector3 p) {
        const float w = Vector3DotProduct(p, wing), n = Vector3DotProduct(p, nose);
        if (n >= front && fabsf(w - 0.5f * (wMin + wMax)) < 0.2f * span && p.y >= yTop - hTol) { sc = Vector3Add(sc, p); cc++; }
    });
    if (cp == 0 || cn == 0) return A;
    // +wing side: port for the player (nose -X, wings Z: right = -Z) and for enemies (nose +Z: right = -X)
    // halfway between the band's centre and its leading point: on the outer hull, toward the front
    A.port = Vector3Lerp(Vector3Scale(sp, 1.f / cp), tipP, 0.6f);
    A.stbd = Vector3Lerp(Vector3Scale(sn, 1.f / cn), tipN, 0.6f);
    A.cockpit = cc > 0 ? Vector3Scale(sc, 1.f / cc) : Vector3Scale(nose, nMin + 0.75f * (nMax - nMin));
    A.ok = true;
    fprintf(stderr, "LIGHTS anchors port=(%.2f %.2f %.2f) stbd=(%.2f %.2f %.2f) cockpit=(%.2f %.2f %.2f)\n",
            A.port.x, A.port.y, A.port.z, A.stbd.x, A.stbd.y, A.stbd.z, A.cockpit.x, A.cockpit.y, A.cockpit.z);
    return A;
}

void Render3D::queueShipLights(const Matrix& M, const ShipAnchors& A, Vector3 noz0, Vector3 noz1, float S,
                               float strobeT, float thrust, Color engine, bool player) {
    if (!A.ok || lights_.size() > 2000) return;
    const Camera3D& c = renderCamera();
    const float u = 0.075f; (void)S;   // light unit (world): the same lamp size on every hull (~8 px at 1440p)
    auto put = [&](Vector3 model, float size, Color col, float lift) {
        const Vector3 w = Vector3Transform(model, M);
        const Vector3 toCam = Vector3Normalize(Vector3Subtract(c.position, w));
        lights_.push_back({Vector3Add(w, Vector3Scale(toCam, lift * u)), size * u, col});   // just proud of the hull
    };
    auto lamp = [&](Vector3 model, Color col, float a, float halo, float lift) {
        put(model, halo, {col.r, col.g, col.b, (unsigned char)std::lround(Clamp(a * 0.85f, 0.f, 1.f) * 255.f)}, lift);   // soft halo
        put(model, halo * 0.36f, {(unsigned char)((col.r + 255) / 2), (unsigned char)((col.g + 255) / 2), (unsigned char)((col.b + 255) / 2),
                                  (unsigned char)std::lround(Clamp(a, 0.f, 1.f) * 255.f)}, lift);                             // hot core
    };
    // navigation: red port, green starboard (steady)
    lamp(A.port, {255, 36, 28, 255}, 0.95f, 3.6f, 0.6f);
    lamp(A.stbd, {40, 255, 90, 255}, 0.95f, 3.6f, 0.6f);
    // anti-collision strobes beside the nav lights: short white double flash, ~0.95 Hz, own phase
    const float f = strobeT * 0.95f - floorf(strobeT * 0.95f);
    static const bool strobeHold = getenv("SHIPLIGHT_STROBE") != nullptr;   // capture runs: strobes held on
    const float flash = (strobeHold || f < 0.045f || (f > 0.12f && f < 0.165f)) ? 1.f : 0.f;
    if (flash > 0.f) {
        const Vector3 aft = Vector3Scale(player ? Vector3{1, 0, 0} : Vector3{0, 0, -1}, 0.06f);
        lamp(Vector3Add(A.port, aft), {235, 245, 255, 255}, 1.f, 6.0f, 0.7f);
        lamp(Vector3Add(A.stbd, aft), {235, 245, 255, 255}, 1.f, 6.0f, 0.7f);
    }
    // engine nozzles: a soft glow that follows thrust
    const float t = Clamp(thrust, 0.f, 1.f);
    const float ea = 0.30f + 0.45f * t, es = 2.6f + 2.4f * t;
    lamp(noz0, engine, ea, es, 0.5f);
    lamp(noz1, engine, ea, es, 0.5f);
    // cockpit / window: faint
    lamp(A.cockpit, player ? Color{120, 200, 255, 255} : Color{255, 190, 120, 255}, 0.30f, 1.8f, 0.4f);
}

void Render3D::addGlow(Vector2 local, float depth, float sizeWorld, Color c) {
    if (!ok_ || lights_.size() >= 2000) return;
    lights_.push_back({toWorld(local, depth), sizeWorld, c});
}

void Render3D::addRing(Vector2 local, float depth, float radiusWorld, float tilt, Color c, float spinRad) {
    if (!ok_ || rings_.size() >= 16) return;
    rings_.push_back({toWorld(local, depth), radiusWorld, tilt, spinRad, c});
}

void Render3D::drawShipLights() {
    if ((lights_.empty() && rings_.empty()) || lightTex_.id == 0) { lights_.clear(); rings_.clear(); return; }
    rlDrawRenderBatchActive();
    rlEnableDepthTest();     // hulls in front hide the lamps (the far wingtip goes behind the wing)
    rlDisableDepthMask();    // but the sprites never occlude anything
    BeginBlendMode(BLEND_ADDITIVE);
    const Camera3D& c = renderCamera();
    for (const LightSprite& L : lights_) DrawBillboard(c, lightTex_, L.p, L.size, L.c);
    if (!rings_.empty() && ringTex_.id > 0) {   // t251: soft shockwave / shield rings, one textured quad each
        rlDisableBackfaceCulling();
        const Vector3 fwd = Vector3Normalize(Vector3Subtract(c.target, c.position));
        const Vector3 right = Vector3Normalize(Vector3CrossProduct(fwd, c.up));
        const Vector3 up = Vector3CrossProduct(right, fwd);
        rlSetTexture(ringTex_.id);
        rlBegin(RL_QUADS);
        for (const RingQ& R : rings_) {
            // plane spanned by U (screen-horizontal) and V (tilted from screen-vertical toward the view axis)
            const float cs = cosf(R.spin), sn = sinf(R.spin);
            Vector3 U = Vector3Add(Vector3Scale(right, cs), Vector3Scale(up, sn));
            Vector3 Vb = Vector3Add(Vector3Scale(up, cs), Vector3Scale(right, -sn));
            // the depth reach is capped short of the eye so a huge ring sweeps wide instead of crossing the near plane
            const float eye = fmaxf(1.f, Vector3DotProduct(Vector3Subtract(R.c, c.position), fwd));
            const Vector3 V = Vector3Add(Vector3Scale(Vb, cosf(R.tilt) * R.r), Vector3Scale(fwd, fminf(sinf(R.tilt) * R.r, 0.75f * eye)));
            U = Vector3Scale(U, R.r);
            rlColor4ub(R.col.r, R.col.g, R.col.b, R.col.a);
            const Vector3 p0 = Vector3Subtract(Vector3Subtract(R.c, U), V), p1 = Vector3Subtract(Vector3Add(R.c, U), V);
            const Vector3 p2 = Vector3Add(Vector3Add(R.c, U), V), p3 = Vector3Add(Vector3Subtract(R.c, U), V);
            rlTexCoord2f(0, 0); rlVertex3f(p0.x, p0.y, p0.z);
            rlTexCoord2f(1, 0); rlVertex3f(p1.x, p1.y, p1.z);
            rlTexCoord2f(1, 1); rlVertex3f(p2.x, p2.y, p2.z);
            rlTexCoord2f(0, 1); rlVertex3f(p3.x, p3.y, p3.z);
        }
        rlEnd();
        rlSetTexture(0);
        rlEnableBackfaceCulling();
    }
    EndBlendMode();          // (flushes the one batch)
    rlEnableDepthMask();
    lights_.clear(); rings_.clear();
}

void Render3D::endShips() {
    if (!ok_ || !inShips_) return;
    endPickupBatch();
    drawShipLights();
    EndMode3D();
    rlEnableBackfaceCulling();
    inShips_ = false;
}

void Render3D::endFrame() {
    if (!ok_) return;
    float tm = (float)GetTime();
    SetShaderValue(heatSh_, hTime_, &tm, SHADER_UNIFORM_FLOAT);
    if (heat_ && direct_) {
        // Direct path: bg + ships were drawn straight to the backbuffer. The heat shader needs the
        // scene behind the player's plume, so copy ONLY that plume's screen box into the scene
        // texture (same bottom-left origin as gl_FragCoord), then draw the refraction quads.
        const Texture2D& st = scene_.texture;
        const int W = GetScreenWidth(), H = GetScreenHeight();
        bool any = false;
        for (auto& p : plumes_) {
            if (!p.player) continue;
            const float hl = 5.0f * p.scale * p.unitPx * p.power, hw = 1.6f * p.scale * p.unitPx, r = sqrtf(hl * hl + hw * hw / 4) + 24;  // + refraction offset margin
            int x0 = std::max(0, (int)(p.nozzle.x - r)), x1 = std::min(W, (int)(p.nozzle.x + r) + 1);
            int y0 = std::max(0, (int)(p.nozzle.y - r)), y1 = std::min(H, (int)(p.nozzle.y + r) + 1);
            if (x1 <= x0 || y1 <= y0 || st.id == 0) continue;
            if (!any) { rlDrawRenderBatchActive(); glBindTexture(GL_TEXTURE_2D, st.id); any = true; }
            const int gy = H - y1;  // GL rows count from the bottom
            glCopyTexSubImage2D(GL_TEXTURE_2D, 0, x0, gy, x0, gy, x1 - x0, y1 - y0);
        }
        if (any) {
            glBindTexture(GL_TEXTURE_2D, 0);
            BeginShaderMode(heatSh_);
            for (auto& p : plumes_) {
                if (!p.player) continue;
                float hl = 5.0f * p.scale * p.unitPx * p.power, hw = 1.6f * p.scale * p.unitPx;
                DrawTexturePro(st, {0, 0, (float)st.width, (float)st.height}, {p.nozzle.x, p.nozzle.y, hl, hw}, {0, hw / 2}, p.angDeg, WHITE);
            }
            EndShaderMode();
        }
    } else if (heat_ && inSceneRt_) {
        EndTextureMode();
        inSceneRt_ = false;
        const Texture2D& st = scene_.texture;
        // Exact demo blit (Y-flip).
        DrawTexturePro(st, {0, 0, (float)st.width, -(float)st.height},
                       {0, 0, (float)GetScreenWidth(), (float)GetScreenHeight()}, {0, 0}, 0, WHITE);
        // Heat plumes: demo sizing hl=5*S*unitPx, hw=1.6*S*unitPx; sample scene RT via shader.
        BeginShaderMode(heatSh_);
        for (auto& p : plumes_) {
            if (!p.player) continue;  // demo-style: only the player refracts (avoids edge tearing)
            float hl = 5.0f * p.scale * p.unitPx * p.power, hw = 1.6f * p.scale * p.unitPx;
            DrawTexturePro(st, {0, 0, (float)st.width, (float)st.height},
                           {p.nozzle.x, p.nozzle.y, hl, hw}, {0, hw / 2}, p.angDeg, WHITE);
        }
        EndShaderMode();
    }
}

void Render3D::drawFlamesOnly() {
    if (!ok_ || !flames_) return;
    float tm = (float)GetTime();
    SetShaderValue(flameSh_, fTime_, &tm, SHADER_UNIFORM_FLOAT);
    BeginBlendMode(BLEND_ADDITIVE); BeginShaderMode(flameSh_);
    Color cur{0, 0, 0, 0};
    float rc[4] = {0, 0, 0, 0};
    SetShaderValue(flameSh_, fRecolor_, rc, SHADER_UNIFORM_VEC4);
    for (size_t i = 0; i < plumes_.size(); i++) {
        auto& p = plumes_[i];
        if (p.tint.r != cur.r || p.tint.g != cur.g || p.tint.b != cur.b || (p.tint.a > 0) != (cur.a > 0)) {
            rlDrawRenderBatchActive();  // flush plumes drawn with the previous colour before changing the uniform
            cur = p.tint;
            rc[0] = cur.r / 255.f; rc[1] = cur.g / 255.f; rc[2] = cur.b / 255.f; rc[3] = cur.a > 0 ? 1.f : 0.f;
            SetShaderValue(flameSh_, fRecolor_, rc, SHADER_UNIFORM_VEC4);
        }
        float j = 1 + 0.10f * sinf(tm * 37 + (float)i * 2.1f);
        float fl = 1.0f * p.scale * p.unitPx * j * p.power, fw = 1.0f * p.scale * p.unitPx * j * 101.f / 256.f * 0.8f;
        unsigned char br = (unsigned char)Clamp((215 + 40 * sinf(tm * 53 + (float)i)) * (0.55f + 0.45f * p.power), 0, 255);
        DrawTexturePro(flameTex_, {0, 0, (float)flameTex_.width, (float)flameTex_.height},
                       {p.nozzle.x, p.nozzle.y, fl, fw}, {fl * 0.04f, fw / 2}, p.angDeg,
                       p.tint.a > 0 ? Color{br, br, br, 255} : Color{br, br, 255, 255});
    }
    rlDrawRenderBatchActive();
    EndShaderMode(); EndBlendMode();
}

// ============================================================ damage: dents + breakup
float Render3D::enemyScale() const {
    return shipScale_ * (view_.mode == Mode::SideScroll ? cfg::kSideEnemyScaleMul : cfg::kTopDownEnemyScaleMul);
}

void Render3D::setDentUniforms(const float (*dent)[4], int n, bool skinned, float glow,
                               const float (*surfaceFx)[4]) {
    if (!ok_) return;
    Shader& sh = (skinned && dentSkinSh_.id > 0) ? dentSkinSh_ : dentSh_;
    const int locN = &sh == &dentSkinSh_ ? dsLocN_ : dLocN_, locD = &sh == &dentSkinSh_ ? dsLocDents_ : dLocDents_;
    const int locSurfaceFx = &sh == &dentSkinSh_ ? dsLocSurfaceFx_ : dLocSurfaceFx_;
    const int locG = &sh == &dentSkinSh_ ? dsLocGlow_ : dLocGlow_, locT = &sh == &dentSkinSh_ ? dsLocTime_ : dLocTime_;
    n = dent ? std::min(n, 8) : 0;
    SetShaderValue(sh, locN, &n, SHADER_UNIFORM_INT);
    if (n > 0) {
        SetShaderValueV(sh, locD, dent, SHADER_UNIFORM_VEC4, n);
    }
    if (locSurfaceFx >= 0) {
        float clearFx[8][4]{};
        SetShaderValueV(sh, locSurfaceFx, surfaceFx ? surfaceFx : clearFx, SHADER_UNIFORM_VEC4, 8);
    }
    const float tm = (float)GetTime();
    if (locG >= 0) SetShaderValue(sh, locG, &glow, SHADER_UNIFORM_FLOAT);
    if (locT >= 0) SetShaderValue(sh, locT, &tm, SHADER_UNIFORM_FLOAT);
}

void Render3D::loadBreakCfg(const std::string& path) {
    std::ifstream f(path);
    if (!f) { fprintf(stderr, "BREAKUP missing %s (defaults)\n", path.c_str()); return; }
    nlohmann::json j;
    try { j = nlohmann::json::parse(f); } catch (const std::exception& e) { fprintf(stderr, "BREAKUP bad json: %s\n", e.what()); return; }
#define G(k) bc_.k = j.value(#k, bc_.k)
    G(dentMax); G(dentRadius); G(dentDepthPerDamage); G(dentMaxDepth); G(dentMerge); G(dentDarken); G(maxBreakups); G(chunks);
    G(chunkSpeedMin); G(chunkSpeedMax); G(chunkDrag); G(chunkSpinMin); G(chunkSpinMax); G(chunkLife); G(chunkShrinkStart);
    G(chunkExplosionScale); G(chunkExplosionGain); G(chunkExplosionDelayMax); G(shardsMin); G(shardsMax); G(shardSizeMin); G(shardSizeMax);
    G(shardSpeedMin); G(shardSpeedMax); G(shardDrag); G(shardLife); G(maxShards);
#undef G
    bc_.dentMax = std::clamp(bc_.dentMax, 0, 8); bc_.chunks = std::clamp(bc_.chunks, 2, kMaxChunks); bc_.maxBreakups = std::clamp(bc_.maxBreakups, 0, 8);
}

// Fracture the enemy model at load: k-means on triangle centroids -> bc_.chunks clusters, one unindexed
// mesh per (cluster, source mesh), vertices re-centred on the cluster pivot (open shells, lit both sides).
void Render3D::buildChunks(EnemyModel& E) {
    struct Tri { int mesh; int v[3]; Vector3 c; };
    std::vector<Tri> tris;
    E.bbMin = {1e9f, 1e9f, 1e9f}; E.bbMax = {-1e9f, -1e9f, -1e9f};
    for (int mi = 0; mi < E.m.meshCount; mi++) {
        const Mesh& m = E.m.meshes[mi];
        auto P = [&](int i) { return Vector3{m.vertices[i * 3], m.vertices[i * 3 + 1], m.vertices[i * 3 + 2]}; };
        for (int i = 0; i < m.vertexCount; i++) { Vector3 v = P(i); E.bbMin = Vector3Min(E.bbMin, v); E.bbMax = Vector3Max(E.bbMax, v); }
        for (int t = 0; t < m.triangleCount; t++) {
            Tri tr{mi, {}, {}};
            for (int k = 0; k < 3; k++) tr.v[k] = m.indices ? m.indices[t * 3 + k] : t * 3 + k;
            tr.c = Vector3Scale(Vector3Add(Vector3Add(P(tr.v[0]), P(tr.v[1])), P(tr.v[2])), 1 / 3.f);
            tris.push_back(tr);
        }
    }
    const int K = E.file.find("enemy_warden") != std::string::npos ? kMaxChunks : bc_.chunks;   // t251: the boss breaks into more parts
    if (tris.size() < (size_t)K * 4) return;
    // seeds: farthest-point from the centroid, deterministic
    std::vector<Vector3> cen(K);
    cen[0] = tris[0].c;
    for (int k = 1; k < K; k++) {
        float best = -1; for (auto& tr : tris) { float dm = 1e9f; for (int j = 0; j < k; j++) dm = std::min(dm, Vector3DistanceSqr(tr.c, cen[j])); if (dm > best) { best = dm; cen[k] = tr.c; } }
    }
    std::vector<int> lab(tris.size(), 0);
    for (int it = 0; it < 10; it++) {
        std::vector<Vector3> sum(K, Vector3{0, 0, 0}); std::vector<int> cnt(K, 0);
        for (size_t i = 0; i < tris.size(); i++) {
            int b = 0; float bd = 1e9f;
            for (int k = 0; k < K; k++) { float d = Vector3DistanceSqr(tris[i].c, cen[k]); if (d < bd) { bd = d; b = k; } }
            lab[i] = b; sum[b] = Vector3Add(sum[b], tris[i].c); cnt[b]++;
        }
        for (int k = 0; k < K; k++) if (cnt[k]) cen[k] = Vector3Scale(sum[k], 1.f / cnt[k]);
    }
    E.nChunks = K;
    for (int k = 0; k < K; k++) E.pivots[k] = cen[k];
    for (int k = 0; k < K; k++)
        for (int mi = 0; mi < E.m.meshCount; mi++) {
            const Mesh& src = E.m.meshes[mi];
            int n = 0; for (size_t i = 0; i < tris.size(); i++) n += lab[i] == k && tris[i].mesh == mi;
            if (!n) continue;
            Mesh m{}; m.vertexCount = n * 3; m.triangleCount = n;
            m.vertices = (float*)MemAlloc(n * 9 * sizeof(float));
            m.normals = src.normals ? (float*)MemAlloc(n * 9 * sizeof(float)) : nullptr;
            m.texcoords = src.texcoords ? (float*)MemAlloc(n * 6 * sizeof(float)) : nullptr;
            int w = 0;
            for (size_t i = 0; i < tris.size(); i++) {
                if (lab[i] != k || tris[i].mesh != mi) continue;
                for (int c = 0; c < 3; c++, w++) {
                    int v = tris[i].v[c];
                    m.vertices[w * 3] = src.vertices[v * 3] - cen[k].x; m.vertices[w * 3 + 1] = src.vertices[v * 3 + 1] - cen[k].y; m.vertices[w * 3 + 2] = src.vertices[v * 3 + 2] - cen[k].z;
                    if (m.normals) for (int a = 0; a < 3; a++) m.normals[w * 3 + a] = src.normals[v * 3 + a];
                    if (m.texcoords) for (int a = 0; a < 2; a++) m.texcoords[w * 2 + a] = src.texcoords[v * 2 + a];
                }
            }
            UploadMesh(&m, false);
            E.chunks.push_back({m, E.m.meshMaterial ? E.m.meshMaterial[mi] : 0, k});
        }
    fprintf(stderr, "BREAKUP %s chunks=%d meshes=%d tris=%zu bbox=%.2fx%.2fx%.2f\n", GetFileName(E.file.c_str()), K, (int)E.chunks.size(), tris.size(), E.bbMax.x - E.bbMin.x, E.bbMax.y - E.bbMin.y, E.bbMax.z - E.bbMin.z);
}

Vector2 Render3D::hullContact(Vector2 shipLocal, Vector2 hitLocal, Vector2 dirLocal, int model, float depth) const {
    if (!ok_) return hitLocal;
    const Vector3 bbMin_ = em(model).bbMin, bbMax_ = em(model).bbMax;
    const Matrix M = makeShipMatrix(toWorld(shipLocal, depth), true, 0, 0, enemyScale() * em(model).scaleMul);
    float x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f;
    for (int c = 0; c < 8; c++) {
        Vector3 q{c & 1 ? bbMax_.x : bbMin_.x, c & 2 ? bbMax_.y : bbMin_.y, c & 4 ? bbMax_.z : bbMin_.z};
        Vector2 l = view_.toLocal(GetWorldToScreen(Vector3Transform(q, M), cam_));
        x0 = std::min(x0, l.x); x1 = std::max(x1, l.x); y0 = std::min(y0, l.y); y1 = std::max(y1, l.y);
    }
    const float w = x1 - x0, h = y1 - y0;
    // the bbox corners overshoot the tapered hull: keep the hit inside its middle band
    const float y = Clamp(hitLocal.y, y0 + 0.3f * h, y1 - 0.3f * h);
    // the bolt enters from the side it travels from; land just inside that skin
    const float x = dirLocal.x >= 0 ? x0 + 0.18f * w : x1 - 0.18f * w;
    return {x, y};
}

Rectangle Render3D::shipScreenRect(Vector2 shipLocal, int model, float depth) const {
    if (!ok_) return {0, 0, 0, 0};
    const Vector3 bbMin_ = em(model).bbMin, bbMax_ = em(model).bbMax;
    const Matrix M = makeShipMatrix(toWorld(shipLocal, depth), true, 0, 0, enemyScale() * em(model).scaleMul);
    float x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f;
    for (int c = 0; c < 8; c++) {
        const Vector3 q{c & 1 ? bbMax_.x : bbMin_.x, c & 2 ? bbMax_.y : bbMin_.y, c & 4 ? bbMax_.z : bbMin_.z};
        const Vector2 p = GetWorldToScreen(Vector3Transform(q, M), renderCamera());
        x0 = std::min(x0, p.x); x1 = std::max(x1, p.x); y0 = std::min(y0, p.y); y1 = std::max(y1, p.y);
    }
    return {x0, y0, x1 - x0, y1 - y0};
}

int Render3D::addDent(float (*dent)[4], int& n, int& next, Vector2 hitLocal, Vector2 shipLocal, float damage, int model, float depth, float (*surfaceFx)[4]) const {
    if (!ok_ || bc_.dentMax <= 0) return -1;
    const Vector3 bbMin_ = em(model).bbMin, bbMax_ = em(model).bbMax;
    const Vector3 shipW = toWorld(shipLocal, depth);
    const Matrix M = makeShipMatrix(shipW, true, 0, 0, enemyScale() * em(model).scaleMul);
    const Matrix inv = MatrixInvert(M);
    Vector3 p = Vector3Transform(toWorld(hitLocal, depth), inv);
    // the hit lands on the playfield mid-plane: push it out to the hull surface facing the camera
    Vector3 camW = Vector3Normalize(Vector3Subtract(cam_.position, shipW));  // toward the (perspective) eye
    Vector3 d = Vector3Normalize(Vector3Subtract(Vector3Transform(camW, inv), Vector3Transform({0, 0, 0}, inv)));
    p = Vector3Clamp(p, bbMin_, bbMax_);
    float ext = -1e9f;
    for (int c = 0; c < 8; c++) {
        Vector3 q{c & 1 ? bbMax_.x : bbMin_.x, c & 2 ? bbMax_.y : bbMin_.y, c & 4 ? bbMax_.z : bbMin_.z};
        ext = std::max(ext, Vector3DotProduct(q, d));
    }
    p = Vector3Add(p, Vector3Scale(d, std::max(0.f, (ext - Vector3DotProduct(p, d)) * 0.7f)));
    const float add = bc_.dentDepthPerDamage * damage;
    for (int i = 0; i < n; i++)  // strengthen a nearby dent instead of stacking a new one
        if (Vector3Distance({dent[i][0], dent[i][1], dent[i][2]}, p) < bc_.dentMerge) { dent[i][3] = std::min(bc_.dentMaxDepth, dent[i][3] + add); return i; }
    int slot = n < bc_.dentMax ? n++ : (next++ % bc_.dentMax);  // full: replace the oldest
    dent[slot][0] = p.x; dent[slot][1] = p.y; dent[slot][2] = p.z; dent[slot][3] = std::min(bc_.dentMaxDepth, add);
    if (surfaceFx) for (int k = 0; k < 4; k++) surfaceFx[slot][k] = 0.f;
    return slot;
}

bool Render3D::spawnBreakup(Vector2 local, float pitch, float roll, const float (*dent)[4], int nDent, Color tint, int model, float depth) {
    if (model < 0 || model >= (int)em_.size()) model = 0;
    const EnemyModel& E = em_[model];
    if (!ok_ || E.nChunks == 0) return false;
    Break* b = nullptr; int live = 0;
    for (int i = 0; i < bc_.maxBreakups; i++) { if (breaks_[i].on) live++; else if (!b) b = &breaks_[i]; }
    if (!b) return false;
    *b = Break{};
    b->on = true; b->tint = tint; b->model = model;
    b->M0 = makeShipMatrix(toWorld(local, depth), true, pitch, roll, enemyScale() * E.scaleMul);
    b->nDent = std::min(nDent, 8);
    for (int i = 0; i < b->nDent; i++) for (int k = 0; k < 4; k++) b->dent[i][k] = dent[i][k];
    const Vector3 c0 = Vector3Transform({0, 0, 0}, b->M0);
    for (int k = 0; k < E.nChunks; k++) {
        Vector3 dir = Vector3Subtract(Vector3Transform(E.pivots[k], b->M0), c0);
        // keep the motion in the playfield plane so chunks stay readable (side: XY, overhead: XZ)
        if (view_.mode == Mode::TopDown) dir.y *= 0.2f; else dir.z *= 0.2f;
        if (Vector3Length(dir) < 1e-3f) dir = {frand(-1, 1), frand(-1, 1), 0};
        dir = Vector3Normalize(Vector3Add(Vector3Normalize(dir), {frand(-0.3f, 0.3f), frand(-0.3f, 0.3f), frand(-0.3f, 0.3f)}));
        b->vel[k] = Vector3Scale(dir, frand(bc_.chunkSpeedMin, bc_.chunkSpeedMax));
        b->axis[k] = Vector3Normalize({frand(-1, 1), frand(-1, 1), frand(-1, 1)});
        b->spin[k] = frand(bc_.chunkSpinMin, bc_.chunkSpinMax) * (GetRandomValue(0, 1) ? 1 : -1);
        b->delay[k] = frand(0, bc_.chunkExplosionDelayMax);
    }
    return true;
}

bool Render3D::spawnBossBreakup(Vector2 local, float pitch, float roll, const float (*dent)[4], int nDent, Color tint, int model, float depth,
                                float speedMul, float spinMul, float life, float delayMax, float hot, float blastScale) {
    if (!spawnBreakup(local, pitch, roll, dent, nDent, tint, model, depth)) return false;
    Break* b = nullptr;   // the slot spawnBreakup just filled (t == 0, newest)
    for (auto& x : breaks_) if (x.on && x.t == 0.f && x.model == model) b = &x;
    if (!b) return true;
    const EnemyModel& E = em(model);
    b->life = life; b->hot = hot; b->blastScale = blastScale; b->shrinkStart = 0.82f;
    const Vector3 c0 = Vector3Transform({0, 0, 0}, b->M0);
    for (int k = 0; k < E.nChunks; k++) {
        // outward from the hull centre: farther parts fly faster (inside-out burst), a little lift toward the camera
        Vector3 dir = Vector3Subtract(Vector3Transform(E.pivots[k], b->M0), c0);
        const float reach = Vector3Length(dir);
        if (view_.mode == Mode::TopDown) dir.y *= 0.25f; else dir.z *= 0.25f;
        if (Vector3Length(dir) < 1e-3f) dir = {frand(-1, 1), frand(-1, 1), 0};
        dir = Vector3Normalize(Vector3Add(Vector3Normalize(dir), {frand(-0.2f, 0.2f), frand(-0.2f, 0.2f), frand(-0.15f, 0.15f)}));
        b->vel[k] = Vector3Scale(dir, frand(bc_.chunkSpeedMin, bc_.chunkSpeedMax) * speedMul * (0.7f + 0.25f * reach));
        b->spin[k] *= spinMul;
        b->delay[k] = frand(0.f, delayMax);
    }
    return true;
}

void Render3D::updateBreakups(float dt, std::vector<ChunkBlast>& out) {
    if (!ok_) return;
    const float drag0 = std::exp(-bc_.chunkDrag * dt);
    for (auto& b : breaks_) {
        if (!b.on) continue;
        b.t += dt;
        const float drag = b.life > 0.f ? std::exp(-0.35f * dt) : drag0;   // t251: heavy boss parts coast
        const EnemyModel& E = em(b.model);
        for (int k = 0; k < E.nChunks; k++) {
            b.vel[k] = Vector3Scale(b.vel[k], drag);
            b.off[k] = Vector3Add(b.off[k], Vector3Scale(b.vel[k], dt));
            b.ang[k] += b.spin[k] * dt;
            if (!b.fired[k] && b.t >= b.delay[k]) {
                b.fired[k] = true;
                Vector3 w = Vector3Add(Vector3Transform(E.pivots[k], b.M0), b.off[k]);
                Vector2 sp = GetWorldToScreen(w, renderCamera()), sp2 = GetWorldToScreen(Vector3Add(w, b.vel[k]), renderCamera());
                out.push_back({sp, Vector2Normalize(Vector2Subtract(sp2, sp)), b.tint, b.blastScale});
            }
        }
        if (b.t >= (b.life > 0.f ? b.life : bc_.chunkLife)) b.on = false;
    }
}

void Render3D::drawBreakups() {
    if (!ok_) return;
    for (auto& b : breaks_) {
        if (!b.on) continue;
        const float life = b.life > 0.f ? b.life : bc_.chunkLife, ss = b.shrinkStart > 0.f ? b.shrinkStart : bc_.chunkShrinkStart;
        const float u = b.t / life, s = u < ss ? 1.f : std::max(0.f, 1 - (u - ss) / (1 - ss));
        if (s <= 0.01f) continue;
        const EnemyModel& E = em(b.model);
        const float heat = b.hot * std::exp(-b.t / 1.6f);   // t251: torn boss parts glow hot, then cool
        for (const ChunkMesh& cm : E.chunks) {
            const int k = cm.cluster;
            float dl[8][4];  // chunks keep the ship's dents: shift them into the pivot-centred chunk space
            for (int i = 0; i < b.nDent; i++) { dl[i][0] = b.dent[i][0] - E.pivots[k].x; dl[i][1] = b.dent[i][1] - E.pivots[k].y; dl[i][2] = b.dent[i][2] - E.pivots[k].z; dl[i][3] = b.dent[i][3]; }
            setDentUniforms(dl, b.nDent);
            // chunk-local (pivot-centred) -> spin + shrink about the pivot -> back to model -> ship -> drift
            Matrix T = MatrixMultiply(MatrixMultiply(MatrixScale(s, s, s), MatrixRotate(b.axis[k], b.ang[k])), MatrixTranslate(E.pivots[k].x, E.pivots[k].y, E.pivots[k].z));
            T = MatrixMultiply(MatrixMultiply(T, b.M0), MatrixTranslate(b.off[k].x, b.off[k].y, b.off[k].z));
            Material mat = E.m.materials[cm.mat];
            mat.shader = dentSh_;  // chunks are rest-pose copies: never skinned
            mat.maps[MATERIAL_MAP_DIFFUSE].color = b.tint;
            DrawMesh(cm.mesh, mat, T);
            if (heat > 0.02f) {   // emissive heat on the part itself (no light is cast on anything else)
                static Material hotMat = LoadMaterialDefault();
                hotMat.maps[MATERIAL_MAP_DIFFUSE].color = {255, (unsigned char)(90 + 80 * heat), 30, (unsigned char)std::lround(Clamp(heat, 0.f, 1.f) * 150.f)};
                rlDrawRenderBatchActive();
                BeginBlendMode(BLEND_ADDITIVE);
                rlDisableDepthMask();
                DrawMesh(cm.mesh, hotMat, T);
                rlDrawRenderBatchActive();
                rlEnableDepthMask();
                EndBlendMode();
            }
        }
    }
}

// ============================================================ 3D background: planets + terrain
Model& Render3D::terrainStrip(BgModel& b, int side, int cells) {
    const int key = side * 1000 + cells;
    auto it = b.strips.find(key);
    if (it != b.strips.end()) return it->second;
    // Same lattice as GenMeshPlane(gridSize, gridSize, segments, segments): x_i = i*g/seg - g/2, so
    // every strip vertex sits exactly on a vertex of the full tile (the shader computes the height
    // from the position, so the strip is that part of the tile, bit for bit).
    const int seg = b.segments, i0 = side > 0 ? seg - cells : 0, i1 = side > 0 ? seg : cells;
    const int nx = i1 - i0 + 1, nz = seg + 1;
    const float g = b.gridSize, step = g / seg;
    Mesh m{};
    m.vertexCount = nx * nz; m.triangleCount = (nx - 1) * (nz - 1) * 2;
    m.vertices = (float*)MemAlloc(m.vertexCount * 3 * sizeof(float));
    m.normals = (float*)MemAlloc(m.vertexCount * 3 * sizeof(float));
    m.texcoords = (float*)MemAlloc(m.vertexCount * 2 * sizeof(float));
    m.indices = (unsigned short*)MemAlloc(m.triangleCount * 3 * sizeof(unsigned short));
    for (int z = 0; z < nz; z++)
        for (int x = 0; x < nx; x++) {
            const int v = z * nx + x, i = i0 + x;
            m.vertices[v * 3 + 0] = i * step - g * 0.5f; m.vertices[v * 3 + 1] = 0; m.vertices[v * 3 + 2] = z * step - g * 0.5f;
            m.normals[v * 3 + 0] = 0; m.normals[v * 3 + 1] = 1; m.normals[v * 3 + 2] = 0;
            m.texcoords[v * 2 + 0] = (float)i / seg; m.texcoords[v * 2 + 1] = (float)z / seg;
        }
    int k = 0;
    for (int z = 0; z < nz - 1; z++)
        for (int x = 0; x < nx - 1; x++) {
            const unsigned short a = (unsigned short)(z * nx + x), bq = (unsigned short)(a + 1), c = (unsigned short)(a + nx), d = (unsigned short)(c + 1);
            m.indices[k++] = a; m.indices[k++] = c; m.indices[k++] = bq;
            m.indices[k++] = bq; m.indices[k++] = c; m.indices[k++] = d;
        }
    UploadMesh(&m, false);
    Model model = LoadModelFromMesh(m);
    for (int i = 0; i < model.materialCount; i++) model.materials[i].shader = terrainProcSh_;
    return b.strips[key] = model;
}

Render3D::BgModel& Render3D::bgModel(const std::string& name) {
    auto it = bgModels_.find(name);
    if (it != bgModels_.end()) return it->second;
    BgModel& b = bgModels_[name];
    const std::string p = root_ + "/models/" + name;
    if (name.empty() || !FileExists(p.c_str())) { fprintf(stderr, "BG3D missing model %s\n", p.c_str()); return b; }
    const bool descriptor = name.size() >= 5 && name.compare(name.size() - 5, 5, ".json") == 0;
    if (descriptor) {
        try {
            std::ifstream in(p);
            nlohmann::json j; in >> j;
            if (j.at("type").get<std::string>() == "chunk_terrain") {
                // Level 6: seamless chunked ground (planet_terrain.cpp). Density / LOD knobs come from the
                // descriptor (an "Arm" suffix overrides on the tablet build) and PLANET_* env vars.
#if defined(__aarch64__)
                const char* sfx = "Arm";
#else
                const char* sfx = "";
#endif
                auto num = [&](const char* k, double def) {
                    const std::string ks = std::string(k) + sfx;
                    double v = j.contains(ks) ? j.at(ks).get<double>() : j.value(k, def);
                    std::string env = "PLANET_"; for (const char* c = k; *c; c++) env += (char)toupper((unsigned char)*c);
                    if (const char* e = getenv(env.c_str())) v = atof(e);
                    return v;
                };
                planet::Params pp;
                pp.seed = (std::uint32_t)num("seed", 6006);
                pp.chunkSize = (float)num("chunkSize", 8);
                pp.segments = std::clamp((int)num("segments", 32), 4, 128);
                pp.texRes = std::clamp((int)num("texRes", 256), 32, 512);
                pp.heightScale = (float)num("heightScale", 1);
                pp.waterLevel = (float)num("waterLevel", -1.6);
                pp.propDensity = (float)num("propDensity", 1);
                pp.propDetail = (int)num("propDetail", 1);
                pp.gunSpacing = (float)num("gunSpacing", 11);
                pp.gunLane = (float)num("gunLane", 5);
                planetPropFar_ = (float)num("propFar", 54);
                if (!planet_) planet_ = std::make_unique<PlanetRenderer>();
                b.chunked = true;
                b.ok = planet_->init(pp, (int)num("workers", 2));
                fprintf(stderr, "BG3D chunk terrain %s seg=%d tex=%d props=%.2f detail=%d ok=%d\n", name.c_str(), pp.segments, pp.texRes, pp.propDensity, pp.propDetail, b.ok);
                return b;
            }
            if (j.at("type").get<std::string>() != "shader_terrain") throw std::runtime_error("unsupported terrain descriptor type");
            b.gridSize = j.at("gridSize").get<float>();
            const int segments = j.at("segments").get<int>();
            b.heightScale = j.at("heightScale").get<float>();
            b.frequency = j.at("frequency").get<float>();
            b.ridgeSharpness = j.at("ridgeSharpness").get<float>();
            const std::int64_t seed = j.at("seed").get<std::int64_t>();
            const int craterCount = (int)std::lround(j.at("craterDensity").get<double>());
            b.waterLevel = j.at("waterLevel").get<float>();
            b.hasWater = j.at("hasWater").get<float>() > 0.5f;
            b.waterAlpha = j.value("waterOpacity", 0.72f);
            b.low = terrainColor(j.at("lowColor").get<std::string>());
            b.mid = terrainColor(j.at("midColor").get<std::string>());
            b.high = terrainColor(j.at("highColor").get<std::string>());
            b.waterColor = terrainColor(j.at("waterColor").get<std::string>());
            if (b.gridSize <= 0 || segments < 16 || segments > 254 || craterCount < 0 || craterCount > kMaxTerrainCraters)
                throw std::runtime_error("terrain dimensions or crater count out of range");

            TerrainPrng random(seed);
            unsigned char permutation[256];
            for (int i = 0; i < 256; i++) permutation[i] = (unsigned char)i;
            for (int i = 255; i > 0; i--) {
                const int r = (int)std::floor(random.next() * (i + 1));
                std::swap(permutation[i], permutation[r]);
            }
            b.craterCount = craterCount;
            for (int i = 0; i < craterCount; i++) {
                b.craters[i] = {
                    (float)((random.next() - 0.5) * b.gridSize * 0.7),
                    (float)((random.next() - 0.5) * b.gridSize * 0.7),
                    (float)(1.0 + random.next() * 2.5),
                    (float)(0.5 + random.next() * 1.2)
                };
            }

            b.minHeight = 1e9f; b.maxHeight = -1e9f;
            for (int z = 0; z <= segments; z++) {
                const double pz = ((double)z / segments - 0.5) * b.gridSize;
                for (int x = 0; x <= segments; x++) {
                    const double px = ((double)x / segments - 0.5) * b.gridSize;
                    const float h = (float)terrainHeight(permutation, b.craters, craterCount, px, pz,
                                                         b.heightScale, b.frequency, b.ridgeSharpness);
                    b.minHeight = std::min(b.minHeight, h); b.maxHeight = std::max(b.maxHeight, h);
                }
            }

            b.segments = segments;
            b.m = LoadModelFromMesh(GenMeshPlane(b.gridSize, b.gridSize, segments, segments));
            if (b.hasWater) b.water = LoadModelFromMesh(GenMeshPlane(b.gridSize, b.gridSize, 1, 1));
            auto* pixels = (unsigned char*)MemAlloc(256);
            std::memcpy(pixels, permutation, 256);
            Image image{pixels, 256, 1, 1, PIXELFORMAT_UNCOMPRESSED_GRAYSCALE};
            b.permutation = LoadTextureFromImage(image);
            UnloadImage(image);
            SetTextureFilter(b.permutation, TEXTURE_FILTER_POINT);
            for (int i = 0; i < b.m.materialCount; i++) b.m.materials[i].shader = terrainProcSh_;
            for (int i = 0; i < b.water.materialCount; i++) b.water.materials[i].shader = terrainProcSh_;
            b.bmin = {-b.gridSize * 0.5f, b.minHeight, -b.gridSize * 0.5f};
            b.bmax = { b.gridSize * 0.5f, std::max(b.maxHeight, b.hasWater ? b.waterLevel : b.maxHeight), b.gridSize * 0.5f};
            b.proceduralTerrain = true;
            b.ok = b.m.meshCount > 0 && b.permutation.id > 0 && terrainProcSh_.id > 0;
            const int tris = b.m.meshCount > 0 ? b.m.meshes[0].triangleCount : 0;
            fprintf(stderr, "BG3D shader terrain %s tris=%d craters=%d height=%.4f..%.4f water=%.3f ok=%d\n",
                    name.c_str(), tris + (b.hasWater ? 2 : 0), craterCount, b.minHeight, b.maxHeight, b.waterLevel, b.ok);
        } catch (const std::exception& e) {
            fprintf(stderr, "BG3D invalid shader terrain %s: %s\n", p.c_str(), e.what());
        }
        return b;
    }
    b.m = LoadModel(p.c_str());
    if (b.m.meshCount == 0) return b;
    if (name == "title_exoplanet_surface_4k.glb") {
        for (int materialIndex = 0; materialIndex < b.m.materialCount; ++materialIndex) {
            for (int mapIndex : {MATERIAL_MAP_ALBEDO, MATERIAL_MAP_NORMAL}) {
                Texture2D& texture = b.m.materials[materialIndex].maps[mapIndex].texture;
                if (texture.id == 0 || texture.width < 512 || texture.height < 512) continue;
                if (texture.mipmaps <= 1) GenTextureMipmaps(&texture);
                SetTextureFilter(texture, TEXTURE_FILTER_TRILINEAR);
            }
        }
    }
    BoundingBox bb = GetModelBoundingBox(b.m);
    b.bmin = bb.min; b.bmax = bb.max;
    b.rMin = 1e9f; b.rMax = 0; int tris = 0;
    for (int i = 0; i < b.m.meshCount; i++) {
        const Mesh& m = b.m.meshes[i]; tris += m.triangleCount;
        for (int v = 0; v < m.vertexCount; v++) {
            float r = Vector3Length({m.vertices[3 * v], m.vertices[3 * v + 1], m.vertices[3 * v + 2]});
            b.rMin = std::min(b.rMin, r); b.rMax = std::max(b.rMax, r);
        }
    }
    b.ok = true;
    fprintf(stderr, "BG3D model %s tris=%d bbox=(%.2f,%.2f,%.2f)-(%.2f,%.2f,%.2f) r=%.3f..%.3f\n", name.c_str(), tris,
            bb.min.x, bb.min.y, bb.min.z, bb.max.x, bb.max.y, bb.max.z, b.rMin, b.rMax);
    return b;
}

void Render3D::releaseTitleAssets() {
    for (auto it = bgModels_.begin(); it != bgModels_.end();) {
        const std::string& name = it->first;
        if (name.rfind("title_", 0) != 0 && name.rfind("title_glyphs/", 0) != 0) {
            ++it;
            continue;
        }
        if (it->second.m.meshCount > 0) UnloadModel(it->second.m);
        if (it->second.water.meshCount > 0) UnloadModel(it->second.water);
        if (it->second.permutation.id > 0) UnloadTexture(it->second.permutation);
        it = bgModels_.erase(it);
    }
}

void Render3D::drawShipCorridor(float elapsedSeconds, float scrollTime) {
    if (!ok_ || view_.mode != Mode::SideScroll) return;
    BgModel& b = bgModel("ship_interior/modular_v5/corridor_section.glb");
    if (!b.ok) return;
    for (int i = 0; i < b.m.materialCount; ++i) b.m.materials[i].shader = metal_;
    const Vector3 eye = cam_.position;
    const float metalness = .78f, roughness = .24f, envStrength = .90f;
    const float opacity = 1.f;
    SetShaderValue(metal_, metalEye_, &eye, SHADER_UNIFORM_VEC3);
    SetShaderValue(metal_, metalness_, &metalness, SHADER_UNIFORM_FLOAT);
    SetShaderValue(metal_, metalRoughness_, &roughness, SHADER_UNIFORM_FLOAT);
    SetShaderValue(metal_, metalEnvStrength_, &envStrength, SHADER_UNIFORM_FLOAT);
    SetShaderValue(metal_, metalOpacity_, &opacity, SHADER_UNIFORM_FLOAT);
    const float halfW = CameraSystem::kViewHeight * 0.5f * view_.pf.width / fmaxf(view_.pf.height, 1.f);
    const float scale = CameraSystem::kViewHeight / 8.8f;
    const float tileWidth = (b.bmax.x - b.bmin.x) * scale;
    if (tileWidth <= 0.001f) return;
    const float centerX = (b.bmin.x + b.bmax.x) * 0.5f;
    const float offset = std::fmod(fmaxf(scrollTime, 0.f) * Corridor::kWorldUnitsPerScrollSecond, tileWidth);
    const int count = (int)std::ceil((2.f * halfW) / tileWidth) + 2;
    rlDrawRenderBatchActive();
    BeginMode3D(cam_);
    // The corridor sits ~5 units behind the play plane. Under the perspective camera, magnify it
    // across the view by the matching amount so its walls still frame the playfield edges.
    rlPushMatrix();
    rlMultMatrixf(MatrixToFloat(CameraSystem::backdropLayer(cam_, 5.f)));
    struct CorridorGlowSphere { Vector3 pos; float radius; Color color; };
    struct CorridorGlowCube { Vector3 pos; Vector3 size; Color color; };
    std::vector<CorridorGlowSphere> glowSpheres;
    std::vector<CorridorGlowCube> glowCubes;
    for (int i = 0; i < count; ++i) {
        const float x = -halfW - tileWidth * 0.5f + i * tileWidth - offset;
        // modular_v5's authored bounds are y=(-1.05, .86); center the shell by
        // 0.095 local units so the ceiling and floor seat symmetrically.
        Matrix m = MatrixMultiply(MatrixTranslate(-centerX, 0.095f, 0), MatrixRotateX(PI / 2.f));
        m = MatrixMultiply(m, MatrixScale(scale, scale, scale));
        b.m.transform = MatrixMultiply(m, MatrixTranslate(x, 0, -5));
        DrawModel(b.m, {0, 0, 0}, 1, WHITE);
    }
    // Animated machinery is deliberately built from a small repeatable kit. It sits just in
    // front of the static shell, which gives the corridor life without increasing texture memory
    // or turning the tablet scene into a draw-call/triangle-heavy set piece.
    const float machinePitch = tileWidth * 0.58f;
    const float machineScroll = std::fmod(fmaxf(scrollTime, 0.f) * Corridor::kWorldUnitsPerScrollSecond, machinePitch);
    const int machines = (int)std::ceil((2.f * halfW) / machinePitch) + 2;
    for (int i = 0; i < machines; ++i) {
        const float x = -halfW - machinePitch * 0.5f + i * machinePitch - machineScroll;
        // A stable, inexpensive sequence gives each bay a role without random popping while
        // it scrolls.  It deliberately does not repeat at the same cadence as the shell tile.
        const unsigned bayHash = (unsigned)(i * 1103515245u + 12345u);
        const int bayRole = (int)((bayHash >> 16) % 5u);  // gantry, reactor, cargo, defense, service
        const float phase = elapsedSeconds * (2.05f + 0.21f * (float)(bayRole % 3)) + i * 1.71f;
        const float piston = 0.30f + 0.26f * (0.5f + 0.5f * sinf(phase));
        const bool reactor = bayRole == 1;
        const bool cargoBay = bayRole == 2;
        const bool serviceBay = bayRole == 4;
        for (int side : {-1, 1}) {
            const float y = side * (5.10f + (cargoBay ? 0.10f : 0.f));
            const Vector3 mount{x, y, -4.45f};
            const Color housing = reactor ? Color{24, 29, 38, 255} : cargoBay ? Color{43, 35, 30, 255} : Color{18, 23, 29, 255};
            const float housingWidth = cargoBay ? 1.20f : serviceBay ? 0.56f : 0.82f;
            DrawCube(mount, housingWidth, 0.72f, 0.42f, housing);
            DrawCubeWires(mount, housingWidth + 0.02f, 0.74f, 0.44f, {90, 70, 45, 190});
            const Vector3 anchor{x, y - side * 0.24f, -4.18f};
            const Vector3 rodEnd{x, y - side * (0.62f + piston), -4.18f};
            if (!serviceBay) {
                DrawCylinderEx(anchor, rodEnd, 0.085f, 0.075f, 10, {82, 91, 102, 255});
                DrawCylinderEx(rodEnd, {x, y - side * (0.83f + piston), -4.18f}, 0.16f, 0.11f, 10, {32, 38, 46, 255});
            }
            if (reactor) {
                const float beat = 0.55f + 0.45f * sinf(phase * 1.9f);
                glowSpheres.push_back({{x, y - side * 0.43f, -4.04f}, 0.13f + beat * 0.045f,
                                       Fade({40, 210, 255, 255}, 0.28f + beat * 0.25f)});
            }
            if (cargoBay) {
                // Cargo clamps and a slow travelling scanner turn selected bays into broad,
                // industrial spaces instead of another copy of the narrow piston station.
                const float carriage = 0.30f * sinf(elapsedSeconds * 0.86f + i * 0.47f);
                DrawCube({x + carriage, y - side * 0.54f, -4.08f}, 0.52f, 0.18f, 0.34f, {64, 52, 39, 255});
                DrawCube({x - 0.42f, y - side * 0.34f, -4.08f}, 0.10f, 0.64f, 0.28f, {80, 64, 43, 255});
                DrawCube({x + 0.42f, y - side * 0.34f, -4.08f}, 0.10f, 0.64f, 0.28f, {80, 64, 43, 255});
                const float scanner = std::fmod(elapsedSeconds * 1.45f + i * 0.37f, 1.f) - 0.5f;
                glowCubes.push_back({{x + scanner * 1.1f, y - side * 0.72f, -3.98f}, {0.035f, 0.35f, 0.025f},
                                     Fade({255, 128, 40, 255}, 0.32f)});
            }
            if (serviceBay) {
                // Three offset feed-lines make these bays read as dense maintenance surfaces.
                for (int line = 0; line < 3; ++line) {
                    const float lineX = x - 0.24f + 0.24f * line;
                    DrawCylinderEx({lineX, y - side * 0.08f, -4.15f},
                                   {lineX, y - side * 1.00f, -4.15f}, 0.052f, 0.052f, 8,
                                   {52, 67, 74, 255});
                    const float signal = 0.5f + 0.5f * sinf(elapsedSeconds * 4.3f - line * 1.8f + i);
                    glowSpheres.push_back({{lineX, y - side * (0.24f + 0.64f * signal), -4.08f}, 0.035f,
                                           Fade({34, 190, 255, 255}, 0.34f)});
                }
            }
            // Each role also receives a deeper, wide facade.  These are intentionally shallow
            // overlays on the reusable hull: they give the route distinct rooms while retaining
            // the low-cost modular construction underneath.
            const float facadeY = side * 4.64f;
            const float facadeWidth = reactor ? 1.32f : cargoBay ? 1.64f : serviceBay ? 1.06f : 0.94f;
            const Color facade = reactor ? Color{20, 53, 63, 255}
                               : cargoBay ? Color{68, 44, 27, 255}
                               : bayRole == 3 ? Color{61, 28, 33, 255}
                                              : Color{38, 46, 51, 255};
            DrawCube({x, facadeY, -4.76f}, facadeWidth, 0.52f, 0.11f, facade);
            DrawCubeWires({x, facadeY, -4.75f}, facadeWidth + 0.025f, 0.545f, 0.12f,
                          reactor ? Color{38, 138, 160, 190} : Color{112, 80, 45, 170});
            if (reactor) {
                const float coil = 0.5f + 0.5f * sinf(elapsedSeconds * 3.1f + i * 0.74f);
                for (int lamp = -2; lamp <= 2; ++lamp) {
                    glowSpheres.push_back({{x + lamp * 0.20f, facadeY, -4.66f}, 0.032f + coil * 0.018f,
                                           Fade({42, 219, 255, 255}, 0.22f + coil * 0.24f)});
                }
            } else if (cargoBay) {
                const float loader = 0.46f * sinf(elapsedSeconds * 0.62f + i);
                DrawCube({x + loader, facadeY, -4.65f}, 0.28f, 0.34f, 0.045f, {108, 72, 42, 255});
                DrawCube({x - 0.55f, facadeY, -4.65f}, 0.05f, 0.38f, 0.045f, {142, 91, 35, 255});
                DrawCube({x + 0.55f, facadeY, -4.65f}, 0.05f, 0.38f, 0.045f, {142, 91, 35, 255});
            } else if (serviceBay) {
                const float scan = std::fmod(elapsedSeconds * 1.9f + i * 0.29f, 1.f) - 0.5f;
                glowCubes.push_back({{x + scan * facadeWidth, facadeY, -4.66f}, {0.045f, 0.40f, 0.035f},
                                     Fade({43, 191, 255, 255}, 0.38f)});
            } else if (bayRole == 3) {
                const float alarm = 0.5f + 0.5f * sinf(elapsedSeconds * 6.4f + i);
                glowSpheres.push_back({{x, facadeY, -4.65f}, 0.045f + alarm * 0.035f,
                                       Fade({255, 52, 61, 255}, 0.22f + alarm * 0.34f)});
            }
        }
        // These paired shutters cycle independently of the structural tile. They live outside
        // the safe central route until their gameplay collision pass is introduced.
        if ((i % 3) == 0) {
            const float cycle = 0.5f + 0.5f * sinf(elapsedSeconds * 0.72f + i * 1.13f);
            const float jaw = 0.36f + cycle * 0.56f;
            for (int side : {-1, 1}) {
                const Vector3 shutter{x - 0.72f, side * (5.62f - jaw * 0.5f), -4.02f};
                DrawCube(shutter, 0.72f, jaw, 0.46f, {34, 39, 47, 255});
                DrawCubeWires(shutter, 0.74f, jaw + 0.02f, 0.48f, {130, 82, 32, 180});
                glowSpheres.push_back({{x - 0.72f, side * (5.58f - jaw), -3.96f}, 0.045f,
                                       Fade({255, 120, 34, 255}, 0.25f + cycle * 0.35f)});
            }
        }
        // Background conduits are deliberately in a deeper layer than the working machinery.
        // Their staggered packets flow continuously along the ceiling and floor, breaking up the
        // repeated wall panels without obscuring the combat lane.
        const float conduitY = (i & 1) ? 5.53f : -5.53f;
        DrawCube({x, conduitY, -4.72f}, machinePitch * 0.92f, 0.11f, 0.12f, {29, 41, 47, 255});
        const float packet = std::fmod(elapsedSeconds * 2.9f + i * 0.31f, 1.f) - 0.5f;
        const Color packetColor = reactor ? Color{44, 215, 255, 255} : cargoBay ? Color{255, 137, 43, 255} : Color{112, 188, 214, 255};
        glowCubes.push_back({{x + packet * machinePitch * 0.72f, conduitY, -4.63f}, {0.24f, 0.055f, 0.025f},
                             Fade(packetColor, 0.40f)});
    }
    // One additive pass preserves all of the animated light detail while avoiding a GPU batch
    // flush for every individual lamp, scanner, and turret charge.
    BeginBlendMode(BLEND_ADDITIVE);
    for (const CorridorGlowSphere& glow : glowSpheres) DrawSphere(glow.pos, glow.radius, glow.color);
    for (const CorridorGlowCube& glow : glowCubes) DrawCubeV(glow.pos, glow.size, glow.color);
    EndBlendMode();
    rlDrawRenderBatchActive();
    rlPopMatrix();
    EndMode3D();
}

Vector3 Render3D::worldAt(Vector2 local, float depth) const {
    // CameraSystem::toWorld clamps to the visible playfield. Collision also needs objects that
    // are still entering from beyond the edge, so evaluate the same (affine) focal-plane map
    // from three on-screen samples and then apply the same eye-ray slide for depth.
    const Vector2 c = view_.toLocal({view_.pf.x + view_.pf.width * .5f, view_.pf.y + view_.pf.height * .5f});
    const Vector3 f0 = CameraSystem::toWorld(view_, cam_, c, 0.f);
    const Vector3 fx = Vector3Subtract(CameraSystem::toWorld(view_, cam_, {c.x + 1.f, c.y}, 0.f), f0);
    const Vector3 fy = Vector3Subtract(CameraSystem::toWorld(view_, cam_, {c.x, c.y + 1.f}, 0.f), f0);
    const Vector3 focal = Vector3Add(f0, Vector3Add(Vector3Scale(fx, local.x - c.x), Vector3Scale(fy, local.y - c.y)));
    if (cam_.projection != CAMERA_PERSPECTIVE || depth == 0.f) return focal;
    const float dist = Vector3Distance(cam_.position, cam_.target);
    const float d = Clamp(depth * CameraSystem::worldPerLocal(view_), -0.9f * dist, 0.9f * dist);
    return Vector3Add(cam_.position, Vector3Scale(Vector3Subtract(focal, cam_.position), (dist - d) / dist));
}

Render3D::BulkheadPose Render3D::bulkheadPose(float worldX) const {
    // The corridor hull is drawn on its -5 world-unit backdrop. Keep the door slightly
    // forward of that shell so its leaves remain visible over the wall geometry.
    constexpr float doorDepthWorld = -3.2f;
    BulkheadPose p;
    const float depthLocal = doorDepthWorld / CameraSystem::worldPerLocal(view_);
    p.scale = 1.f / fmaxf(CameraSystem::depthScale(view_, depthLocal), .01f);
    // Centred on the corridor's mid-line: the two leaves meet at lateral/2, like the passage.
    p.center = worldAt({worldX, view_.lateral() * .5f}, depthLocal);
    p.forward = Vector3Normalize(Vector3Subtract(cam_.target, cam_.position));
    p.right = Vector3Normalize(Vector3CrossProduct(p.forward, cam_.up));
    p.up = Vector3Normalize(Vector3CrossProduct(p.right, p.forward));
    return p;
}

Vector3 Render3D::bulkheadLocal(const BulkheadPose& p, Vector2 local, float depth) const {
    // Model axes as drawn: X -> up (across the corridor), Y -> right (flight), Z -> forward (depth).
    const Vector3 d = Vector3Subtract(worldAt(local, depth), p.center);
    return {Vector3DotProduct(d, p.up) / p.scale, Vector3DotProduct(d, p.right) / p.scale,
            Vector3DotProduct(d, p.forward) / p.scale};
}

void Render3D::drawBulkhead(float worldX, float opening, float elapsedSeconds, float playerWorldX) {
    if (!ok_ || view_.mode != Mode::SideScroll) return;
    const BulkheadPose bp = bulkheadPose(worldX);
    const float scale = bp.scale;
    const Vector3 center = bp.center, forward = bp.forward, right = bp.right, up = bp.up;
    const Vector3 towardEye = Vector3Negate(forward);
    Matrix basis = MatrixIdentity();
    // Rotate the local up axis by 90 degrees around the flight direction. The door
    // now spans the corridor instead of facing the cockpit; only its thin edge is
    // visible while approaching it.
    basis.m0=up.x; basis.m1=up.y; basis.m2=up.z;
    basis.m4=right.x; basis.m5=right.y; basis.m6=right.z;
    basis.m8=-towardEye.x; basis.m9=-towardEye.y; basis.m10=-towardEye.z;
    auto pose = [&](Vector3 at, Vector3 modelScale) {
        Matrix m=MatrixMultiply(MatrixScale(modelScale.x, modelScale.y, modelScale.z),basis);
        return MatrixMultiply(m,MatrixTranslate(at.x,at.y,at.z));
    };

    const Vector3 eye=cam_.position;
    const float metalness=.82f, roughness=.27f, envStrength=.86f;
    const float xrayDistance=32.f*view_.s;
    const float signedDistance=worldX-playerWorldX;
    bulkheadPillarVisual_.updateDistance(signedDistance, xrayDistance,
        [&](BulkheadPillarPhase phase) {
            if (getenv("BULKHEAD_REVIEW")) fprintf(stderr,"BULKHEAD_PILLAR_EVENT phase=%d distance=%.2f\n",(int)phase,signedDistance);
        });
    SetShaderValue(metal_,metalEye_,&eye,SHADER_UNIFORM_VEC3);
    SetShaderValue(metal_,metalness_,&metalness,SHADER_UNIFORM_FLOAT);
    SetShaderValue(metal_,metalRoughness_,&roughness,SHADER_UNIFORM_FLOAT);
    SetShaderValue(metal_,metalEnvStrength_,&envStrength,SHADER_UNIFORM_FLOAT);
    const float solidOpacity=1.f;
    SetShaderValue(metal_,metalOpacity_,&solidOpacity,SHADER_UNIFORM_FLOAT);
    rlDrawRenderBatchActive();
    BeginBlendMode(BLEND_ALPHA);
    BeginMode3D(cam_);
    BgModel& frame=bgModel("ship_interior/hazards/bulkhead_runtime_v11/frame.glb");
    if (frame.ok) {
        for (int i=0;i<frame.m.materialCount;++i) frame.m.materials[i].shader=metal_;
        frame.m.transform=pose(center,{scale,scale,scale});
        DrawModel(frame.m,{0,0,0},1.f,WHITE);
    }
    // Both sides use the exact same pillar asset. Only the screen-facing instance
    // receives the distance-driven x-ray opacity.
    BgModel& pillar=bgModel("ship_interior/hazards/bulkhead_runtime_v11/pillar.glb");
    if (pillar.ok) {
        for (int i=0;i<pillar.m.materialCount;++i) pillar.m.materials[i].shader=metal_;
        const Vector3 farAt=Vector3Add(center,Vector3Scale(towardEye,-7.15f*scale));
        pillar.m.transform=pose(farAt,{scale,scale,scale});
        DrawModel(pillar.m,{0,0,0},1.f,WHITE);
        const float pillarOpacity=bulkheadPillarVisual_.opacity();
        SetShaderValue(metal_,metalOpacity_,&pillarOpacity,SHADER_UNIFORM_FLOAT);
        const Vector3 nearAt=Vector3Add(center,Vector3Scale(towardEye,7.15f*scale));
        pillar.m.transform=pose(nearAt,{scale,scale,scale});
        DrawModel(pillar.m,{0,0,0},1.f,WHITE);
        SetShaderValue(metal_,metalOpacity_,&solidOpacity,SHADER_UNIFORM_FLOAT);
    }
    // Each rigid leaf is a fixed 5.64-unit panel. It retracts into the pocket above or
    // below the passage; only its position changes, never its dimensions. The constants are
    // shared with BulkheadModel so the collision volumes are the drawn leaves.
    constexpr float leafHeight=BulkheadModel::kLeafHeight;
    BgModel& leaf=bgModel("ship_interior/hazards/bulkhead_runtime_v11/leaf.glb");
    if (leaf.ok) {
        for (int i=0;i<leaf.m.materialCount;++i) leaf.m.materials[i].shader=metal_;
        const float leafCenter=BulkheadModel::leafCenter(opening);
        for (float side : {-1.f,1.f}) {
            const Vector3 at=Vector3Add(center,Vector3Scale(up,side*leafCenter*scale));
            leaf.m.transform=pose(at,{scale*(leafHeight/6.f),scale,scale});
            DrawModel(leaf.m,{0,0,0},1.f,WHITE);
        }
    }

    static const bool collisionDebug = getenv("BULKHEAD_COLLISION_DEBUG") != nullptr;
    if (collisionDebug) {  // the exact solid boxes gameplay collides with, in the door's frame
        Vector3 boxes[6][2];
        const int n = BulkheadModel::solids(opening, boxes, 6);
        rlDrawRenderBatchActive();
        rlPushMatrix();
        rlMultMatrixf(MatrixToFloat(pose(center, {scale, scale, scale})));
        for (int i = 0; i < n; ++i) {
            const Vector3 a{fmaxf(boxes[i][0].x, -9.f), boxes[i][0].y, boxes[i][0].z};
            const Vector3 b{fminf(boxes[i][1].x, 9.f), boxes[i][1].y, boxes[i][1].z};
            if (a.x >= b.x) continue;
            DrawCubeWiresV(Vector3Scale(Vector3Add(a, b), .5f), Vector3Subtract(b, a), RED);
        }
        rlDrawRenderBatchActive();
        rlPopMatrix();
    }
    // Embedded warning lamps are emitted in the same 3D frame as the moving door leaves.
    const float pulse=.72f+.28f*(.5f+.5f*sinf(elapsedSeconds*7.5f));
    const Color warning=opening>.94f ? Color{74,225,190,255}
                       : opening>.12f ? Color{255,151,52,255} : Color{255,48,34,255};
    BeginBlendMode(BLEND_ADDITIVE);
    for (float y : {-6.45f,6.45f}) for (float z : {-2.55f,2.55f}) {
        const Vector3 lamp=Vector3Add(center,Vector3Add(Vector3Scale(up,y*scale),
            Vector3Add(Vector3Scale(towardEye,.60f*scale),Vector3Scale(right,z*scale))));
        DrawSphere(lamp,.115f*scale,Fade(warning,pulse));
        DrawSphere(lamp,.25f*scale,Fade(warning,.15f*pulse));
    }
    EndBlendMode();
    EndMode3D();
    EndBlendMode();
}

namespace {
constexpr float kTurretBarrelPixels = 54.f;
constexpr float kTurretHardpointPixels = 70.f;
constexpr float kTurretBaseScale = .76f;
struct TurretPose {
    Matrix base{}, head{}, barrels{};
    Vector3 beacon{};
};

TurretPose turretPose(const View& view, const Camera3D& cam, Vector2 mount, Vector2 aim,
                      float barrelAngle, float recoil) {
    const Vector3 forward = Vector3Normalize(Vector3Subtract(cam.target, cam.position));
    const Vector3 right = Vector3Normalize(Vector3CrossProduct(forward, cam.up));
    const Vector3 up = Vector3Normalize(Vector3CrossProduct(right, forward));
    const Vector3 towardEye = Vector3Negate(forward);
    Matrix basis = MatrixIdentity();
    basis.m0=right.x; basis.m1=right.y; basis.m2=right.z;
    basis.m4=up.x; basis.m5=up.y; basis.m6=up.z;
    basis.m8=towardEye.x; basis.m9=towardEye.y; basis.m10=towardEye.z;
    const float scale = CameraSystem::worldPerLocal(view) * (kTurretBarrelPixels * view.s / 2.74f);
    const Vector3 pos = CameraSystem::toWorld(view, cam, mount, 0.f);
    const Vector2 dir = Vector2LengthSqr(aim) > .00001f ? Vector2Normalize(aim) : Vector2{-1.f, 0.f};
    const float angle = atan2f(-dir.y, dir.x), c = cosf(angle), sn = sinf(angle);
    // The mounting base and housing stay flipped against the ceiling. Only the barrels
    // rotate around their fixed elevation pivot; rotating the whole head turns the flat
    // mounting side away from the wall when the gun aims left.
    const Vector3 headPos = Vector3Add(pos, Vector3Scale(up, -.52f * scale));
    const Vector3 barrelPivot = Vector3Add(pos, Vector3Scale(up, -1.49f * scale));
    const Vector3 barrelPos = Vector3Add(barrelPivot,
        Vector3Add(Vector3Scale(right, -c * recoil * scale), Vector3Scale(up, -sn * recoil * scale)));
    auto matrix = [&](Matrix rotation, Vector3 p, float localScale = 1.f) {
        Matrix m = MatrixMultiply(MatrixScale(scale * localScale, scale * localScale, scale * localScale), rotation);
        m = MatrixMultiply(m, basis);
        return MatrixMultiply(m, MatrixTranslate(p.x, p.y, p.z));
    };
    const Matrix flipped = MatrixRotateX(PI);
    const Matrix aimRotation = MatrixMultiply(flipped, MatrixRotateZ(angle));
    TurretPose pose;
    pose.base = matrix(flipped, pos, kTurretBaseScale);
    pose.head = matrix(flipped, headPos);
    pose.barrels = matrix(MatrixMultiply(MatrixRotateX(barrelAngle), aimRotation), barrelPos);
    pose.beacon = Vector3Transform({-.8f, 1.50f, -.25f}, pose.head);
    return pose;
}
}

Vector2 Render3D::turretMuzzle(Vector2 mount, Vector2 aim, float barrelAngle, float recoil, int barrelIndex) const {
    if (!ok_) return mount;
    const TurretPose pose = turretPose(view_, cam_, mount, aim, barrelAngle, recoil);
    constexpr float kBarrelOrbitRadius = .32f;
    const float barrelPhase = barrelAngle + 2.f * PI * static_cast<float>((barrelIndex % kTurretBarrelCount + kTurretBarrelCount) % kTurretBarrelCount) /
                              static_cast<float>(kTurretBarrelCount);
    const Vector3 localMuzzle{2.74f, kBarrelOrbitRadius * cosf(barrelPhase), kBarrelOrbitRadius * sinf(barrelPhase)};
    return view_.toLocal(GetWorldToScreen(Vector3Transform(localMuzzle, pose.barrels), cam_));
}

void Render3D::drawTurret(Vector2 mount, Vector2 aim, float barrelAngle, float recoil, int firingBarrel, float muzzleFlash, bool destroyed) {
    if (!ok_ || view_.mode != Mode::SideScroll) return;
    const TurretPose pose = turretPose(view_, cam_, mount, aim, barrelAngle, recoil);
    rlDrawRenderBatchActive();
    BeginMode3D(cam_);
    const Vector3 eye = cam_.position;
    const float metalness = .84f, roughness = .22f, envStrength = .82f;
    const float opacity = 1.f;
    SetShaderValue(metal_, metalEye_, &eye, SHADER_UNIFORM_VEC3);
    SetShaderValue(metal_, metalness_, &metalness, SHADER_UNIFORM_FLOAT);
    SetShaderValue(metal_, metalRoughness_, &roughness, SHADER_UNIFORM_FLOAT);
    SetShaderValue(metal_, metalEnvStrength_, &envStrength, SHADER_UNIFORM_FLOAT);
    SetShaderValue(metal_, metalOpacity_, &opacity, SHADER_UNIFORM_FLOAT);
    BeginShaderMode(metal_);
    // A broad, flat hardpoint sits above the inverted base. It uses the exact same
    // transform as the mount and remains in place after the gun is destroyed.
    rlPushMatrix();
    // Keep the hardpoint at its original size while scaling the gun down inside it.
    const float hardpointScale = kTurretHardpointPixels / (kTurretBarrelPixels * kTurretBaseScale);
    const Matrix hardpoint = MatrixMultiply(MatrixScale(hardpointScale, hardpointScale, hardpointScale), pose.base);
    rlMultMatrixf(MatrixToFloat(hardpoint));
    DrawCube({0.f, -.16f, 0.f}, 3.8f, .16f, 2.8f, {54, 64, 74, 255});
    DrawCube({0.f, -.055f, 0.f}, 3.25f, .07f, 2.25f, {90, 103, 114, 255});
    DrawCubeWires({0.f, -.16f, 0.f}, 3.82f, .17f, 2.82f, {147, 161, 169, 255});
    for (int x : {-1, 1}) for (int z : {-1, 1}) {
        const Vector3 a{x * 1.55f, -.27f, z * 1.06f};
        const Vector3 b{x * 1.55f, -.17f, z * 1.06f};
        DrawCylinderEx(a, b, .09f, .09f, 8, {150, 157, 160, 255});
    }
    rlDrawRenderBatchActive();
    rlPopMatrix();
    EndShaderMode();
    auto drawPart = [&](const char* path, Matrix transform) {
        BgModel& model = bgModel(path);
        if (!model.ok) return;
        for (int i=0; i<model.m.materialCount; ++i) model.m.materials[i].shader = metal_;
        model.m.transform = transform;
        DrawModel(model.m, {0,0,0}, 1.f, WHITE);
    };
    if (destroyed) {
        drawPart("ship_interior/hazards/turret_wreck_runtime_v2.glb", pose.base);
    } else {
        drawPart("ship_interior/hazards/turret_runtime_v2/base.glb", pose.base);
        drawPart("ship_interior/hazards/turret_runtime_v2/head.glb", pose.head);
        drawPart("ship_interior/hazards/turret_runtime_v2/barrels.glb", pose.barrels);
        const float scale = CameraSystem::worldPerLocal(view_) * (kTurretBarrelPixels * view_.s / 2.74f);
        const float beacon = .35f + .65f * powf(.5f + .5f * sinf((float)GetTime() * 7.f), 6.f);
        BeginBlendMode(BLEND_ADDITIVE);
        DrawSphere(pose.beacon, .11f * scale, Fade({255, 68, 20, 255}, .3f + .5f * beacon));
        if (muzzleFlash > 0.f) {
            const float barrelPhase = barrelAngle + 2.f * PI * static_cast<float>((firingBarrel % kTurretBarrelCount + kTurretBarrelCount) % kTurretBarrelCount) /
                                      static_cast<float>(kTurretBarrelCount);
            const Vector3 localMuzzle{2.74f, .32f * cosf(barrelPhase), .32f * sinf(barrelPhase)};
            const Vector3 flashPos = Vector3Transform(localMuzzle, pose.barrels);
            const float intensity = Clamp(muzzleFlash / .075f, 0.f, 1.f);
            DrawSphere(flashPos, (.11f + .24f * intensity) * scale, Fade({255, 112, 48, 255}, intensity));
            DrawSphere(Vector3Add(flashPos, Vector3Scale(Vector3Normalize(Vector3Subtract(cam_.target, cam_.position)), -.12f * scale)),
                       .08f * scale, Fade({255, 235, 165, 255}, intensity));
        }
        EndBlendMode();
    }
    EndMode3D();
}

void Render3D::drawBackground3D(const std::vector<Background::Bg3D>& items) {
    bgTris_ = 0;
    if (!ok_ || items.empty()) return;
    rlDrawRenderBatchActive();
    rlDisableBackfaceCulling();  // mirrored terrain tiles flip winding
    BeginMode3D(renderCamera());
    const bool top = view_.mode == Mode::TopDown;
    const float ph = fmaxf(1.f, view_.pf.height), pw = fmaxf(1.f, view_.pf.width);
    const float pxPerUnit = ph / CameraSystem::kViewHeight, halfH = CameraSystem::kViewHeight * 0.5f, halfW = halfH * pw / ph;
    const Vector3 viewDir = Vector3Normalize(Vector3Subtract(cam_.target, cam_.position));
    for (const auto& it : items) {
        BgModel& b = bgModel(it.model);
        if (!b.ok) continue;
        const Vector3 tint{it.col.r / 255.f, it.col.g / 255.f, it.col.b / 255.f};
        Shader sh = it.terrain ? (b.proceduralTerrain ? terrainProcSh_ : terrainSh_) : planetSh_;
        for (int i = 0; i < b.m.materialCount; i++) b.m.materials[i].shader = sh;
        int tris = 0; for (int i = 0; i < b.m.meshCount; i++) tris += b.m.meshes[i].triangleCount;
        if (!it.terrain) {
            // local -> world without toWorld's clamp (planets scroll in/out past the edges), then pushed
            // 40 units away along the view ray: behind every ship, and no screen shift from the cam tilt
            Vector2 sp = view_.toScreen(it.pos);
            float nx = (sp.x - view_.pf.x) / pw, ny = (sp.y - view_.pf.y) / ph;
            Vector3 w = top ? Vector3{-halfW + nx * 2 * halfW, 0, -halfH + ny * 2 * halfH} : Vector3{-halfW + nx * 2 * halfW, halfH - ny * 2 * halfH, 0};
            w = Vector3Add(w, Vector3Scale(viewDir, 40));
            // Perspective: the planet really is 40 units deeper, so slide it out along the eye ray and
            // grow it by the same factor; its screen position and size stay as designed.
            const float camDist = Vector3Distance(cam_.position, cam_.target);
            const float farK = (camDist + 40.f) / fmaxf(camDist, 0.01f);
            w = Vector3Add(cam_.position, Vector3Scale(Vector3Subtract(Vector3Subtract(w, Vector3Scale(viewDir, 40)), cam_.position), farK));
            const float sc = it.sizePx / pxPerUnit / fmaxf(1e-3f, b.rMax) * farK;
            // A restrained screen-space axial tilt keeps the planet from reading as perfectly upright.
            constexpr float kPlanetAxisTilt = -0.10f;  // about 5.7 degrees
            // Spin direction: in the side view the camera looks down -Z, so a positive Y rotation slides the
            // planet's visible face to screen-RIGHT, against the leftward scroll (it read as the planet moving
            // backward / catching up). Turn the face WITH the scroll instead: same spin speed, opposite sign.
            const float spinRot = top ? it.rot : -it.rot, spinCloud = top ? it.cloudRot : -it.cloudRot;
            Matrix M = MatrixMultiply(MatrixMultiply(MatrixMultiply(MatrixRotateY(spinRot), MatrixRotateX(0.35f)), MatrixRotateZ(kPlanetAxisTilt)), MatrixScale(sc, sc, sc));
            M = MatrixMultiply(M, MatrixTranslate(w.x, w.y, w.z));
            float col[3] = {tint.x, tint.y, tint.z}, rr[2] = {b.rMin, b.rMax};
            SetShaderValue(planetSh_, pLocCol_, col, SHADER_UNIFORM_VEC3);
            SetShaderValue(planetSh_, pLocR_, rr, SHADER_UNIFORM_VEC2);
            const Vector3 eye = Vector3Negate(viewDir), sun = Vector3Normalize(top ? Vector3{-0.55f, 0.7f, -0.45f} : Vector3{-0.55f, 0.45f, 0.7f});
            SetShaderValue(planetSh_, GetShaderLocation(planetSh_, "eye"), &eye, SHADER_UNIFORM_VEC3);
            SetShaderValue(planetSh_, GetShaderLocation(planetSh_, "sun"), &sun, SHADER_UNIFORM_VEC3);
            b.m.transform = M;
            DrawModel(b.m, {0, 0, 0}, 1, WHITE);
            bgTris_ += tris;
            // Clouds are a separate low-poly shell so they can drift independently without
            // glTF animation playback. Draw after the opaque planet without depth writes.
            if (!it.cloudModel.empty()) {
                BgModel& cloud = bgModel(it.cloudModel);
                if (cloud.ok) {
                    for (int i = 0; i < cloud.m.materialCount; i++) cloud.m.materials[i].shader = cloudSh_;
                    const float cloudSc = it.sizePx / pxPerUnit / fmaxf(1e-3f, cloud.rMax) * farK;
                    cloud.m.transform = MatrixMultiply(MatrixMultiply(MatrixMultiply(MatrixRotateY(spinCloud), MatrixRotateX(0.35f)), MatrixRotateZ(kPlanetAxisTilt)),
                                                       MatrixScale(cloudSc, cloudSc, cloudSc));
                    cloud.m.transform = MatrixMultiply(cloud.m.transform, MatrixTranslate(w.x, w.y, w.z));
                    SetShaderValue(cloudSh_, GetShaderLocation(cloudSh_, "sun"), &sun, SHADER_UNIFORM_VEC3);
                    BeginBlendMode(BLEND_ALPHA);
                    rlDisableDepthMask();
                    DrawModel(cloud.m, {0, 0, 0}, 1, WHITE);
                    rlEnableDepthMask();
                    EndBlendMode();
                    bgTris_ += cloud.m.meshes[0].triangleCount;
                }
            }
        } else if (b.chunked) {
            drawPlanet(it, halfH, halfH * (pw + 2.f * view_.vis) / ph, pxPerUnit);
        } else {
            // Heightfield tile under the overhead playfield, scrolling toward the bottom (+Z). The source
            // isn't tileable, so every other tile is mirrored in Z: edges always meet their own copy.
            const float srcW = fmaxf(1e-3f, b.bmax.x - b.bmin.x), srcL = fmaxf(1e-3f, b.bmax.z - b.bmin.z);
            const float sc = (top ? 2 * halfW : 2 * halfH) * 1.06f / srcW, T = srcL * sc;
            const float off = std::fmod(it.offset / pxPerUnit, 2 * T);
            const float cx = (b.bmin.x + b.bmax.x) * 0.5f, cz = (b.bmin.z + b.bmax.z) * 0.5f;
            float col[3] = {tint.x, tint.y, tint.z};
            if (b.proceduralTerrain) {
                const float range[2] = {b.minHeight, b.maxHeight};
                const float low[3] = {b.low.r / 255.f, b.low.g / 255.f, b.low.b / 255.f};
                const float mid[3] = {b.mid.r / 255.f, b.mid.g / 255.f, b.mid.b / 255.f};
                const float high[3] = {b.high.r / 255.f, b.high.g / 255.f, b.high.b / 255.f};
                const float water[4] = {b.waterColor.r / 255.f, b.waterColor.g / 255.f, b.waterColor.b / 255.f, b.waterAlpha};
                SetShaderValueTexture(terrainProcSh_, tpLocPerm_, b.permutation);
                SetShaderValue(terrainProcSh_, tpLocGrid_, &b.gridSize, SHADER_UNIFORM_FLOAT);
                SetShaderValue(terrainProcSh_, tpLocHeight_, &b.heightScale, SHADER_UNIFORM_FLOAT);
                SetShaderValue(terrainProcSh_, tpLocFreq_, &b.frequency, SHADER_UNIFORM_FLOAT);
                SetShaderValue(terrainProcSh_, tpLocRidge_, &b.ridgeSharpness, SHADER_UNIFORM_FLOAT);
                SetShaderValueV(terrainProcSh_, tpLocCraters_, b.craters, SHADER_UNIFORM_VEC4, b.craterCount);
                SetShaderValue(terrainProcSh_, tpLocCraterCount_, &b.craterCount, SHADER_UNIFORM_INT);
                SetShaderValue(terrainProcSh_, tpLocRange_, range, SHADER_UNIFORM_VEC2);
                SetShaderValue(terrainProcSh_, tpLocTint_, col, SHADER_UNIFORM_VEC3);
                SetShaderValue(terrainProcSh_, tpLocLow_, low, SHADER_UNIFORM_VEC3);
                SetShaderValue(terrainProcSh_, tpLocMid_, mid, SHADER_UNIFORM_VEC3);
                SetShaderValue(terrainProcSh_, tpLocHigh_, high, SHADER_UNIFORM_VEC3);
                SetShaderValue(terrainProcSh_, tpLocWater_, water, SHADER_UNIFORM_VEC4);
                SetShaderValue(terrainProcSh_, tpLocWaterLevel_, &b.waterLevel, SHADER_UNIFORM_FLOAT);
            } else {
                const float hr[3] = {b.bmin.y, b.bmax.y, 0.45f};  // 0.45 = water in the legacy GLB
                SetShaderValue(terrainSh_, tLocTint_, col, SHADER_UNIFORM_VEC3);
                SetShaderValue(terrainSh_, tLocH_, hr, SHADER_UNIFORM_VEC3);
            }
            const float base = top ? -18.f : -18.f;  // well below the ships
            // Perspective: magnify the deep terrain across the view so it still fills the playfield.
            const Matrix layer = CameraSystem::backdropLayer(cam_, -base);
            // L6 full screen: extra mirrored columns either side so the ground reaches the window edges
            // (same scale, so nothing stretches; mirrored so the seams meet their own copy).
            const float tileW = srcW * sc, visHalfW = halfH * (pw + 2.f * view_.vis) / ph;
            const int cols = (top && view_.vis > 0.f && visHalfW > tileW * 0.5f) ? 1 : 0;
            // The side copies are mirrored, so the part next to the centre tile is the source tile's own
            // edge: draw only an edge strip that covers the visible margin plus a generous pad (deep
            // valleys project inward a little). While the 3D chase / rise camera is drawing (L6
            // 112-118 s, oblique, under the haze) the full tiles are kept so that view cannot change.
            int stripCells = 0;
            if (cols && b.proceduralTerrain && b.segments > 0 && !rcamOn_) {
                const float need = (visHalfW - tileW * 0.5f) / tileW;          // fraction of a tile on screen
                stripCells = std::min(b.segments, (int)std::ceil((need + 0.08f) * b.segments) + 2);
                if (stripCells >= b.segments) stripCells = 0;                    // would be the whole tile anyway
            }
            Matrix tiles[12]{}; Model* tileModel[12]{}; int tileCount = 0;
            for (int cIdx = -cols; cIdx <= cols; cIdx++) {
                const float xc = cIdx * tileW, mx = (cIdx & 1) ? -1.f : 1.f;
                if (xc - tileW / 2 > visHalfW || xc + tileW / 2 < -visHalfW) continue;
                for (int k = 0; k < 4; k++) {
                    const float zc = halfH + T * 0.5f - (k * T) + off - 2 * T + T;  // tile centres, step T
                    if (zc - T / 2 > halfH || zc + T / 2 < -halfH) continue;
                    const float mz = (k & 1) ? -1.f : 1.f;
                    Matrix M = MatrixMultiply(MatrixTranslate(-cx, 0, -cz), MatrixScale(sc * mx, sc, sc * mz));
                    M = MatrixMultiply(M, MatrixTranslate(xc, base, zc));
                    // side copy cIdx = +1 is mirrored (x -> tileW - x): it shows the source's +x edge; -1 the -x edge
                    tileModel[tileCount] = (cIdx != 0 && stripCells > 0) ? &terrainStrip(b, cIdx > 0 ? 1 : -1, stripCells) : &b.m;
                    tiles[tileCount++] = MatrixMultiply(M, layer);
                }
            }
            if (b.proceduralTerrain) {
                const int ground = 0;
                SetShaderValue(terrainProcSh_, tpLocIsWater_, &ground, SHADER_UNIFORM_INT);
                static const bool tlog = getenv("TERRAIN_LOG") != nullptr;
                int drawnTris = 0;
                for (int k = 0; k < tileCount; k++) {
                    Model& tm = *tileModel[k];
                    tm.transform = tiles[k];
                    DrawModel(tm, {0, 0, 0}, 1, WHITE);
                    drawnTris += tm.meshes[0].triangleCount;
                }
                bgTris_ += drawnTris;
                if (tlog) { static double lt = 0; if (GetTime() - lt > 1.0) { lt = GetTime(); fprintf(stderr, "TERRAIN tiles=%d strip=%d/%d tris=%d\n", tileCount, stripCells, b.segments, drawnTris); } }
                if (b.hasWater && b.water.meshCount > 0) {
                    const int water = 1;
                    SetShaderValue(terrainProcSh_, tpLocIsWater_, &water, SHADER_UNIFORM_INT);
                    BeginBlendMode(BLEND_ALPHA);
                    rlDisableDepthMask();
                    for (int k = 0; k < tileCount; k++) {
                        b.water.transform = tiles[k];
                        DrawModel(b.water, {0, 0, 0}, 1, WHITE);
                        bgTris_ += b.water.meshes[0].triangleCount;
                    }
                    rlEnableDepthMask();
                    EndBlendMode();
                }
            } else for (int k = 0; k < tileCount; k++) {
                b.m.transform = tiles[k];
                DrawModel(b.m, {0, 0, 0}, 1, WHITE);
                bgTris_ += tris;
            }
        }
    }
    EndMode3D();
    rlEnableBackfaceCulling();
}

// ============================================================ Level 6 chunked ground + ground guns
bool Render3D::ensurePlanet(const std::string& model) {
    if (!ok_ || model.empty()) return false;
    BgModel& b = bgModel(model);
    return b.chunked && b.ok;
}

void Render3D::prefetchPlanet(const std::string& model, float scrollUnits) {
    if (!ensurePlanet(model) || !planet_) return;
    const float ph = fmaxf(1.f, view_.pf.height), pw = fmaxf(1.f, view_.pf.width);
    const float halfH = CameraSystem::kViewHeight * 0.5f, visHalfW = halfH * (pw + 2.f * view_.vis) / ph;
    const float C = planet_->params().chunkSize;
    planet_->request(-visHalfW - 1.2f, visHalfW + 1.2f, -halfH - 1.5f - scrollUnits - C, halfH + 1.5f - scrollUnits);
    planet_->pump(4);
}

void Render3D::warmPlanet(const std::string& model, float x0, float x1, float z0, float z1, float timeoutSec) {
    if (!ensurePlanet(model) || !planet_) return;
    planet_->request(x0, x1, z0, z1);
    const double t0 = GetTime();
    while (!planet_->covered(x0, x1, z0, z1) && GetTime() - t0 < timeoutSec) {
        planet_->pump(64);
        std::this_thread::sleep_for(std::chrono::milliseconds(4));
    }
    planet_->pump(64);
    fprintf(stderr, "PLANET warm covered=%d in %.2fs\n", (int)planet_->covered(x0, x1, z0, z1), GetTime() - t0);
}

void Render3D::drawPlanet(const Background::Bg3D& it, float halfH, float visHalfW, float pxPerUnit) {
    if (!planet_ || !planet_->ready()) return;
    const float S = it.offset / pxPerUnit, base = kGroundBase, C = planet_->params().chunkSize;
    Matrix layer = CameraSystem::backdropLayer(cam_, -base);
    // Liftoff climb: the same ground, further away (it shrinks toward the screen centre).
    const float camD = Vector3Distance(cam_.position, cam_.target), grow = groundDrop_ > 0.f ? (camD - base + groundDrop_) / fmaxf(1.f, camD - base) : 1.f;
    if (groundDrop_ > 0.f) layer = MatrixMultiply(layer, MatrixTranslate(Vector3Scale(Vector3Normalize(Vector3Subtract(cam_.target, cam_.position)), groundDrop_).x,
                                                                         Vector3Scale(Vector3Normalize(Vector3Subtract(cam_.target, cam_.position)), groundDrop_).y,
                                                                         Vector3Scale(Vector3Normalize(Vector3Subtract(cam_.target, cam_.position)), groundDrop_).z));
    // Visible ground rectangle (+ a pad for relief / prop overhang); the oblique rise camera sees
    // further ahead and wider.
    float x0 = (-visHalfW - 1.2f) * grow, x1 = (visHalfW + 1.2f) * grow, Z0 = (-halfH - 1.5f) * grow, Z1 = (halfH + 1.5f) * grow;
    if (rcamOn_) { Z0 -= 2.f * C; x0 -= C; x1 += C; }
    planet_->setLight(sceneToSun_, sceneTerrainAmb_);
    planet_->request(x0, x1, Z0 - S - C, Z1 - S);   // + one row ahead of the scroll
    planet_->pump(planet_->covered(x0, x1, Z0 - S, Z1 - S) ? 3 : 64);
    planet_->draw(layer, base, S, x0, x1, Z0 - S, Z1 - S, renderCamera().position, planetPropFar_);
    const PlanetRenderer::Stats& st = planet_->stats;
    bgTris_ += st.terrainTris + st.propTris + st.shadowQuads * 2;
    if (groundDrop_ <= 0.f) drawGroundGuns(layer, S);
    static const bool tlog = getenv("TERRAIN_LOG") != nullptr;
    if (tlog) {
        static double lt = 0;
        if (GetTime() - lt > 1.0) {
            lt = GetTime();
            fprintf(stderr, "PLANET chunks=%d cached=%d pending=%d terrainTris=%d propTris=%d shadows=%d draws=%d guns=%d scroll=%.2f sun=(%.2f,%.2f,%.2f) amb=%.2f dist=%.1f..%.1f fps=%d drop=%.1f camD=%.1f\n",
                    st.chunks, st.cached, st.pending, st.terrainTris, st.propTris, st.shadowQuads, st.draws, (int)groundGuns_.size(), S, sceneToSun_.x, sceneToSun_.y, sceneToSun_.z, sceneTerrainAmb_, st.dMin, st.dMax, GetFPS(), groundDrop_, Vector3Distance(cam_.position, cam_.target));
        }
    }
}

namespace {
constexpr float kGroundGunScale = 1.0f;   // relative to the Level 3 turret's on-screen size
float groundGunWorldScale(const View& v) { return CameraSystem::worldPerLocal(v) * (kTurretBarrelPixels * v.s / 2.74f) * kGroundGunScale; }
// Upright version of turretPose on the ground (pre-layer ground space, then `layer`): base and head
// turn with the yaw, the barrel cluster elevates toward the ship and spins about its own axis.
TurretPose groundTurretPose(const Matrix& layer, Vector3 pos, float scale, float yaw, float pitch, float barrelAngle, float recoil) {
    auto M = [&](Matrix rot, Vector3 p, float ls) {
        Matrix m = MatrixMultiply(MatrixScale(scale * ls, scale * ls, scale * ls), rot);
        m = MatrixMultiply(m, MatrixTranslate(p.x, p.y, p.z));
        return MatrixMultiply(m, layer);
    };
    const Matrix ry = MatrixRotateY(yaw);
    const Vector3 d{cosf(pitch) * cosf(yaw), sinf(pitch), -cosf(pitch) * sinf(yaw)};
    TurretPose pose;
    pose.base = M(ry, pos, kTurretBaseScale);
    pose.head = M(ry, Vector3Add(pos, {0.f, .52f * scale, 0.f}), 1.f);
    const Vector3 pivot = Vector3Subtract(Vector3Add(pos, {0.f, 1.49f * scale, 0.f}), Vector3Scale(d, recoil * scale));
    pose.barrels = M(MatrixMultiply(MatrixMultiply(MatrixRotateX(barrelAngle), MatrixRotateZ(pitch)), ry), pivot, 1.f);
    pose.beacon = Vector3Transform({-.8f, 1.50f, -.25f}, pose.head);
    return pose;
}
}  // namespace

Vector2 Render3D::groundLocal(float gx, float gy, float groundZ) const {
    if (!ok_) return {};
    const Vector3 r = Vector3Transform({gx, kGroundBase + gy, groundZ}, CameraSystem::backdropLayer(cam_, -kGroundBase));
    return view_.toLocal(GetWorldToScreen(r, renderCamera()));
}

Vector2 Render3D::groundGunMuzzle(const GroundGunDraw& g, float scroll, int barrel) const {
    if (!ok_) return {};
    const Matrix layer = CameraSystem::backdropLayer(cam_, -kGroundBase);
    const TurretPose pose = groundTurretPose(layer, {g.gx, kGroundBase + g.gy, g.gz + scroll}, groundGunWorldScale(view_), g.yaw, g.pitch, g.barrelAngle, g.recoil);
    const float ph = g.barrelAngle + 2.f * PI * (float)((barrel % kTurretBarrelCount + kTurretBarrelCount) % kTurretBarrelCount) / (float)kTurretBarrelCount;
    return view_.toLocal(GetWorldToScreen(Vector3Transform({2.74f, .32f * cosf(ph), .32f * sinf(ph)}, pose.barrels), renderCamera()));
}

void Render3D::drawGroundGuns(const Matrix& layer, float scroll) {
    if (groundGuns_.empty()) return;
    const float halfH = CameraSystem::kViewHeight * 0.5f;
    const float scale = groundGunWorldScale(view_);
    const float dist = Vector3Distance(cam_.position, cam_.target), k = (dist - kGroundBase) / fmaxf(dist, .01f);
    const Vector3 eye = renderCamera().position;
    // outdoors under the sun: less mirror-like than the L3 interior turrets (the env map there is the
    // lit ship interior, here it would read as black)
    const float metalness = .50f, roughness = .45f, envStrength = .45f, opacity = 1.f;
    const float padMetal = .05f, padRough = .90f;
    rlDrawRenderBatchActive();
    SetShaderValue(metal_, metalEye_, &eye, SHADER_UNIFORM_VEC3);
    SetShaderValue(metal_, metalness_, &metalness, SHADER_UNIFORM_FLOAT);
    SetShaderValue(metal_, metalRoughness_, &roughness, SHADER_UNIFORM_FLOAT);
    SetShaderValue(metal_, metalEnvStrength_, &envStrength, SHADER_UNIFORM_FLOAT);
    SetShaderValue(metal_, metalOpacity_, &opacity, SHADER_UNIFORM_FLOAT);
    auto drawPart = [&](const char* path, Matrix transform) {
        BgModel& model = bgModel(path);
        if (!model.ok) return;
        for (int i = 0; i < model.m.materialCount; ++i) model.m.materials[i].shader = metal_;
        model.m.transform = transform;
        DrawModel(model.m, {0, 0, 0}, 1.f, WHITE);
    };
    for (const GroundGunDraw& g : groundGuns_) {
        const float Z = g.gz + scroll;
        if (Z < -halfH - 3.f || Z > halfH + 3.f) continue;
        const Vector3 pos{g.gx, kGroundBase + g.gy, Z};
        const TurretPose pose = groundTurretPose(layer, pos, scale, g.yaw, g.pitch, g.barrelAngle, g.recoil);
        static const bool glog = getenv("GUN_LOG") != nullptr;
        if (glog) { static double lt = 0; if (GetTime() - lt > 1.0) { lt = GetTime(); const Vector2 sp = GetWorldToScreen(Vector3Transform(pos, layer), renderCamera());
            fprintf(stderr, "GUNDRAW scale=%.3f screen=(%.0f,%.0f) destroyed=%d\n", scale, sp.x, sp.y, g.destroyed); } }
        // concrete emplacement pad flush with the (flattened) ground
        SetShaderValue(metal_, metalness_, &padMetal, SHADER_UNIFORM_FLOAT);
        SetShaderValue(metal_, metalRoughness_, &padRough, SHADER_UNIFORM_FLOAT);
        BeginShaderMode(metal_);
        rlPushMatrix();
        const float ps = scale * 0.55f;
        rlMultMatrixf(MatrixToFloat(MatrixMultiply(MatrixMultiply(MatrixScale(ps, ps, ps), MatrixTranslate(pos.x, pos.y, pos.z)), layer)));
        DrawCylinder({0.f, -.25f, 0.f}, 2.35f, 2.55f, .32f, 12, {78, 74, 86, 255});
        DrawCylinder({0.f, .07f, 0.f}, 1.85f, 2.0f, .06f, 12, {104, 100, 114, 255});
        rlDrawRenderBatchActive();
        rlPopMatrix();
        EndShaderMode();
        SetShaderValue(metal_, metalness_, &metalness, SHADER_UNIFORM_FLOAT);
        SetShaderValue(metal_, metalRoughness_, &roughness, SHADER_UNIFORM_FLOAT);
        if (g.destroyed) {
            drawPart("ship_interior/hazards/turret_wreck_runtime_v2.glb", pose.base);
            continue;
        }
        drawPart("ship_interior/hazards/turret_runtime_v2/base.glb", pose.base);
        drawPart("ship_interior/hazards/turret_runtime_v2/head.glb", pose.head);
        drawPart("ship_interior/hazards/turret_runtime_v2/barrels.glb", pose.barrels);
        const float beacon = .35f + .65f * powf(.5f + .5f * sinf((float)GetTime() * 7.f), 6.f);
        BeginBlendMode(BLEND_ADDITIVE);
        DrawSphere(pose.beacon, .11f * scale * k, Fade({255, 68, 20, 255}, .3f + .5f * beacon));
        if (g.muzzleFlash > 0.f) {
            const float ph = g.barrelAngle + 2.f * PI * (float)((g.flashBarrel % kTurretBarrelCount + kTurretBarrelCount) % kTurretBarrelCount) / (float)kTurretBarrelCount;
            const Vector3 flashPos = Vector3Transform({2.74f, .32f * cosf(ph), .32f * sinf(ph)}, pose.barrels);
            const float intensity = Clamp(g.muzzleFlash / .075f, 0.f, 1.f);
            DrawSphere(flashPos, (.11f + .24f * intensity) * scale * k, Fade({255, 112, 48, 255}, intensity));
        }
        EndBlendMode();
        rlDrawRenderBatchActive();
    }
}
